#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <syslog.h>
#include <time.h>
#include <linux/rtnetlink.h>
#include <linux/pkt_sched.h>
#include <netlink/netlink.h>
#include <netlink/msg.h>
#include <netlink/attr.h>
#include <netlink/socket.h>
#include <netlink/utils.h>
#include <netlink/cache.h>
#include <netlink/route/link.h>
#include <netlink/route/qdisc.h>
#include <netlink/route/qdisc/netem.h>

#include "jittertrap.h"
#include "netem.h"
#include "timeywimey.h"

static struct nl_sock *sock;
static struct nl_cache *link_cache, *qdisc_cache;

static const char NETEM_KIND[] = "netem";
/* Matches the tc(8) default; the previous libnl-based set path inherited
 * this same default. */
#define NETEM_QUEUE_LIMIT 1000

#define QUOTE(str) #str
#define EXPAND_AND_QUOTE(str) QUOTE(str)

/* Runtime allowed interfaces list (colon-separated).
 * Empty string means all interfaces are allowed.
 * Initialized from compile-time default, can be overridden at runtime.
 */
static char *allowed_ifaces = NULL;

const char *get_allowed_ifaces(void)
{
	if (allowed_ifaces)
		return allowed_ifaces;
	return EXPAND_AND_QUOTE(ALLOWED_IFACES);
}

void set_allowed_ifaces(const char *ifaces)
{
	free(allowed_ifaces);
	allowed_ifaces = ifaces ? strdup(ifaces) : NULL;
}

int netem_init(void)
{
	/* Allocate and initialize a new netlink handle */
	if (!(sock = nl_socket_alloc())) {
		syslog(LOG_ERR, "Failed to alloc netlink socket\n");
		return -EOPNOTSUPP;
	}

	/* Bind and connect socket to protocol, NETLINK_ROUTE in our case. */
	if (nl_connect(sock, NETLINK_ROUTE) < 0) {
		syslog(LOG_ERR, "Failed to connect to kernel\n");
		return -EOPNOTSUPP;
	}

	/* Retrieve a list of all available interfaces and populate cache. */
	if (rtnl_link_alloc_cache(sock, AF_UNSPEC, &link_cache) < 0) {
		syslog(LOG_ERR, "Error creating link cache\n");
		return -EOPNOTSUPP;
	}

	/* Retrieve a list of all available qdiscs and populate cache. */
	if (rtnl_qdisc_alloc_cache(sock, &qdisc_cache) < 0) {
		syslog(LOG_ERR, "Error creating qdisc cache\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

int is_iface_allowed(const char *needle)
{
	const char *haystack = get_allowed_ifaces();
	char *tokens = strdup(haystack);
	char *iface;

	if (0 == strlen(haystack)) {
		free(tokens);
		return 1;
	}

	iface = strtok(tokens, ":");
	while (iface) {
		if (0 == strcmp(iface, needle)) {
			free(tokens);
			return 1;
		}
		iface = strtok(NULL, ":");
	}
	free(tokens);
	return 0;
}

char **netem_list_ifaces(void)
{
	struct rtnl_link *link;
	char **ifaces;
	char **i;
	int count, size;

	pthread_mutex_lock(&nl_sock_mutex);

	count = nl_cache_nitems(link_cache);
	size = (count + 1) * sizeof(char *);

	ifaces = malloc(size);
	assert(NULL != ifaces);
	i = ifaces;
	link = (struct rtnl_link *)nl_cache_get_first(link_cache);
	while (link) {
		char *j = rtnl_link_get_name(link);

		/* Include loopback only if explicitly allowed via -a lo.
		 * This allows testing with loopback while keeping it hidden
		 * by default in the UI interface list. */
		if (is_iface_allowed(j)) {
			*i = malloc(strlen(j) + 1);
			assert(NULL != *i);
			sprintf(*i, "%s", j);
			i++;
		}
		link = (struct rtnl_link *)nl_cache_get_next(
		    (struct nl_object *)link);
	}
	*i = 0;

	pthread_mutex_unlock(&nl_sock_mutex);
	return ifaces;
}

#if 1
/* The nl_cache_find function is missing from the version of libnl that shipped
 * with Ubuntu 12.04 (LTS), which is still in common use as of 2015-02-13.
 *
 * The nl_cache_find defined below is a poor substitute for the one in new
 * versions of libnl, but enables these older distributions. It is marked as
 * weak so that the libnl version will be used if it exists at link time.
 */

/* Callback used by nl_cache_foreach_filter in nl_cache_find.
 * Sets parameter p to point to parameter found
 */
void cb_found_cache_obj(struct nl_object *found, void *p)
{
	struct nl_object **out = (struct nl_object **)p;
	nl_object_get(found);
	*out = found;
}

/**
 * Find object in cache
 * @arg cache           Cache
 * @arg filter          object acting as a filter
 *
 * Searches the cache for an object which matches the object filter.
 * nl_object_match_filter() is used to determine if the objects match.
 * If a matching object is found, the reference counter is incremented and the
 * object is returned.
 *
 * Therefore, if an object is returned, the reference to the object
 * must be returned by calling nl_object_put() after usage.
 *
 * @return Reference to object or NULL if not found.
 */
__attribute__((weak)) struct nl_object *
nl_cache_find(struct nl_cache *cache, struct nl_object *filter)
{
	struct nl_object *obj = NULL;

	/* obj will be set to point to the matched object, so if there are
	 * multiple matches, obj will be set to point to the last match.
	 */
	nl_cache_foreach_filter(cache, filter, cb_found_cache_obj, &obj);
	return obj;
}

#endif

/* Parsing callback for fetch_netem_rate. The kernel sends one RTM_NEWQDISC
 * per (ifindex, qdisc) pair when we issue a dump. We pick the one matching
 * the requested ifindex with kind=netem and pull TCA_NETEM_RATE out of the
 * raw TCA_OPTIONS payload. */
struct rate_query_ctx {
	int ifindex;
	uint32_t rate_kbps;
};

static int parse_rate_cb(struct nl_msg *msg, void *arg)
{
	struct rate_query_ctx *ctx = arg;
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct tcmsg *tcm = nlmsg_data(nlh);
	struct nlattr *tb[TCA_MAX + 1] = { 0 };
	struct nlattr *pos, *nested_head;
	void *opts;
	int opts_len;
	int rem;
	size_t qopt_aligned;

	if (nlmsg_parse(nlh, sizeof(*tcm), tb, TCA_MAX, NULL) < 0)
		return NL_OK;
	if (tcm->tcm_ifindex != ctx->ifindex)
		return NL_OK;
	if (!tb[TCA_KIND] ||
	    strcmp(nla_data(tb[TCA_KIND]), NETEM_KIND) != 0)
		return NL_OK;
	if (!tb[TCA_OPTIONS])
		return NL_OK;

	opts = nla_data(tb[TCA_OPTIONS]);
	opts_len = nla_len(tb[TCA_OPTIONS]);
	qopt_aligned = NLA_ALIGN(sizeof(struct tc_netem_qopt));
	if ((size_t)opts_len < qopt_aligned)
		return NL_OK;

	/* TCA_OPTIONS = tc_netem_qopt header followed by nested TCA_NETEM_*
	 * attributes. The kernel does not wrap the qopt in its own nla
	 * header; it's raw bytes at the start. */
	nested_head = (struct nlattr *)((char *)opts + qopt_aligned);
	nla_for_each_attr(pos, nested_head, opts_len - qopt_aligned, rem) {
		if (nla_type(pos) == TCA_NETEM_RATE &&
		    nla_len(pos) >= (int)sizeof(struct tc_netem_rate)) {
			struct tc_netem_rate r;
			uint64_t kbps;
			memcpy(&r, nla_data(pos), sizeof(r));
			kbps = (uint64_t)r.rate * 8ULL / 1000ULL;
			ctx->rate_kbps = kbps > UINT32_MAX
			                     ? UINT32_MAX
			                     : (uint32_t)kbps;
		}
	}
	return NL_OK;
}

/* Dump qdiscs over a fresh socket and parse out TCA_NETEM_RATE for ifindex.
 * Uses its own socket to avoid contending with the long-lived `sock` and
 * its cache callbacks. On error returns -1 and leaves *rate_kbps unchanged.
 */
static int fetch_netem_rate(int ifindex, uint32_t *rate_kbps)
{
	struct nl_sock *s;
	struct nl_msg *msg;
	struct tcmsg tch;
	struct rate_query_ctx ctx = { .ifindex = ifindex, .rate_kbps = 0 };
	int err;

	s = nl_socket_alloc();
	if (!s)
		return -1;
	if (nl_connect(s, NETLINK_ROUTE) < 0)
		goto fail_sock;

	nl_socket_modify_cb(s, NL_CB_VALID, NL_CB_CUSTOM, parse_rate_cb, &ctx);

	msg = nlmsg_alloc();
	if (!msg)
		goto fail_sock;
	memset(&tch, 0, sizeof(tch));
	tch.tcm_family = AF_UNSPEC;
	tch.tcm_ifindex = ifindex;
	if (!nlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, RTM_GETQDISC, 0,
	               NLM_F_REQUEST | NLM_F_DUMP)) {
		nlmsg_free(msg);
		goto fail_sock;
	}
	if (nlmsg_append(msg, &tch, sizeof(tch), NLMSG_ALIGNTO) < 0) {
		nlmsg_free(msg);
		goto fail_sock;
	}

	err = nl_send_auto(s, msg);
	nlmsg_free(msg);
	if (err < 0)
		goto fail_sock;

	/* Drain the dump until NLMSG_DONE (libnl returns 0). */
	while ((err = nl_recvmsgs_default(s)) > 0) { }

	nl_socket_free(s);
	*rate_kbps = ctx.rate_kbps;
	return 0;

fail_sock:
	nl_socket_free(s);
	return -1;
}

int netem_get_params(char *iface, struct netem_params *params)
{
	struct rtnl_link *link;
	struct rtnl_qdisc *filter_qdisc;
	struct rtnl_qdisc *found_qdisc = NULL;
	int err;
	int delay, jitter, loss;
	int ifindex = -1;

	pthread_mutex_lock(&nl_sock_mutex);
	if ((err = nl_cache_refill(sock, link_cache)) < 0) {
		syslog(LOG_ERR, "Unable to resync link cache: %s\n",
		       nl_geterror(err));
		goto cleanup;
	}

	if ((err = nl_cache_refill(sock, qdisc_cache)) < 0) {
		syslog(LOG_ERR, "Unable to resync link cache: %s\n",
		        nl_geterror(err));
		goto cleanup;
	}

	/* filter link by name */
	if ((link = rtnl_link_get_by_name(link_cache, iface)) == NULL) {
		syslog(LOG_ERR, "unknown interface/link name: %s\n", iface);
		goto cleanup;
	}
	ifindex = rtnl_link_get_ifindex(link);

	if (!(filter_qdisc = rtnl_qdisc_alloc())) {
		/* OOM error */
		syslog(LOG_ERR, "couldn't alloc qdisc\n");
		goto cleanup_link;
	}

	rtnl_tc_set_link(TC_CAST(filter_qdisc), link);
	rtnl_tc_set_parent(TC_CAST(filter_qdisc), TC_H_ROOT);
	rtnl_tc_set_kind(TC_CAST(filter_qdisc), "netem");

	found_qdisc = (struct rtnl_qdisc *)nl_cache_find(
	    qdisc_cache, OBJ_CAST(filter_qdisc));
	if (!found_qdisc) {
		/* The iface probably doesn't have a netem qdisc at startup. */
		goto cleanup_filter_qdisc;
	}

	if (0 > (delay = rtnl_netem_get_delay(found_qdisc))) {
		syslog(LOG_ERR, "couldn't get delay for iface: %s\n", iface);
		goto cleanup_qdisc;
	}
	params->delay = (double)delay / 1000;

	if (0 > (jitter = rtnl_netem_get_jitter(found_qdisc))) {
		syslog(LOG_ERR, "couldn't get jitter for iface: %s\n", iface);
		goto cleanup_qdisc;
	}
	params->jitter = (double)jitter / 1000;

	if (0 > (loss = rtnl_netem_get_loss(found_qdisc))) {
		syslog(LOG_ERR, "couldn't get loss for iface: %s\n", iface);
		goto cleanup_qdisc;
	}
	/* loss is specified in 10ths of a percent, ie. 1 ==> 0.1% */
	params->loss = (int)(loss / (UINT_MAX / 1000));

	rtnl_qdisc_put(found_qdisc);
	rtnl_qdisc_put(filter_qdisc);
	rtnl_link_put(link);
	pthread_mutex_unlock(&nl_sock_mutex);

	/* Rate read is best-effort: it uses an auxiliary socket so it must
	 * happen outside nl_sock_mutex, and a failure (e.g. transient ENOBUFS
	 * on the dump) shouldn't fail the whole get. */
	params->rate = 0;
	if (ifindex >= 0 && fetch_netem_rate(ifindex, &params->rate) < 0) {
		syslog(LOG_WARNING,
		       "netem rate read failed for %s; reporting rate=0",
		       iface);
	}
	return 0;

cleanup_qdisc:
	rtnl_qdisc_put(found_qdisc);
cleanup_filter_qdisc:
	rtnl_qdisc_put(filter_qdisc);
cleanup_link:
	rtnl_link_put(link);
cleanup:
	pthread_mutex_unlock(&nl_sock_mutex);
	return -1;
}

/* libnl-route 3.x does not expose rtnl_netem_set_rate, so the qdisc message is
 * built manually here. The wire format inside TCA_OPTIONS is:
 *   - struct tc_netem_qopt (raw bytes, no nla header)
 *   - followed by optional nested attributes (TCA_NETEM_RATE, ...)
 * This is the layout that the kernel netem_change() expects and matches what
 * the tc(8) utility produces. */
int netem_set_params(const char *iface, struct netem_params *params)
{
	struct rtnl_link *link;
	struct nl_msg *msg = NULL;
	struct nlattr *opts;
	struct tcmsg tch;
	struct tc_netem_qopt qopt;
	int ifindex;
	int err;

	pthread_mutex_lock(&nl_sock_mutex);

	if ((link = rtnl_link_get_by_name(link_cache, iface)) == NULL) {
		syslog(LOG_ERR, "unknown interface/link name.\n");
		pthread_mutex_unlock(&nl_sock_mutex);
		return -1;
	}
	ifindex = rtnl_link_get_ifindex(link);
	rtnl_link_put(link);

	msg = nlmsg_alloc();
	if (!msg) {
		syslog(LOG_ERR, "couldn't alloc nl_msg\n");
		pthread_mutex_unlock(&nl_sock_mutex);
		return -1;
	}

	memset(&tch, 0, sizeof(tch));
	tch.tcm_family = AF_UNSPEC;
	tch.tcm_ifindex = ifindex;
	tch.tcm_parent = TC_H_ROOT;

	/* Pass 0 for payload size so nlmsg_put doesn't reserve any bytes
	 * before the appended family header — the kernel reads tcmsg at
	 * NLMSG_DATA, so the appended bytes must be flush against the
	 * nlmsghdr. */
	if (!nlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, RTM_NEWQDISC, 0,
	               NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE |
	                   NLM_F_REPLACE)) {
		goto fail_msg;
	}
	if (nlmsg_append(msg, &tch, sizeof(tch), NLMSG_ALIGNTO) < 0)
		goto fail_msg;

	if (nla_put_string(msg, TCA_KIND, NETEM_KIND) < 0)
		goto fail_msg;

	opts = nla_nest_start(msg, TCA_OPTIONS);
	if (!opts)
		goto fail_msg;

	memset(&qopt, 0, sizeof(qopt));
	/* latency and jitter are in PSCHED ticks, not microseconds. */
	qopt.latency = nl_us2ticks(params->delay * 1000);
	qopt.jitter = nl_us2ticks(params->jitter * 1000);
	qopt.loss = params->loss * (UINT_MAX / 1000);
	qopt.limit = NETEM_QUEUE_LIMIT;
	if (nlmsg_append(msg, &qopt, sizeof(qopt), NLMSG_ALIGNTO) < 0)
		goto fail_msg;

	{
		/* TCA_NETEM_RATE is always emitted, even when rate==0, so that
		 * a previously-set rate is explicitly cleared. The kernel only
		 * updates q->rate from attributes that are present. */
		struct tc_netem_rate r;
		uint64_t bytes_per_sec = (uint64_t)params->rate * 1000ULL / 8ULL;

		memset(&r, 0, sizeof(r));
		/* TCA_NETEM_RATE.rate is u32 byte/s; cap at UINT32_MAX. The
		 * RATE64 attribute exists for >4Gbit/s use cases but is not
		 * exposed in the UI. */
		r.rate = bytes_per_sec > UINT32_MAX ? UINT32_MAX
		                                    : (uint32_t)bytes_per_sec;
		if (nla_put(msg, TCA_NETEM_RATE, sizeof(r), &r) < 0)
			goto fail_msg;
	}

	nla_nest_end(msg, opts);

	err = nl_send_sync(sock, msg);
	msg = NULL; /* nl_send_sync frees on both success and failure */

	if (err < 0) {
		if (err == -NLE_PERM) {
			syslog(LOG_WARNING,
			       "Unable to add qdisc: permission denied. "
			       "Network impairment features require CAP_NET_ADMIN.");
		} else {
			syslog(LOG_ERR, "Unable to add qdisc: %s\n",
			       nl_geterror(err));
		}
		pthread_mutex_unlock(&nl_sock_mutex);
		return err;
	}

	if ((err = nl_cache_refill(sock, link_cache)) < 0) {
		syslog(LOG_ERR, "Unable to resync link cache: %s\n",
		       nl_geterror(err));
		pthread_mutex_unlock(&nl_sock_mutex);
		return -1;
	}

	pthread_mutex_unlock(&nl_sock_mutex);
	return 0;

fail_msg:
	nlmsg_free(msg);
	pthread_mutex_unlock(&nl_sock_mutex);
	return -1;
}

/* ---- netlink link-change monitor ---------------------------------------- */

static struct nl_sock *monitor_sock;
static pthread_t monitor_thread_id;
static atomic_int monitor_running;
static void (*link_change_cb)(void);

/* Trailing-edge debounce: after a link event, fire the callback once the
 * kernel has gone quiet for MONITOR_DEBOUNCE_MS. Bringing up e.g. a bond
 * produces a burst of attribute-change events; coalescing them means clients
 * see the final settled state instead of every intermediate one. */
#define MONITOR_DEBOUNCE_MS 250

static atomic_int pending_change;
static struct timespec last_event;  /* accessed only on monitor thread */

static long ts_to_ms(struct timespec t)
{
	return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static int monitor_msg_cb(struct nl_msg *msg, void *arg)
{
	struct nlmsghdr *hdr = nlmsg_hdr(msg);

	(void)arg;

	if (hdr->nlmsg_type != RTM_NEWLINK && hdr->nlmsg_type != RTM_DELLINK)
		return NL_OK;

	clock_gettime(CLOCK_MONOTONIC, &last_event);
	atomic_store(&pending_change, 1);
	return NL_OK;
}

static void flush_pending_change(void)
{
	pthread_mutex_lock(&nl_sock_mutex);
	if (nl_cache_refill(sock, link_cache) < 0) {
		syslog(LOG_WARNING, "monitor: link cache refill failed");
	}
	pthread_mutex_unlock(&nl_sock_mutex);

	atomic_store(&pending_change, 0);
	if (link_change_cb)
		link_change_cb();
}

static void *monitor_run(void *arg)
{
	(void)arg;
	pthread_setname_np(pthread_self(), "nl-link-monitor");

	int fd = nl_socket_get_fd(monitor_sock);

	while (atomic_load(&monitor_running)) {
		/* When idle, wake periodically so monitor_running can be
		 * observed without needing a separate shutdown signal. */
		int timeout = atomic_load(&pending_change)
		                  ? MONITOR_DEBOUNCE_MS
		                  : 5000;
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		int rc = poll(&pfd, 1, timeout);

		if (rc < 0) {
			if (errno == EINTR)
				continue;
			syslog(LOG_WARNING, "netlink monitor poll: %s",
			       strerror(errno));
			struct timespec backoff = { .tv_sec = 1 };
			nanosleep(&backoff, NULL);
			continue;
		}

		if (rc > 0 && (pfd.revents & POLLIN)) {
			int err = nl_recvmsgs_default(monitor_sock);
			if (err < 0 && err != -NLE_AGAIN) {
				syslog(LOG_WARNING, "netlink monitor: %s",
				       nl_geterror(err));
			}
		}

		if (atomic_load(&pending_change)) {
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			if (ts_to_ms(ts_absdiff(now, last_event))
			    >= MONITOR_DEBOUNCE_MS) {
				flush_pending_change();
			}
		}
	}
	return NULL;
}

int netem_monitor_start(void (*cb)(void))
{
	int rc;

	if (atomic_load(&monitor_running)) {
		syslog(LOG_WARNING, "netem monitor already running");
		return -1;
	}

	link_change_cb = cb;

	monitor_sock = nl_socket_alloc();
	if (!monitor_sock) {
		syslog(LOG_ERR, "netem monitor: nl_socket_alloc failed");
		return -1;
	}

	/* Multicast events do not use sequence numbers. */
	nl_socket_disable_seq_check(monitor_sock);
	nl_socket_modify_cb(monitor_sock, NL_CB_VALID, NL_CB_CUSTOM,
	                    monitor_msg_cb, NULL);

	if (nl_connect(monitor_sock, NETLINK_ROUTE) < 0) {
		syslog(LOG_ERR, "netem monitor: nl_connect failed");
		goto fail;
	}

	if (nl_socket_add_memberships(monitor_sock, RTNLGRP_LINK, 0) < 0) {
		syslog(LOG_ERR,
		       "netem monitor: failed to join RTNLGRP_LINK group");
		goto fail;
	}

	/* Non-blocking lets the poll(2) loop drive timing. */
	if (nl_socket_set_nonblocking(monitor_sock) < 0) {
		syslog(LOG_ERR, "netem monitor: set_nonblocking failed");
		goto fail;
	}

	atomic_store(&monitor_running, 1);
	rc = pthread_create(&monitor_thread_id, NULL, monitor_run, NULL);
	if (rc != 0) {
		syslog(LOG_ERR, "netem monitor: pthread_create failed");
		atomic_store(&monitor_running, 0);
		goto fail;
	}

	syslog(LOG_INFO, "netem monitor: watching RTNLGRP_LINK");
	return 0;

fail:
	nl_socket_free(monitor_sock);
	monitor_sock = NULL;
	return -1;
}

void netem_monitor_stop(void)
{
	if (!atomic_load(&monitor_running))
		return;

	atomic_store(&monitor_running, 0);
	pthread_join(monitor_thread_id, NULL);
	nl_socket_free(monitor_sock);
	monitor_sock = NULL;
}
