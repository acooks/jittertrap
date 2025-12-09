#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <syslog.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/cache.h>
#include <netlink/route/link.h>
#include <netlink/route/qdisc.h>
#include <netlink/route/qdisc/netem.h>

#include "jittertrap.h"
#include "netem.h"

static struct nl_sock *sock;
static struct nl_cache *link_cache, *qdisc_cache;

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

		if ((strcmp("lo", j) != 0) && is_iface_allowed(j)) {
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

int netem_get_params(char *iface, struct netem_params *params)
{
	struct rtnl_link *link;
	struct rtnl_qdisc *filter_qdisc;
	struct rtnl_qdisc *found_qdisc = NULL;
	int err;
	int delay, jitter, loss;

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

int netem_set_params(const char *iface, struct netem_params *params)
{
	struct rtnl_link *link;
	struct rtnl_qdisc *qdisc;
	int err;

	pthread_mutex_lock(&nl_sock_mutex);

	/* filter link by name */
	if ((link = rtnl_link_get_by_name(link_cache, iface)) == NULL) {
		syslog(LOG_ERR, "unknown interface/link name.\n");
		pthread_mutex_unlock(&nl_sock_mutex);
		return -1;
	}

	if (!(qdisc = rtnl_qdisc_alloc())) {
		/* OOM error */
		syslog(LOG_ERR, "couldn't alloc qdisc\n");
		pthread_mutex_unlock(&nl_sock_mutex);
		return -1;
	}

	rtnl_tc_set_link(TC_CAST(qdisc), link);
	rtnl_tc_set_parent(TC_CAST(qdisc), TC_H_ROOT);
	rtnl_tc_set_kind(TC_CAST(qdisc), "netem");

	rtnl_netem_set_delay(qdisc,
	                     params->delay * 1000); /* expects microseconds */
	rtnl_netem_set_jitter(qdisc, params->jitter * 1000);
	/* params->loss is given in 10ths of a percent */
	rtnl_netem_set_loss(qdisc, (params->loss * (UINT_MAX / 1000)));

	/* Submit request to kernel and wait for response */
	err = rtnl_qdisc_add(sock, qdisc, NLM_F_CREATE | NLM_F_REPLACE);

	/* Return the qdisc object to free memory resources */
	rtnl_qdisc_put(qdisc);

	if (err < 0) {
		/*
		 * NLE_PERM (-10) or -EPERM (-1) indicates permission denied.
		 * This happens when running without CAP_NET_ADMIN.
		 */
		if (err == -EPERM || err == -10) {
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
}
