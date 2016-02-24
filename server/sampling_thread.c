#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

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
#include "iface_stats.h"
#include "sampling_thread.h"
#include "sample_buf.h"
#include "timeywimey.h"

/* globals */
static pthread_t sampling_thread;
struct sigaction sa;

struct nl_sock *nl_sock;

static pthread_mutex_t g_iface_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;
char *g_iface;

struct sample g_stats_o;

/* FIXME: Treat with extreme prejudice!
 * There's some kind of bug (maybe in libnl?) where the rtnl_link_get_stat()
 * will return stale data after a rtnl_link_get_kernel() call that changes
 * the iface (and therefore also the link object).
 *
 * This hack effectively ignores the values of the first 50 results from
 * rtnl_link_get_kernel();
 */
#define DISCARD_FIRST_N_READINGS 50
int reset_stats = DISCARD_FIRST_N_READINGS;

int sample_period_us;

void (*stats_handler)(struct iface_stats *counts);

/* local prototypes */
static void *run(void *data);

int get_sample_period()
{
	return sample_period_us;
}

int sample_thread_init(void (*_stats_handler)(struct iface_stats *counts))
{
	int err;

	if (!g_iface || !_stats_handler) {
		return -1;
	}
	stats_handler = _stats_handler;

	if (!sampling_thread) {
		err = pthread_create(&sampling_thread, NULL, run, NULL);
		assert(!err);
		pthread_setname_np(sampling_thread, "jt-sample");
	}
	return 0;
}

/* update g_iface and reset the old stats. */
void sample_iface(const char *_iface)
{
	pthread_mutex_lock(&g_iface_mutex);
	pthread_mutex_lock(&g_stats_mutex);
	g_stats_o.rx_bytes = 0;
	g_stats_o.tx_bytes = 0;
	g_stats_o.rx_packets = 0;
	g_stats_o.tx_packets = 0;
	reset_stats = DISCARD_FIRST_N_READINGS;
	pthread_mutex_unlock(&g_stats_mutex);
	if (g_iface) {
		free(g_iface);
	}
	g_iface = strdup(_iface);
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

	pthread_mutex_lock(&nl_sock_mutex);

	/* iface index zero means use the iface name */
	if (rtnl_link_get_kernel(nl_sock, 0, iface, &link) < 0) {
		fprintf(stderr, "unknown interface/link name: %s\n", iface);
		pthread_mutex_unlock(&nl_sock_mutex);
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
	pthread_mutex_unlock(&nl_sock_mutex);
	return 0;
}

static void calc_deltas(struct sample *stats_o, struct sample *stats_c)
{
	if (reset_stats-- > 0 || stats_o->rx_bytes > stats_c->rx_bytes) {
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

static void
update_stats(struct sample *sample_c, char *iface, struct timespec deadline)
{
	struct sample sample_o;
	struct timespec whoosh_err; /* the sound of a missed deadline. */
	pthread_mutex_lock(&g_stats_mutex);
	/* FIXME: this smells funny */
	memcpy(&sample_o, &g_stats_o, sizeof(struct sample));

	if (0 == read_counters(iface, sample_c)) {
		clock_gettime(CLOCK_MONOTONIC, &(sample_c->timestamp));
		whoosh_err = ts_absdiff(sample_c->timestamp, deadline);
		sample_c->whoosh_error_ns = whoosh_err.tv_nsec;
		calc_deltas(&sample_o, sample_c);
	}
	memcpy(&g_stats_o, sample_c, sizeof(struct sample));
	pthread_mutex_unlock(&g_stats_mutex);
}

#define handle_error_en(en, msg)                                               \
	do {                                                                   \
		errno = en;                                                    \
		perror(msg);                                                   \
		exit(EXIT_FAILURE);                                            \
	} while (0)

static void set_affinity()
{
	int s, j;
	cpu_set_t cpuset;
	pthread_t thread;
	thread = pthread_self();
	/* Set affinity mask to include CPUs 1 only */
	CPU_ZERO(&cpuset);
	CPU_SET(RT_CPU, &cpuset);
	s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (s != 0) {
		handle_error_en(s, "pthread_setaffinity_np");
	}

	/* Check the actual affinity mask assigned to the thread */
	s = pthread_getaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (s != 0) {
		handle_error_en(s, "pthread_getaffinity_np");
	}

	printf("RT thread CPU affinity: ");
	for (j = 0; j < CPU_SETSIZE; j++) {
		if (CPU_ISSET(j, &cpuset)) {
			printf(" CPU%d", j);
		}
	}
	printf("\n");
}

static int init_realtime(void)
{
	struct sched_param schedparm;
	memset(&schedparm, 0, sizeof(schedparm));
	schedparm.sched_priority = 2;
	sched_setscheduler(0, SCHED_FIFO, &schedparm);
	set_affinity();
	return 0;
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
	clock_gettime(CLOCK_MONOTONIC, &deadline);

	int sample_no = 0;
	char *iface = strdup(g_iface);
	struct iface_stats *stats_frame = raw_sample_buf_produce_next();
	snprintf(stats_frame->iface, MAX_IFACE_LEN, "%s", iface);
	stats_frame->sample_period_us = sample_period_us;

	for (;;) {
		update_stats(&(stats_frame->samples[sample_no]), iface,
		             deadline);

		sample_no++;
		sample_no %= SAMPLES_PER_FRAME;

		/* set the iface, samples per period at start of each frame*/
		if (sample_no == 0) {
			pthread_mutex_lock(&g_iface_mutex);
			free(iface);
			iface = strdup(g_iface);

			stats_frame = raw_sample_buf_produce_next();
			snprintf(stats_frame->iface, MAX_IFACE_LEN, "%s",
			         iface);
			pthread_mutex_unlock(&g_iface_mutex);
			stats_frame->sample_period_us = sample_period_us;
			stats_handler(raw_sample_buf_consume_next());
		}

		deadline.tv_nsec += 1000 * sample_period_us;

		/* Normalize the time to account for the second boundary */
		if (deadline.tv_nsec >= 1000000000) {
			deadline.tv_nsec -= 1000000000;
			deadline.tv_sec++;
		}

		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline,
		                NULL);
	}

	return NULL;
}
