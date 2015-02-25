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

static pthread_mutex_t g_iface_mutex;
char *g_iface;

struct iface_stats g_stats_o;
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

/* update g_iface and reset the old stats. */
void stats_monitor_iface(const char *_iface)
{
	pthread_mutex_lock(&g_iface_mutex);
	g_stats_o.rx_bytes = 0;
	g_stats_o.tx_bytes = 0;
	g_stats_o.rx_packets = 0;
	g_stats_o.tx_packets = 0;
	if (g_iface) {
		free(g_iface);
	}
	g_iface = strdup(_iface);
	sprintf(g_stats_o.iface, "%s", g_iface);
	printf("monitoring iface: [%s]\n", g_iface);
	pthread_mutex_unlock(&g_iface_mutex);
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

static int read_counters(const char *iface, struct iface_stats *stats)
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

	if (!stats) {
		/* link cache is now warm; values don't matter */
		rtnl_link_put(link);
		return 0;
	}

	/* read and return counter */
	stats->rx_bytes = rtnl_link_get_stat(link, RTNL_LINK_RX_BYTES);
	stats->tx_bytes = rtnl_link_get_stat(link, RTNL_LINK_TX_BYTES);
	stats->rx_packets = rtnl_link_get_stat(link, RTNL_LINK_RX_PACKETS);
	stats->rx_packets += rtnl_link_get_stat(link, RTNL_LINK_RX_COMPRESSED);
	stats->tx_packets = rtnl_link_get_stat(link, RTNL_LINK_TX_PACKETS);
	stats->tx_packets += rtnl_link_get_stat(link, RTNL_LINK_TX_COMPRESSED);
	sprintf(stats->iface, "%s", iface);
	rtnl_link_put(link);
	return 0;
}

static void calc_deltas(struct iface_stats *stats_o,
			struct iface_stats *stats_c)
{
	if (0 == stats_o->rx_bytes) {
		stats_o->rx_bytes = stats_c->rx_bytes;
		stats_o->tx_bytes = stats_c->tx_bytes;
		stats_o->rx_packets = stats_c->rx_packets;
		stats_o->tx_packets = stats_c->tx_packets;
	}

	stats_c->rx_bytes_delta = stats_c->rx_bytes - stats_o->rx_bytes;
	stats_c->tx_bytes_delta = stats_c->tx_bytes - stats_o->tx_bytes;
	stats_c->rx_packets_delta = stats_c->rx_packets - stats_o->rx_packets;
	stats_c->tx_packets_delta = stats_c->tx_packets - stats_o->tx_packets;
}

static void update_stats()
{
	char *iface;
	struct iface_stats stats_o;
	struct iface_stats stats_c;

	pthread_mutex_lock(&g_iface_mutex);
	iface = strdup(g_iface);
	memcpy(&stats_o, &g_stats_o, sizeof(struct iface_stats));
	pthread_mutex_unlock(&g_iface_mutex);

	if (0 == read_counters(iface, &stats_c)) {
		clock_gettime(CLOCK_MONOTONIC, &stats_c.timestamp);
		calc_deltas(&stats_o, &stats_c);
		stats_handler(&stats_c);
	}
	memcpy(&g_stats_o, &stats_c, sizeof(struct iface_stats));
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
	(void)data; /* unused parameter. silence warning. */
	init_nl();
	read_counters("lo", NULL); /* warm up the link cache */
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
