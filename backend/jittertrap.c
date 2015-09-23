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

#include "jittertrap.h"
#include "iface_stats.h"
#include "stats_thread.h"
#include "netem.h"
#include "mgmt_sock.h"

#define QUOTE(str) #str
#define EXPAND_AND_QUOTE(str) QUOTE(str)

static pthread_mutex_t unsent_frame_count_mutex;

struct iface_stats *g_raw_samples;
int g_unsent_frame_count = 0;
char g_selected_iface[MAX_IFACE_LEN];

int jt_get_sample_period()
{
	return get_sample_period();
}

/* json_arr_alloc: must free returned memory */
static char *json_arr_alloc()
{
	char *buf;

	buf = malloc(3);
	assert(NULL != buf);
	buf[0] = '[';
	buf[1] = ']';
	buf[2] = '\0';
	return buf;
}

/* *arr MUST be a malloc'ed pointer */
static void json_arr_append(char **arr, const char *const word)
{
	assert(NULL != arr);
	assert(NULL != *arr);
	assert(NULL != word);

	int buf_len = strlen(*arr);
	int word_len = strlen(word);

	/* comma, space, nul term */
	*arr = realloc(*arr, buf_len + word_len + 2 + 1);
	assert(NULL != *arr);

	if (buf_len >= 3) {
		memcpy((*arr) + buf_len - 1, ", ", 2);
		buf_len += 2;
	}
	memcpy(*arr + buf_len - 1, word, word_len);
	(*arr)[buf_len + word_len - 1] = ']';
	(*arr)[buf_len + word_len] = 0;
}

char ** jt_list_ifaces()
{
	return netem_list_ifaces();
}

char const * jt_get_iface()
{
	return g_selected_iface;
}

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
		fprintf(stderr,
		        "No interfaces available. "
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


static void stats_to_json(struct iface_stats *s, char json_msg[]) {

	char *m = json_arr_alloc();

	int i;
	for (i = 0; i < FILTERED_SAMPLES_PER_MSG; i++) {
		char msg[MAX_JSON_MSG_LEN];
		snprintf(msg,
			 MAX_JSON_MSG_LEN,
			 "{"
			 "\"rxDelta\":%" PRId64 ","
			 "\"txDelta\":%" PRId64 ","
			 "\"rxPktDelta\":%" PRId64 ","
			 "\"txPktDelta\":%" PRId64 ""
			 "}",
			 s->samples[i].rx_bytes_delta,
			 s->samples[i].tx_bytes_delta,
			 s->samples[i].rx_packets_delta,
			 s->samples[i].tx_packets_delta);
		json_arr_append(&m, msg);
	}

	snprintf(json_msg,
		 MAX_JSON_MSG_LEN,
		 "{\"msg\":\"stats\", \"p\": {\"iface\": \"%s\",\"s\": %s, \"whoosh_err_mean\": %"PRId64", \"whoosh_err_max\": %"PRId64", \"whoosh_err_sd\": %"PRId64"}}",
		 s->iface,
		 m,
		 s->whoosh_err_mean,
		 s->whoosh_err_max,
		 s->whoosh_err_sd
	);
	free(m);
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

	if ((max >= 500 * SAMPLE_PERIOD_US)
	   || (stats->whoosh_err_sd >= 200 * SAMPLE_PERIOD_US))
	{
		fprintf(stderr, "sampling jitter! mean: %10"PRId64" max: %10"PRId64" sd: %10"PRId64"\n",
		        stats->whoosh_err_mean,
		        stats->whoosh_err_max,
			stats->whoosh_err_sd);
	}
}

inline static void stats_filter(struct iface_stats *stats)
{
	calc_whoosh_error(stats);
}

void stats_filter_and_send()
{
	pthread_mutex_lock(&unsent_frame_count_mutex);
	if (g_unsent_frame_count > 0) {
		stats_filter(g_raw_samples);

		char *json_msg = malloc(MAX_JSON_MSG_LEN);
		assert(json_msg);
		stats_to_json(g_raw_samples, json_msg);
		mgmt_sock_stats_send(json_msg);
		free(json_msg);
		g_unsent_frame_count--;
	}
	pthread_mutex_unlock(&unsent_frame_count_mutex);
}

/* callback for the real-time stats thread. */
void stats_event_handler(struct iface_stats *raw_samples)
{
	pthread_mutex_lock(&unsent_frame_count_mutex);
	g_raw_samples = raw_samples;
	g_unsent_frame_count++;
	pthread_mutex_unlock(&unsent_frame_count_mutex);
}

int main()
{
	const char *s_http_port = EXPAND_AND_QUOTE(WEB_SERVER_PORT);
	printf("Allowed ifaces: %s\n", EXPAND_AND_QUOTE(ALLOWED_IFACES));
	printf("Web document root: %s\n",
	       EXPAND_AND_QUOTE(WEB_SERVER_DOCUMENT_ROOT));
	printf("Binding to port: %s\n", s_http_port);

	mgmt_sock_init(s_http_port,
	               EXPAND_AND_QUOTE(WEB_SERVER_DOCUMENT_ROOT));

	if (netem_init() < 0) {
		fprintf(stderr,
			"Couldn't initialise netlink for netem module.\n");
		return -1;
	}

	char iface[MAX_IFACE_LEN];
	get_first_iface(iface);
	jt_set_iface(iface);
	stats_thread_init(stats_event_handler);

	/* main loop in mgmt_sock.c */
	mgmt_sock_main(stats_filter_and_send, 10);

	return 0;
}
