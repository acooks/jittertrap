#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <stdio.h>
#include <sched.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <linux/types.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/cache.h>
#include <netlink/utils.h>
#include <netlink/route/link.h>

#include "stats_thread.h"

/* globals */
static pthread_t stats_thread;
struct sigaction sa;

struct nl_sock *nl_sock;
struct nl_cache *nl_link_cache;

char *g_iface;
struct iface_stats stats_o;
struct iface_stats stats_c;
int sample_period_ms;

void (*stats_handler) (struct iface_stats * counts);

/* local prototypes */
static void *run(void *data);

int get_sample_period()
{
	return sample_period_ms;
}

int stats_thread_init(void (*_stats_handler) (struct iface_stats * counts))
{
	if (!g_iface || !_stats_handler) {
		return -1;
	}
	stats_handler = _stats_handler;

	if (!stats_thread) {
		return pthread_create(&stats_thread, NULL, run, NULL);
	}
	return 0;
}

void stats_monitor_iface(const char *_iface)
{
	if (g_iface) {
		free(g_iface);
	}
	g_iface = strdup(_iface);
	stats_o.rx_bytes = 0;
	stats_o.tx_bytes = 0;
	stats_o.rx_packets = 0;
	stats_o.tx_packets = 0;
}

static int init_nl(void)
{
	int err;

	/* Allocate and initialize a new netlink handle */
	if (!(nl_sock = nl_socket_alloc())) {
		fprintf(stderr, "Failed to alloc netlink socket\n");
		return -EOPNOTSUPP;
	}

	/* Bind and connect socket to protocol, NETLINK_ROUTE in our case. */
	if ((err = nl_connect(nl_sock, NETLINK_ROUTE)) < 0) {
		fprintf(stderr, "Failed to connect to kernel\n");
		return -EOPNOTSUPP;
	}

	/* Retrieve a list of all available interfaces and populate cache. */
	if ((err =
	     rtnl_link_alloc_cache(nl_sock, AF_UNSPEC, &nl_link_cache)) < 0) {
		fprintf(stderr, "Error creating link cache\n");
		return -EOPNOTSUPP;
	}
	return 0;
}

static int read_counters(const char *iface)
{
	int err;
	struct rtnl_link *link;
	assert(nl_sock);
	assert(nl_link_cache);

	if ((err = nl_cache_refill(nl_sock, nl_link_cache)) < 0) {
		fprintf(stderr, "Unable to resync link cache: %s\n",
			nl_geterror(err));
		return -1;
	}

	/* filter link by name */
	if ((link = rtnl_link_get_by_name(nl_link_cache, iface)) == NULL) {
		fprintf(stderr, "unknown interface/link name.\n");
		return -1;
	}

	/* read and return counter */
	stats_c.rx_bytes = rtnl_link_get_stat(link, RTNL_LINK_RX_BYTES);
	stats_c.tx_bytes = rtnl_link_get_stat(link, RTNL_LINK_TX_BYTES);
	stats_c.rx_packets = rtnl_link_get_stat(link, RTNL_LINK_RX_PACKETS);
	stats_c.rx_packets += rtnl_link_get_stat(link, RTNL_LINK_RX_COMPRESSED);
	stats_c.tx_packets = rtnl_link_get_stat(link, RTNL_LINK_TX_PACKETS);
	stats_c.tx_packets += rtnl_link_get_stat(link, RTNL_LINK_TX_COMPRESSED);
	rtnl_link_put(link);
	return 0;
}

static void calc_deltas()
{
	if (0 == stats_o.rx_bytes) {
		stats_o.rx_bytes = stats_c.rx_bytes;
		stats_o.tx_bytes = stats_c.tx_bytes;
		stats_o.rx_packets = stats_c.rx_packets;
		stats_o.tx_packets = stats_c.tx_packets;
	}

	stats_c.rx_bytes_delta = stats_c.rx_bytes - stats_o.rx_bytes;
	stats_c.tx_bytes_delta = stats_c.tx_bytes - stats_o.tx_bytes;
	stats_c.rx_packets_delta = stats_c.rx_packets - stats_o.rx_packets;
	stats_c.tx_packets_delta = stats_c.tx_packets - stats_o.tx_packets;
}

static void update_stats()
{
	struct timeval t;
	gettimeofday(&t, NULL);
	stats_c.timestamp = (t.tv_sec * 1000 * 1000 + t.tv_usec);
	if (0 == read_counters(g_iface)) {
		calc_deltas();
		stats_handler(&stats_c);
	}
	memcpy(&stats_o, &stats_c, sizeof(struct iface_stats));
}

static int init_realtime(void)
{
	struct sched_param schedparm;
	memset(&schedparm, 0, sizeof(schedparm));
	schedparm.sched_priority = 1;	// lowest rt priority
	return sched_setscheduler(0, SCHED_FIFO, &schedparm);
}

void set_sample_period(int period)
{
	if (period < 1)
		period = 1;
	sample_period_ms = period;
}

static void *run(void *data)
{
	init_nl();
	read_counters("lo"); /* warm up the link cache */
	init_realtime();
	set_sample_period(SAMPLE_PERIOD_MS);

	struct timespec deadline;
	struct timespec whoosh; /* the sound of a missed deadline. */
	clock_gettime(CLOCK_MONOTONIC, &deadline);

	for (;;) {
		clock_gettime(CLOCK_MONOTONIC, &whoosh);
#if 0
		printf("%ld.%09ld : %ld.%09ld\n",
			deadline.tv_sec,
			deadline.tv_nsec,
			whoosh.tv_sec - deadline.tv_sec,
			whoosh.tv_nsec - deadline.tv_nsec);
#endif
		deadline.tv_nsec += 1000 * 1000 * sample_period_ms;

		/* Normalize the time to account for the second boundary */
		if(deadline.tv_nsec >= 1000000000) {
			deadline.tv_nsec -= 1000000000;
			deadline.tv_sec++;
		}

		update_stats();
	        clock_nanosleep(CLOCK_MONOTONIC,
				TIMER_ABSTIME, &deadline, NULL);
	}

	return NULL;
}
