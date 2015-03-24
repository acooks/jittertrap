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
#include <netlink/utils.h>
#include <netlink/route/link.h>

#include "jittertrap.h"
#include "stats_thread.h"
#include "sample_buf.h"

/* 10ms of samples per message */
static_assert((SAMPLES_PER_FRAME * SAMPLE_PERIOD_US) == 10000,
	      "Message must contain exactly 10ms of data samples");

/* globals */
static pthread_t stats_thread;
struct sigaction sa;

struct nl_sock *nl_sock;

static pthread_mutex_t g_iface_mutex;
static pthread_mutex_t g_stats_mutex;
char *g_iface;

struct sample g_stats_o;
int sample_period_us;

void (*stats_handler) (struct iface_stats * counts);

/* local prototypes */
static void *run(void *data);

int get_sample_period()
{
	return sample_period_us;
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
	pthread_mutex_lock(&g_stats_mutex);
	g_stats_o.rx_bytes = 0;
	g_stats_o.tx_bytes = 0;
	g_stats_o.rx_packets = 0;
	g_stats_o.tx_packets = 0;
	pthread_mutex_unlock(&g_stats_mutex);
	if (g_iface) {
		free(g_iface);
	}
	g_iface = strdup(_iface);
	printf("monitoring iface: [%s]\n", g_iface);
	pthread_mutex_unlock(&g_iface_mutex);
}

static int init_nl(void)
{
	/* Allocate and initialize a new netlink handle */
	if (!(nl_sock = nl_socket_alloc())) {
		fprintf(stderr, "Failed to alloc netlink socket\n");
		return -EOPNOTSUPP;
	}

	/* Bind and connect socket to protocol, NETLINK_ROUTE in our case. */
	if (nl_connect(nl_sock, NETLINK_ROUTE) < 0) {
		fprintf(stderr, "Failed to connect to kernel\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int read_counters(const char *iface, struct sample *stats)
{
	struct rtnl_link *link;
	assert(nl_sock);

	pthread_mutex_lock(&netlink_cache_mutex);

	/* iface index zero means use the iface name */
	if (rtnl_link_get_kernel(nl_sock, 0, iface, &link) < 0) {
		fprintf(stderr, "unknown interface/link name: %s\n", iface);
		pthread_mutex_unlock(&netlink_cache_mutex);
		return -1;
	}

	/* read and return counter */
	stats->rx_bytes = rtnl_link_get_stat(link, RTNL_LINK_RX_BYTES);
	stats->tx_bytes = rtnl_link_get_stat(link, RTNL_LINK_TX_BYTES);
	stats->rx_packets = rtnl_link_get_stat(link, RTNL_LINK_RX_PACKETS);
	stats->rx_packets += rtnl_link_get_stat(link, RTNL_LINK_RX_COMPRESSED);
	stats->tx_packets = rtnl_link_get_stat(link, RTNL_LINK_TX_PACKETS);
	stats->tx_packets += rtnl_link_get_stat(link, RTNL_LINK_TX_COMPRESSED);
	rtnl_link_put(link);
	pthread_mutex_unlock(&netlink_cache_mutex);
	return 0;
}

static void calc_deltas(struct sample *stats_o,
			struct sample *stats_c)
{
	if (0 == stats_o->rx_bytes || stats_o->rx_bytes > stats_c->rx_bytes) {
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

static void update_stats(struct sample *sample_c, char *iface)
{
	struct sample sample_o;
	pthread_mutex_lock(&g_stats_mutex);
	/* FIXME: this smells funny */
	memcpy(&sample_o, &g_stats_o, sizeof(struct sample));

	if (0 == read_counters(iface, sample_c)) {
		clock_gettime(CLOCK_MONOTONIC, &(sample_c->timestamp));
		calc_deltas(&sample_o, sample_c);
	}
	memcpy(&g_stats_o, sample_c, sizeof(struct sample));
	pthread_mutex_unlock(&g_stats_mutex);
}

static int init_realtime(void)
{
	struct sched_param schedparm;
	memset(&schedparm, 0, sizeof(schedparm));
	schedparm.sched_priority = 1;	// lowest rt priority
	return sched_setscheduler(0, SCHED_FIFO, &schedparm);
}

/* microseconds */
void set_sample_period(int period)
{
	if (period < 100)
		period = 100;
	sample_period_us = period;
}

static void *run(void *data)
{
	(void)data; /* unused parameter. silence warning. */
	init_nl();
	raw_sample_buf_init();
	init_realtime();
	set_sample_period(SAMPLE_PERIOD_US);

	struct timespec deadline;
	struct timespec whoosh; /* the sound of a missed deadline. */
	clock_gettime(CLOCK_MONOTONIC, &deadline);

	int sample_no = 0;
	char *iface = strdup(g_iface);
	struct iface_stats *stats_frame = raw_sample_buf_produce_next();
	snprintf(stats_frame->iface, MAX_IFACE_LEN, "%s", iface);
	stats_frame->sample_period_us = sample_period_us;

	for (;;) {
		clock_gettime(CLOCK_MONOTONIC, &whoosh);
#if 0
/* TODO: put the whoosh in the sample */
		printf("%ld.%09ld : %ld.%09ld\n",
			deadline.tv_sec,
			deadline.tv_nsec,
			whoosh.tv_sec - deadline.tv_sec,
			whoosh.tv_nsec - deadline.tv_nsec);
#endif
		deadline.tv_nsec += 1000 * sample_period_us;

		/* Normalize the time to account for the second boundary */
		if(deadline.tv_nsec >= 1000000000) {
			deadline.tv_nsec -= 1000000000;
			deadline.tv_sec++;
		}

		update_stats(&(stats_frame->samples[sample_no]), iface);

		sample_no++;
		sample_no %= SAMPLES_PER_FRAME;

		/* set the iface, samples per period at start of each frame*/
		if (sample_no == 0) {
			pthread_mutex_lock(&g_iface_mutex);
			free(iface);
			iface = strdup(g_iface);

			stats_frame = raw_sample_buf_produce_next();
			snprintf(stats_frame->iface, MAX_IFACE_LEN, "%s", iface);
			pthread_mutex_unlock(&g_iface_mutex);
			stats_frame->sample_period_us = sample_period_us;
			stats_handler(raw_sample_buf_consume_next());
		}

	        clock_nanosleep(CLOCK_MONOTONIC,
				TIMER_ABSTIME, &deadline, NULL);
	}

	return NULL;
}
