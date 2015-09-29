#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

#include <jansson.h>
#include "jt_server_message_handler.h"
#include "jt_ws_mq_config.h"
#include "jt_ws_mq.h"

#include "iface_stats.h"
#include "stats_thread.h"
#include "netem.h"

#include "jt_message_types.h"
#include "jt_messages.h"

#define QUOTE(str) #str
#define EXPAND_AND_QUOTE(str) QUOTE(str)

static pthread_mutex_t unsent_frame_count_mutex;

enum { JT_STATE_STOPPING,
       JT_STATE_STOPPED,
       JT_STATE_STARTING,
       JT_STATE_RUNNING,
};

int g_jt_state = JT_STATE_STARTING;
struct iface_stats *g_raw_samples;
int g_unsent_frame_count = 0;
char g_selected_iface[MAX_IFACE_LEN];

/* prototypes */
static int jt_init();

int jt_get_sample_period() { return get_sample_period(); }

char **jt_list_ifaces() { return netem_list_ifaces(); }

char const *jt_get_iface() { return g_selected_iface; }

int jt_set_iface(const char *iface)
{
	if (!is_iface_allowed(iface)) {
		fprintf(stderr, "ignoring request to switch to iface: [%s] - "
		                "iface not in allowed list: [%s]\n",
		        iface, EXPAND_AND_QUOTE(ALLOWED_IFACES));
		return -1;
	}
	snprintf(g_selected_iface, MAX_IFACE_LEN, "%s", iface);
	printf("switching to iface: [%s]\n", iface);
	stats_monitor_iface(iface);
	return 0;
}

static void get_first_iface(char *iface)
{
	char **ifaces = netem_list_ifaces();
	char **i = ifaces;
	assert(NULL != i);
	if (NULL == *i) {
		fprintf(stderr, "No interfaces available. "
		                "Allowed interfaces (compile-time): %s\n",
		        EXPAND_AND_QUOTE(ALLOWED_IFACES));
	}
	snprintf(iface, MAX_IFACE_LEN, "%s", *i);

	while (*i) {
		free(*i);
		i++;
	}

	free(ifaces);
}

static void iface_stats_to_msg_stats(struct iface_stats *if_s, struct jt_msg_stats *msg_s)
{
        snprintf(msg_s->iface, MAX_IFACE_LEN, "%s", if_s->iface);

	msg_s->sample_count = FILTERED_SAMPLES_PER_MSG;
	msg_s->samples =
            malloc(FILTERED_SAMPLES_PER_MSG * sizeof(struct stats_sample));

	for (int i = 0; i < FILTERED_SAMPLES_PER_MSG; i++) {
		msg_s->samples[i].rx = if_s->samples[i].rx_bytes_delta;
		msg_s->samples[i].tx = if_s->samples[i].tx_bytes_delta;
		msg_s->samples[i].rxPkt = if_s->samples[i].rx_packets_delta;
		msg_s->samples[i].txPkt = if_s->samples[i].tx_packets_delta;
	}
	msg_s->err.mean = if_s->whoosh_err_mean;
	msg_s->err.max = if_s->whoosh_err_max;
	msg_s->err.sd = if_s->whoosh_err_sd;
}

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

inline static void stats_filter(struct iface_stats *stats)
{
	calc_whoosh_error(stats);
}

static int message_producer(struct jt_ws_msg *m, void *data)
{
	char *s = (char *)data;
	snprintf(m->m, MAX_JSON_MSG_LEN, "%s", s);
	return 0;
}

/* returns number of chars written to out. */
int stats_filter_and_write()
{
	char *tmpstr;
	int err = 0;
	struct jt_msg_stats *msg_stats;

	pthread_mutex_lock(&unsent_frame_count_mutex);
	if (g_unsent_frame_count > 0) {

		stats_filter(g_raw_samples);

		/* convert from struct iface_stats to struct jt_msg_stats */
		msg_stats = malloc(sizeof(struct jt_msg_stats));
		iface_stats_to_msg_stats(g_raw_samples, msg_stats);

		/* convert from jt_msg_stats to string */
		err = jt_messages[JT_MSG_STATS_V1].to_json_string(msg_stats,
		                                                  &tmpstr);
		assert(!err);

		/* write the json string to a websocket message */
		err = jt_ws_mq_produce(message_producer, tmpstr);
		if (!err) {
			g_unsent_frame_count--;
		}

		/* cleanup */
		free(tmpstr);
		jt_messages[JT_MSG_STATS_V1].free(msg_stats);
	}
	pthread_mutex_unlock(&unsent_frame_count_mutex);
	return 0;
}

/* callback for the real-time stats thread. */
void stats_event_handler(struct iface_stats *raw_samples)
{
	pthread_mutex_lock(&unsent_frame_count_mutex);
	g_raw_samples = raw_samples;
	g_unsent_frame_count++;
	pthread_mutex_unlock(&unsent_frame_count_mutex);
}

static int jt_init()
{
	if (netem_init() < 0) {
		fprintf(stderr,
		        "Couldn't initialise netlink for netem module.\n");
		return -1;
	}

	char iface[MAX_IFACE_LEN];
	get_first_iface(iface);
	jt_set_iface(iface);
	stats_thread_init(stats_event_handler);
	g_jt_state = JT_STATE_RUNNING;
	return 0;
}

int jt_server_tick()
{
	switch (g_jt_state) {
	case JT_STATE_STARTING:
		jt_ws_mq_init();
		jt_init();
		/* write the other stuff */
		break;
	case JT_STATE_RUNNING:
		/* try to send a stats msg */
		stats_filter_and_write();
		break;
	}
	return 0;
}

static int jt_msg_handler(char *in, const int *msg_type_arr)
{
	json_t *root;
	json_error_t error;
	void *data;
	const int *msg_type;

	root = json_loads(in, 0, &error);
	if (!root) {
		fprintf(stderr, "error: on line %d: %s\n", error.line,
		        error.text);
		return -1;
	}

	// iterate over array of msg types using pointer arithmetic.
	for (msg_type = msg_type_arr; *msg_type != JT_MSG_END; msg_type++) {
		// check if the message type matches.
		int err;
		err = jt_msg_match_type(root, *msg_type);
		if (err) {
			// type doesn't match, try the next.
			continue;
		}

		// type matches, try to unpack it.
		err = jt_messages[*msg_type].to_struct(root, &data);
		if (err) {
			// type matched, but unpack failed.
			fprintf(stderr, "[%s] type match, unpack failed.\n",
			        jt_messages[*msg_type].key);
			break;
		}

		jt_messages[*msg_type].print(data);
		json_decref(root);
		return 0;
	}
	fprintf(stderr, "couldn't unpack message: %s\n", in);
	json_decref(root);
	return -1;
}

/* handle messages received from client in server */
int jt_server_msg_receive(char *in)
{
	return jt_msg_handler(in, &jt_msg_types_c2s[0]);
}
