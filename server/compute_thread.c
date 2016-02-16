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
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>   /* for types like uint64_t */
#include <inttypes.h> /* for printf macros like PRId64 */
#include <math.h>     /* for ceill */

#include "jittertrap.h"
#include "iface_stats.h"
#include "sample_buf.h"
#include "timeywimey.h"
#include "compute_thread.h"
#include "sampling_thread.h"

#include "mq_msg_stats.h"

static pthread_mutex_t unsent_frame_count_mutex;

static pthread_t compute_thread;
struct iface_stats *g_raw_samples;
int g_unsent_frame_count = 0;
char g_selected_iface[MAX_IFACE_LEN];

/* local prototypes */
static void *run(void *data);

inline static void calc_whoosh_error(struct iface_stats *stats)
{
	long double variance;
	double max = 0;
	uint64_t sum = 0;
	uint64_t sum_squares = 0;

	for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
		struct sample s = stats->samples[i];
		max = (s.whoosh_error_ns > max) ? s.whoosh_error_ns : max;
		sum += s.whoosh_error_ns;
		sum_squares += (s.whoosh_error_ns * s.whoosh_error_ns);
	}
	stats->whoosh_err_max = max;
	stats->whoosh_err_mean = sum / SAMPLES_PER_FRAME;
	variance = (long double)sum_squares / (double)SAMPLES_PER_FRAME;
	stats->whoosh_err_sd = (uint64_t)ceill(sqrtl(variance));

	if ((max >= 500 * SAMPLE_PERIOD_US) ||
	    (stats->whoosh_err_sd >= 200 * SAMPLE_PERIOD_US)) {
		fprintf(stderr, "sampling jitter! mean: %10" PRId64
		                " max: %10" PRId64 " sd: %10" PRId64 "\n",
		        stats->whoosh_err_mean, stats->whoosh_err_max,
		        stats->whoosh_err_sd);
	}
}

inline static void stats_filter(struct iface_stats *ifs, struct mq_stats_msg *m)
{
	uint64_t rxb_sum = 0, txb_sum = 0, rxp_sum = 0, txp_sum = 0;
	uint64_t rxb_max = 0, txb_max = 0;
	uint32_t rxp_max = 0, txp_max = 0;
	uint64_t rxb_min = UINT64_MAX, txb_min = UINT64_MAX;
	uint32_t rxp_min = UINT32_MAX, txp_min = UINT32_MAX;
	uint32_t whoosh_sum = 0, whoosh_max = 0, whoosh_sum2 = 0;

	//calc_whoosh_error(ifs);

	for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
		struct sample *s = &(ifs->samples[i]);
		rxb_sum += s->rx_bytes_delta;
		txb_sum += s->tx_bytes_delta;
		rxp_sum += s->rx_packets_delta;
		txp_sum += s->tx_packets_delta;
		whoosh_sum += s->whoosh_error_ns;

		rxb_max = (rxb_max > s->rx_bytes_delta) ? rxb_max : s->rx_bytes_delta;
		txb_max = (txb_max > s->tx_bytes_delta) ? txb_max : s->tx_bytes_delta;
		rxp_max = (rxp_max > s->rx_packets_delta) ? rxp_max : s->rx_packets_delta;
		txp_max = (txp_max > s->tx_packets_delta) ? txp_max : s->tx_packets_delta;
		whoosh_max = (whoosh_max > s->whoosh_error_ns) ? whoosh_max : s->whoosh_error_ns;
		rxb_min = (rxb_min < s->rx_bytes_delta) ? rxb_min : s->rx_bytes_delta;
		txb_min = (txb_min < s->tx_bytes_delta) ? txb_min : s->tx_bytes_delta;
		rxp_min = (rxp_min < s->rx_packets_delta) ? rxp_min : s->rx_packets_delta;
		txp_min = (txp_min < s->tx_packets_delta) ? txp_min : s->tx_packets_delta;

	}
	m->min_rx_bytes = rxb_min;
	m->max_rx_bytes = rxb_max;
	m->mean_rx_bytes = rxb_sum / SAMPLES_PER_FRAME;

	m->min_tx_bytes = txb_min;
	m->max_tx_bytes = txb_max;
	m->mean_tx_bytes = txb_sum / SAMPLES_PER_FRAME;

	m->min_rx_packets = rxp_min;
	m->max_rx_packets = rxp_max;
	m->mean_rx_packets = rxp_sum / SAMPLES_PER_FRAME;

	m->min_tx_packets = txp_min;
	m->max_tx_packets = txp_max;
	m->mean_tx_packets = txp_sum / SAMPLES_PER_FRAME;

	m->max_whoosh = whoosh_max;
	m->mean_whoosh = whoosh_sum / SAMPLES_PER_FRAME;

	sprintf(m->iface, "%s", ifs->iface);
	m->interval_ns = 1E7;
}

inline static int message_producer(struct mq_stats_msg *m, void *data)
{
	struct iface_stats *ifs = (struct iface_stats *)data;
	stats_filter(ifs, m);
	return 0;
}

void stats_filter_and_write()
{
	int cb_err, err = 0;

	pthread_mutex_lock(&unsent_frame_count_mutex);
	if (g_unsent_frame_count > 0) {
		err =
		    mq_stats_produce(message_producer, g_raw_samples, &cb_err);
		if (!err) {
			g_unsent_frame_count--;
		}
	}
	pthread_mutex_unlock(&unsent_frame_count_mutex);
}

/* callback for the sampling thread. */
void sample_thread_event_handler(struct iface_stats *raw_samples)
{
	pthread_mutex_lock(&unsent_frame_count_mutex);
	g_raw_samples = raw_samples;
	g_unsent_frame_count++;
	pthread_mutex_unlock(&unsent_frame_count_mutex);
}

int compute_thread_init(void)
{
	int err;
	assert(!compute_thread);
	err = pthread_create(&compute_thread, NULL, run, NULL);
	assert(!err);
	pthread_setname_np(compute_thread, "jt-compute");

	sample_thread_init(sample_thread_event_handler);

	return 0;
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
	schedparm.sched_priority = 1; // lowest rt priority
	sched_setscheduler(0, SCHED_FIFO, &schedparm);
	set_affinity();
	return 0;
}

static void *run(void *data)
{
	(void)data; /* unused parameter. silence warning. */
	init_realtime();
	struct timespec deadline;

	clock_gettime(CLOCK_MONOTONIC, &deadline);

	for (;;) {
		stats_filter_and_write();

		deadline.tv_nsec += 1E6;

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
