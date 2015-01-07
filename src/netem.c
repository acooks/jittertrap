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
	ifaces = malloc(size);
	i = ifaces;
	link = (struct rtnl_link *)nl_cache_get_first(link_cache);
	while (link) {
		char *j = rtnl_link_get_name(link);
		if (strcmp("lo", j) != 0) {
			*i = malloc(strlen(j) + 1);
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

	found_qdisc = nl_cache_find(qdisc_cache, OBJ_CAST(filter_qdisc));
	if (!found_qdisc) {
		fprintf(stderr, "could't find qdisc for iface: %s\n", iface);
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
	params->loss = (double)loss / (UINT_MAX / 100);

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

int netem_update(const char *iface, int delay, int jitter, int loss)
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

	rtnl_netem_set_delay(qdisc, delay * 1000);	/* expects microseconds */
	rtnl_netem_set_jitter(qdisc, jitter * 1000);
	rtnl_netem_set_loss(qdisc, (loss * (UINT_MAX / 100)));

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
