#define _POSIX_C_SOURCE 200809L

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
#include "slist.h"

static pthread_mutex_t unsent_frame_count_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_t compute_thread;
struct iface_stats *g_raw_samples;
int g_unsent_frame_count = 0;
char g_selected_iface[MAX_IFACE_LEN];

struct slist *sample_list;
int g_sample_count;

/* local prototypes */
static void *run(void *data);

#define MAX_LIST_LEN 1000
#define DECIMATIONS_COUNT 8
int decs[DECIMATIONS_COUNT] = { 5, 10, 20, 50, 100, 200, 500, 1000 };

/* TODO: check all integer divisions and consider using FP */

struct minmaxmean {
	uint32_t min;
	uint32_t max;
	uint32_t mean;
};

enum { RX = 0, TX = 1 };

inline static struct minmaxmean
calc_min_max_mean_gap(struct slist *list, int decim8, int rxtx)
{
	struct slist *ln;
	int32_t size = slist_size(list);
	int32_t gap_lengths[MAX_LIST_LEN] = { 0 };
	int32_t gap_idx = 0;
	int found_gap = 0;
	int32_t min_gap = decim8 + 1;
	int32_t max_gap = 0;
	int32_t sum_gap = 0;
	int32_t mean_gap = 0;

	assert(size >= decim8);
	assert(decim8 > 0);

	ln = slist_idx(list, size - decim8);
	for (int i = decim8; i > 0; i--) {
		struct sample *s = ln->s;
		if (((RX == rxtx) && (0 == s->rx_packets_delta)) ||
		    ((TX == rxtx) && (0 == s->tx_packets_delta))) {
			found_gap = 1;
			gap_lengths[gap_idx]++;
			max_gap = (max_gap > gap_lengths[gap_idx])
			              ? max_gap
			              : gap_lengths[gap_idx];
		} else if (found_gap) {
			found_gap = 0;
			gap_idx++;
		}
		ln = ln->next;
	}

	assert(gap_idx <= decim8);
	assert(max_gap <= decim8);

	for (int i = 0; i <= gap_idx; i++) {
		sum_gap += gap_lengths[i];
		if (min_gap > gap_lengths[i]) {
			min_gap = gap_lengths[i];
		}
	}
	assert(sum_gap <= decim8);
	assert((min_gap <= decim8) || (sum_gap == 0));

	/* gap is the last index into the gap_lengths, so +1 for count. */
	mean_gap = roundl((1000.0 * sum_gap) / (gap_idx + 1));
	assert(1000 * min_gap <= mean_gap);

	return (struct minmaxmean){min_gap, max_gap, mean_gap};
}

inline static int
calc_packet_gap(struct slist *list, struct mq_stats_msg *m, int decim8)
{
	struct minmaxmean rx, tx;
	rx = calc_min_max_mean_gap(list, decim8, RX);
	m->max_rx_packet_gap = rx.max;
	m->min_rx_packet_gap = rx.min;
	m->mean_rx_packet_gap = rx.mean;

	tx = calc_min_max_mean_gap(list, decim8, TX);
	m->max_tx_packet_gap = tx.max;
	m->min_tx_packet_gap = tx.min;
	m->mean_tx_packet_gap = tx.mean;

	return 0;
}

inline static int
calc_whoosh_err(struct slist *list, struct mq_stats_msg *m, int decim8)
{
	uint32_t whoosh_sum = 0, whoosh_max = 0, whoosh_sum2 = 0;
	struct slist *ln;
	int size = slist_size(list);

	ln = slist_idx(list, size - decim8);
	for (int i = decim8; i > 0; i--) {
		struct sample *s = ln->s;
		whoosh_sum += s->whoosh_error_ns;
		whoosh_sum2 += (s->whoosh_error_ns * s->whoosh_error_ns);
		whoosh_max = (whoosh_max > s->whoosh_error_ns)
		                 ? whoosh_max
		                 : s->whoosh_error_ns;
		ln = ln->next;
	}

	m->max_whoosh = whoosh_max;
	m->mean_whoosh = (uint64_t)ceill((double)whoosh_sum / (double)decim8);

	double variance = (long double)whoosh_sum2 / (double)decim8;
	m->sd_whoosh = (uint64_t)ceill(sqrtl(variance));

	if ((decim8 == decs[DECIMATIONS_COUNT - 1]) &&
	    ((whoosh_max >= 0.1 * m->interval_ns) ||
	     (m->sd_whoosh >= m->interval_ns))) {
		fprintf(stderr, "sampling jitter! mean: %10" PRId32
		                " max: %10" PRId32 " sd: %10" PRId32 "\n",
		        m->mean_whoosh, m->max_whoosh, m->sd_whoosh);
	}

	return 0;
}

inline static int
calc_txrx_minmaxmean(struct slist *list, struct mq_stats_msg *m, int decim8)
{
	uint64_t rxb_sum = 0, txb_sum = 0, rxp_sum = 0, txp_sum = 0;
	uint64_t rxb_max = 0, txb_max = 0;
	uint32_t rxp_max = 0, txp_max = 0;
	uint64_t rxb_min = UINT64_MAX, txb_min = UINT64_MAX;
	uint32_t rxp_min = UINT32_MAX, txp_min = UINT32_MAX;

	struct slist *ln;
	int size = slist_size(list);

	ln = slist_idx(list, size - decim8);
	for (int i = decim8; i > 0; i--) {
		struct sample *s = ln->s;
		rxb_sum += s->rx_bytes_delta;
		txb_sum += s->tx_bytes_delta;
		rxp_sum += s->rx_packets_delta;
		txp_sum += s->tx_packets_delta;

		rxb_max =
		    (rxb_max > s->rx_bytes_delta) ? rxb_max : s->rx_bytes_delta;
		txb_max =
		    (txb_max > s->tx_bytes_delta) ? txb_max : s->tx_bytes_delta;
		rxp_max = (rxp_max > s->rx_packets_delta) ? rxp_max
		                                          : s->rx_packets_delta;
		txp_max = (txp_max > s->tx_packets_delta) ? txp_max
		                                          : s->tx_packets_delta;
		rxb_min =
		    (rxb_min < s->rx_bytes_delta) ? rxb_min : s->rx_bytes_delta;
		txb_min =
		    (txb_min < s->tx_bytes_delta) ? txb_min : s->tx_bytes_delta;
		rxp_min = (rxp_min < s->rx_packets_delta) ? rxp_min
		                                          : s->rx_packets_delta;
		txp_min = (txp_min < s->tx_packets_delta) ? txp_min
		                                          : s->tx_packets_delta;

		ln = ln->next;
	}

	m->min_rx_bytes = rxb_min;
	m->max_rx_bytes = rxb_max;
	m->mean_rx_bytes = 1000 * rxb_sum / decim8;

	m->min_tx_bytes = txb_min;
	m->max_tx_bytes = txb_max;
	m->mean_tx_bytes = 1000 * txb_sum / decim8;

	m->min_rx_packets = rxp_min;
	m->max_rx_packets = rxp_max;
	m->mean_rx_packets = 1000 * rxp_sum / decim8;

	m->min_tx_packets = txp_min;
	m->max_tx_packets = txp_max;
	m->mean_tx_packets = 1000 * txp_sum / decim8;

	return 0;
}

inline static int
stats_filter(struct slist *list, struct mq_stats_msg *m, int decim8)
{
	int size = slist_size(list);
	int smod = size % decim8;

	if (!size || smod) {
		/* */
		return 1;
	}

	m->interval_ns = 1E6 * decim8;

	/* FIXME - get this from mq_stats_msg ? */
	sprintf(m->iface, "%s", g_selected_iface);

	calc_txrx_minmaxmean(list, m, decim8);
	calc_whoosh_err(list, m, decim8);
	calc_packet_gap(list, m, decim8);

	return 0;
}

inline static int message_producer(struct mq_stats_msg *m, void *data)
{
	int *decimation_factor = (int *)data;
	return stats_filter(sample_list, m, *decimation_factor);
}

void send_decimations()
{
	int cb_err;

	assert(SAMPLES_PER_FRAME <= decs[0]);

	for (int i = 0; i < DECIMATIONS_COUNT; i++) {

		if (0 == g_sample_count % decs[i]) {
			mq_stats_produce(message_producer, &decs[i], &cb_err);
		}
	}
}

static int frames_to_sample_list()
{
	int new_samples = 0;

	pthread_mutex_lock(&unsent_frame_count_mutex);
	while (g_unsent_frame_count > 0) {
		for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
			struct sample *s = malloc(sizeof(struct sample));
			assert(s);
			memcpy(s, &(g_raw_samples->samples[i]),
			       sizeof(struct sample));
			struct slist *ln = malloc(sizeof(struct slist));
			assert(ln);
			ln->s = s;
			slist_push(sample_list, ln);
			g_sample_count++;
			new_samples++;
		}
		g_unsent_frame_count--;
	}
	pthread_mutex_unlock(&unsent_frame_count_mutex);

	int overflow = (slist_size(sample_list) > MAX_LIST_LEN)
	                   ? slist_size(sample_list) - MAX_LIST_LEN
	                   : 0;

	while (overflow--) {
		struct slist *ln = slist_pop(sample_list);
		assert(ln);
		assert(ln->s);
		free(ln->s);
		free(ln);
	}

	if (g_sample_count > MAX_LIST_LEN) {
		g_sample_count -= MAX_LIST_LEN;
	}
	return new_samples;
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

	sample_list = slist_new();
	assert(sample_list);

	assert(!compute_thread);
	err = pthread_create(&compute_thread, NULL, run, NULL);
	assert(!err);
	pthread_setname_np(compute_thread, "jt-compute");

	err = sample_thread_init(sample_thread_event_handler);
	assert(!err);

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
		if (0 < frames_to_sample_list()) {
			send_decimations();
		}

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
