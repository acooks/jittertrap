#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/cache.h>
#include <netlink/route/link.h>
#include <netlink/route/qdisc.h>
#include <netlink/route/qdisc/netem.h>

#include "netem.h"
#include "err.h"

static struct nl_sock *sock;
static struct nl_cache *link_cache, *qdisc_cache;

int netem_init()
{
	int err;

	/* Allocate and initialize a new netlink handle */
	if (!(sock = nl_socket_alloc())) {
		fprintf(stderr, "Failed to alloc netlink socket\n");
		return -EOPNOTSUPP;
	}

	/* Bind and connect socket to protocol, NETLINK_ROUTE in our case. */
	if ((err = nl_connect(sock, NETLINK_ROUTE)) < 0) {
		fprintf(stderr, "Failed to connect to kernel\n");
		return -EOPNOTSUPP;
	}

	/* Retrieve a list of all available interfaces and populate cache. */
	if ((err = rtnl_link_alloc_cache(sock, AF_UNSPEC, &link_cache)) < 0) {
		fprintf(stderr, "Error creating link cache\n");
		return -EOPNOTSUPP;
	}

	/* Retrieve a list of all available qdiscs and populate cache. */
	if ((err = rtnl_qdisc_alloc_cache(sock, &qdisc_cache)) < 0) {
		fprintf(stderr, "Error creating qdisc cache\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

char **netem_list_ifaces()
{
	struct rtnl_link *link;
	char **ifaces;
	char **i;
	int count = nl_cache_nitems(link_cache);
	int size = (count + 1) * sizeof(char *);

	if ( (ifaces = malloc(size)) == NULL)
		err_sys("malloc");
	i = ifaces;
	link = (struct rtnl_link *)nl_cache_get_first(link_cache);
	while (link) {
		char *j = rtnl_link_get_name(link);
		if (strcmp("lo", j) != 0) {
			if ( (*i = malloc(strlen(j) + 1)) == NULL)
				err_sys("malloc");
			sprintf(*i, j);
			i++;
			//  rtnl_link_put(link); // FIXME: Yes? No?
		}
		link =
		    (struct rtnl_link *)nl_cache_get_next((struct nl_object *)
							  link);
	}
	*i = 0;

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
void cb_found_cache_obj(struct nl_object *found, void *p) {
	struct nl_object **out = (struct nl_object**)p;
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
__attribute__((weak))struct nl_object *nl_cache_find(struct nl_cache *cache,
						     struct nl_object *filter)
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

	if ((err = nl_cache_refill(sock, link_cache)) < 0) {
		fprintf(stderr, "Unable to resync link cache: %s\n",
			nl_geterror(err));
		goto cleanup;
	}

	if ((err = nl_cache_refill(sock, qdisc_cache)) < 0) {
		fprintf(stderr, "Unable to resync link cache: %s\n",
			nl_geterror(err));
		goto cleanup;
	}

	/* filter link by name */
	if ((link = rtnl_link_get_by_name(link_cache, iface)) == NULL) {
		fprintf(stderr, "unknown interface/link name.\n");
		goto cleanup;
	}

	if (!(filter_qdisc = rtnl_qdisc_alloc())) {
		/* OOM error */
		fprintf(stderr, "couldn't alloc qdisc\n");
		goto cleanup_link;
	}

	rtnl_tc_set_link(TC_CAST(filter_qdisc), link);
	rtnl_tc_set_parent(TC_CAST(filter_qdisc), TC_H_ROOT);
	rtnl_tc_set_kind(TC_CAST(filter_qdisc), "netem");

	found_qdisc = (struct rtnl_qdisc *)
			nl_cache_find(qdisc_cache, OBJ_CAST(filter_qdisc));
	if (!found_qdisc) {
		fprintf(stderr, "could't find netem qdisc for iface: %s\n",
			iface);
		goto cleanup_filter_qdisc;
	}

	params->iface = iface;
	if (0 > (delay = rtnl_netem_get_delay(found_qdisc))) {
		fprintf(stderr, "couldn't get delay for iface: %s\n", iface);
		goto cleanup_qdisc;
	}
	params->delay = (double)delay / 1000;

	if (0 > (jitter = rtnl_netem_get_jitter(found_qdisc))) {
		fprintf(stderr, "couldn't get jitter for iface: %s\n", iface);
		goto cleanup_qdisc;
	}
	params->jitter = (double)jitter / 1000;

	if (0 > (loss = rtnl_netem_get_loss(found_qdisc))) {
		fprintf(stderr, "couldn't get loss for iface: %s\n", iface);
		goto cleanup_qdisc;
	}
	params->loss = (int)(loss / (UINT_MAX / 100));

	rtnl_qdisc_put(found_qdisc);
	rtnl_qdisc_put(filter_qdisc);
	rtnl_link_put(link);
	return 0;

cleanup_qdisc:
        rtnl_qdisc_put(found_qdisc);
cleanup_filter_qdisc:
	rtnl_qdisc_put(filter_qdisc);
cleanup_link:
	rtnl_link_put(link);
cleanup:
	return -1;
}

int netem_set_params(const char *iface, struct netem_params *params)
{
	struct rtnl_link *link;
	struct rtnl_qdisc *qdisc;
	int err;

	/* filter link by name */
	if ((link = rtnl_link_get_by_name(link_cache, iface)) == NULL) {
		fprintf(stderr, "unknown interface/link name.\n");
		return -1;
	}

	if (!(qdisc = rtnl_qdisc_alloc())) {
		/* OOM error */
		fprintf(stderr, "couldn't alloc qdisc\n");
		return -1;
	}

	rtnl_tc_set_link(TC_CAST(qdisc), link);
	rtnl_tc_set_parent(TC_CAST(qdisc), TC_H_ROOT);
	rtnl_tc_set_kind(TC_CAST(qdisc), "netem");

	rtnl_netem_set_delay(qdisc, params->delay * 1000); /* expects microseconds */
	rtnl_netem_set_jitter(qdisc, params->jitter * 1000);
	rtnl_netem_set_loss(qdisc, (params->loss * (UINT_MAX / 100)));

	/* Submit request to kernel and wait for response */
	err = rtnl_qdisc_add(sock, qdisc, NLM_F_CREATE | NLM_F_REPLACE);

	/* Return the qdisc object to free memory resources */
	rtnl_qdisc_put(qdisc);

	if (err < 0) {
		fprintf(stderr, "Unable to add qdisc: %s\n", nl_geterror(err));
		return err;
	}

	if ((err = nl_cache_refill(sock, link_cache)) < 0) {
		fprintf(stderr, "Unable to resync link cache: %s\n",
			nl_geterror(err));
		return -1;
	}

	return 0;
}
