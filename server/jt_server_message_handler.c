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
#include <syslog.h>
#include <unistd.h>

#include <jansson.h>
#include "jittertrap.h"
#include "jt_server_message_handler.h"

#include "iface_stats.h"
#include "sampling_thread.h"
#include "compute_thread.h"
#include "tt_thread.h"
#include "netem.h"

#include "mq_msg_stats.h"
#include "mq_msg_ws.h"
#include "mq_msg_tt.h"

#include "jt_message_types.h"
#include "jt_messages.h"

#include "pcap_buffer.h"


#define QUOTE(str) #str
#define EXPAND_AND_QUOTE(str) QUOTE(str)

enum {
	JT_STATE_STOPPING,
	JT_STATE_STOPPED,
	JT_STATE_STARTING,
	JT_STATE_RUNNING,
	JT_STATE_PAUSED
};

int g_jt_state = JT_STATE_STARTING;
char g_selected_iface[MAX_IFACE_LEN];
unsigned long stats_consumer_id;
unsigned long tt_consumer_id;

static int set_netem(void *data)
{
#ifdef DISABLE_IMPAIRMENTS
	(void)data;
	syslog(LOG_WARNING,
	       "ignoring set_netem request: impairments disabled at compile time\n");
	return 0;
#else
	struct jt_msg_netem_params *p1 = data;
	struct netem_params p2 = {
		.delay = p1->delay, .jitter = p1->jitter, .loss = p1->loss,
	};

	netem_set_params(p1->iface, &p2);
	jt_srv_send_netem_params();
	return 0;
#endif
}

static int select_iface(void *data)
{
	char(*iface)[MAX_IFACE_LEN] = data;

	if (!is_iface_allowed(*iface)) {
		syslog(LOG_WARNING,
		       "ignoring request to switch to iface: [%s] - "
		       "iface not in allowed list: [%s]\n",
		       *iface, EXPAND_AND_QUOTE(ALLOWED_IFACES));
		return -1;
	}
	snprintf(g_selected_iface, MAX_IFACE_LEN, "%s", *iface);
	syslog(LOG_INFO, "switching to iface: [%s]\n", *iface);
	sample_iface(*iface);
	tt_thread_restart(*iface);

	jt_srv_send_select_iface();
	jt_srv_send_netem_params();
	jt_srv_send_sample_period();
	return 0;
}

static void get_first_iface(char *iface)
{
	char **ifaces = netem_list_ifaces();
	char **i = ifaces;
	assert(NULL != i);
	if (NULL == *i) {
		syslog(LOG_WARNING, "No interfaces available. "
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

/* FIXME: this is fugly. */
static void mq_stats_msg_to_jt_msg_stats(struct mq_stats_msg *mq_s,
                                         struct jt_msg_stats *msg_s)
{
	snprintf(msg_s->iface, MAX_IFACE_LEN, "%s", mq_s->iface);

	msg_s->mean_rx_bytes = mq_s->mean_rx_bytes;
	msg_s->mean_tx_bytes = mq_s->mean_tx_bytes;
	msg_s->mean_rx_packets = mq_s->mean_rx_packets;
	msg_s->mean_tx_packets = mq_s->mean_tx_packets;

	msg_s->mean_whoosh = mq_s->mean_whoosh;
	msg_s->max_whoosh = mq_s->max_whoosh;
	msg_s->sd_whoosh = mq_s->sd_whoosh;

	msg_s->min_rx_packet_gap = mq_s->min_rx_packet_gap;
	msg_s->max_rx_packet_gap = mq_s->max_rx_packet_gap;
	msg_s->mean_rx_packet_gap = mq_s->mean_rx_packet_gap;

	msg_s->min_tx_packet_gap = mq_s->min_tx_packet_gap;
	msg_s->max_tx_packet_gap = mq_s->max_tx_packet_gap;
	msg_s->mean_tx_packet_gap = mq_s->mean_tx_packet_gap;

	msg_s->interval_ns = mq_s->interval_ns;
}

inline static int message_producer(struct mq_ws_msg *m, void *data)
{
	char *s = (char *)data;
	snprintf(m->m, MAX_JSON_MSG_LEN, "%s", s);
	return 0;
}

int jt_srv_send(int msg_type, void *msg_data)
{
	char *tmpstr;
	int cb_err, err = 0;

	/* convert from jt_msg_* to string */
	err = jt_messages[msg_type].to_json_string(msg_data, &tmpstr);
	if (err) {
		return -1;
	}

	/* write the json string to a websocket message */
	err = mq_ws_produce(message_producer, tmpstr, &cb_err);
	free(tmpstr);
	return err;
}

int jt_srv_send_netem_params(void)
{
	struct netem_params p;
	struct jt_msg_netem_params *m =
	    malloc(sizeof(struct jt_msg_netem_params));
	assert(m);

	memcpy(p.iface, g_selected_iface, MAX_IFACE_LEN);

	if (0 != netem_get_params(p.iface, &p)) {
		/* There need not be a netem qdisc on the interface */
		p.delay = -1;
		p.jitter = -1;
		p.loss = -1;
	}

	m->delay = p.delay;
	m->jitter = p.jitter;
	m->loss = p.loss;
	snprintf(m->iface, MAX_IFACE_LEN, "%s", p.iface);

	int err = jt_srv_send(JT_MSG_NETEM_PARAMS_V1, m);
	free(m);
	return err;
}

int jt_srv_send_select_iface(void)
{
	char iface[MAX_IFACE_LEN];
	memcpy(&iface, g_selected_iface, MAX_IFACE_LEN);

	return jt_srv_send(JT_MSG_SELECT_IFACE_V1, &iface);
}

int jt_srv_send_iface_list(void)
{
	struct jt_iface_list *il;
	char **iface;
	int idx;
	char **ifaces = netem_list_ifaces();

	il = malloc(sizeof(struct jt_iface_list));
	assert(il);

	il->count = 0;
	iface = ifaces;
	assert(NULL != iface);
	if (NULL == *iface) {
		fprintf(stderr, "No interfaces available. "
		                "Allowed interfaces (compile-time): %s\n",
		        EXPAND_AND_QUOTE(ALLOWED_IFACES));
		free(ifaces);
		free(il);
		return -1;
	} else {
		char buff[128] = {0};
		char *offset = buff;
		int blen = sizeof(buff);
		do {
			snprintf(offset, blen, "%s ", *iface);
			blen -= strlen(offset);
			offset += strlen(offset);

			(il->count)++;
			iface++;
		} while (*iface);

		syslog(LOG_INFO, "available ifaces: %s", buff);
	}

	il->ifaces = malloc(il->count * MAX_IFACE_LEN);
	assert(il->ifaces);

	for (iface = ifaces, idx = 0; NULL != *iface && idx < il->count;
	     idx++) {
		strncpy(il->ifaces[idx], *iface, MAX_IFACE_LEN);
		free(*iface);
		iface++;
	}

	free(ifaces);
	int err = jt_srv_send(JT_MSG_IFACE_LIST_V1, il);
	jt_messages[JT_MSG_IFACE_LIST_V1].free(il);
	return err;
}

int jt_srv_send_sample_period(void)
{
	int sp;
	sp = get_sample_period();
	return jt_srv_send(JT_MSG_SAMPLE_PERIOD_V1, &sp);
}

int jt_srv_send_pcap_config(void)
{
	struct pcap_buf_config buf_cfg;
	struct jt_msg_pcap_config msg;

	if (pcap_buf_get_config(&buf_cfg) != 0) {
		/* Buffer not initialized */
		msg.enabled = 0;
		msg.max_memory_mb = PCAP_BUF_DEFAULT_MAX_MEM_MB;
		msg.duration_sec = PCAP_BUF_DEFAULT_DURATION_SEC;
		msg.pre_trigger_sec = PCAP_BUF_DEFAULT_PRE_TRIGGER;
		msg.post_trigger_sec = PCAP_BUF_DEFAULT_POST_TRIGGER;
	} else {
		pcap_buf_state_t state = pcap_buf_get_state();
		msg.enabled = (state != PCAP_BUF_STATE_DISABLED) ? 1 : 0;
		msg.max_memory_mb = buf_cfg.max_memory_bytes / (1024 * 1024);
		msg.duration_sec = buf_cfg.duration_sec;
		msg.pre_trigger_sec = buf_cfg.pre_trigger_sec;
		msg.post_trigger_sec = buf_cfg.post_trigger_sec;
	}

	return jt_srv_send(JT_MSG_PCAP_CONFIG_V1, &msg);
}

int jt_srv_send_pcap_status(void)
{
	struct pcap_buf_stats buf_stats;
	struct jt_msg_pcap_status msg;
	struct timeval now;

	if (pcap_buf_get_stats(&buf_stats) != 0) {
		/* Buffer not initialized */
		memset(&msg, 0, sizeof(msg));
		msg.state = PCAP_BUF_STATE_DISABLED;
	} else {
		msg.state = buf_stats.state;
		msg.total_packets = buf_stats.total_packets;
		msg.total_bytes = buf_stats.total_bytes;
		msg.dropped_packets = buf_stats.dropped_packets;
		msg.current_memory_mb = buf_stats.current_memory / (1024 * 1024);
		msg.buffer_percent = buf_stats.buffer_percent;

		/* Calculate oldest packet age */
		gettimeofday(&now, NULL);
		if (buf_stats.oldest_ts_sec > 0 && now.tv_sec >= (time_t)buf_stats.oldest_ts_sec) {
			msg.oldest_age_sec = now.tv_sec - buf_stats.oldest_ts_sec;
		} else {
			msg.oldest_age_sec = 0;
		}
	}

	return jt_srv_send(JT_MSG_PCAP_STATUS_V1, &msg);
}

int jt_srv_send_pcap_ready(struct pcap_buf_trigger_result *result)
{
	struct jt_msg_pcap_ready msg;
	char *basename_ptr;

	/* Extract just the filename for the URL path */
	basename_ptr = strrchr(result->filepath, '/');
	if (basename_ptr) {
		snprintf(msg.filename, sizeof(msg.filename),
		         "/pcap%s", basename_ptr);
	} else {
		/* No slash in path - use filename directly, truncate if needed */
		snprintf(msg.filename, sizeof(msg.filename),
		         "/pcap/%.240s", result->filepath);
	}

	msg.file_size = result->file_size;
	msg.packet_count = result->packet_count;
	msg.duration_sec = result->duration_sec;

	return jt_srv_send(JT_MSG_PCAP_READY_V1, &msg);
}

static int set_pcap_config(void *data)
{
	struct jt_msg_pcap_config *cfg = data;
	struct pcap_buf_config buf_cfg;

	/* Get current config as base */
	if (pcap_buf_get_config(&buf_cfg) != 0) {
		/* Initialize with defaults if not yet initialized */
		buf_cfg.max_memory_bytes = cfg->max_memory_mb * 1024 * 1024;
		buf_cfg.duration_sec = cfg->duration_sec;
		buf_cfg.pre_trigger_sec = cfg->pre_trigger_sec;
		buf_cfg.post_trigger_sec = cfg->post_trigger_sec;
		buf_cfg.datalink_type = DLT_EN10MB;
		buf_cfg.snaplen = BUFSIZ;

		if (pcap_buf_init(&buf_cfg) != 0) {
			syslog(LOG_ERR, "Failed to initialize pcap buffer\n");
			return -1;
		}
	} else {
		/* Update existing config */
		buf_cfg.duration_sec = cfg->duration_sec;
		buf_cfg.pre_trigger_sec = cfg->pre_trigger_sec;
		buf_cfg.post_trigger_sec = cfg->post_trigger_sec;

		if (cfg->max_memory_mb > 0) {
			buf_cfg.max_memory_bytes = cfg->max_memory_mb * 1024 * 1024;
		}

		pcap_buf_set_config(&buf_cfg);
	}

	/* Enable or disable based on config */
	if (cfg->enabled) {
		pcap_buf_enable();
	} else {
		pcap_buf_disable();
	}

	/* Send updated config and status back to client */
	jt_srv_send_pcap_config();
	jt_srv_send_pcap_status();

	return 0;
}

static int trigger_pcap(void *data)
{
	struct jt_msg_pcap_trigger *trigger = data;
	struct pcap_buf_trigger_result result;
	int err;

	syslog(LOG_INFO, "PCAP trigger requested: %s\n", trigger->reason);

	/* Trigger the capture */
	err = pcap_buf_trigger(trigger->reason);
	if (err) {
		syslog(LOG_ERR, "PCAP trigger failed\n");
		return -1;
	}

	/* Wait for post-trigger period if configured */
	while (!pcap_buf_post_trigger_complete()) {
		usleep(100000); /* 100ms */
	}

	/* Write the pcap file */
	err = pcap_buf_write_file(&result);
	if (err) {
		syslog(LOG_ERR, "PCAP file write failed\n");
		return -1;
	}

	/* Send ready notification to client */
	jt_srv_send_pcap_ready(&result);
	jt_srv_send_pcap_status();

	return 0;
}

static int stats_consumer(struct mq_stats_msg *m, void *data)
{
	struct jt_msg_stats *s = (struct jt_msg_stats *)data;
	mq_stats_msg_to_jt_msg_stats(m, s);
	if (0 == jt_srv_send(JT_MSG_STATS_V1, s)) {
		return 0;
	}
	return 1;
}

int jt_srv_send_stats(void)
{
	struct jt_msg_stats *msg_stats;
	int err, cb_err;

	do {
		/* convert from struct iface_stats to struct jt_msg_stats */
		msg_stats = malloc(sizeof(struct jt_msg_stats));
		assert(msg_stats);
		err = mq_stats_consume(stats_consumer_id, stats_consumer,
		                       msg_stats, &cb_err);
		/* cleanup */
		jt_messages[JT_MSG_STATS_V1].free(msg_stats);
	} while (JT_WS_MQ_OK == err);

	return 0;
}

static int tt_consumer(struct mq_tt_msg *m, void *data)
{
	(void)data;

	//jt_messages[JT_MSG_TOPTALK_V1].print(m);

	if (0 == jt_srv_send(JT_MSG_TOPTALK_V1, (struct jt_msg_toptalk *)m)) {
		return 0;
	}
	return 1;
}

int jt_srv_send_tt(void)
{
	int ret, cb_err;

	do {
		ret = mq_tt_consume(tt_consumer_id, tt_consumer, NULL, &cb_err);
	} while (JT_WS_MQ_OK == ret);

	return 0;
}

static int jt_init(void)
{
	int err;
	char *iface;

	if (netem_init() < 0) {
		fprintf(stderr,
		        "Couldn't initialise netlink for netem module.\n");
		return -1;
	}

	err = mq_ws_init("ws");
	if (err) {
		return -1;
	}

	iface = malloc(MAX_IFACE_LEN);
	get_first_iface(iface);
	select_iface(iface);
	free(iface);

	mq_tt_init("tt");
	mq_stats_init("stats");
	compute_thread_init();
	intervals_thread_init();

	err = mq_stats_consumer_subscribe(&stats_consumer_id);
	assert(!err);

	err = mq_tt_consumer_subscribe(&tt_consumer_id);
	assert(!err);

	g_jt_state = JT_STATE_RUNNING;
	return 0;
}

int jt_srv_pause(void)
{
	int err;

	assert(JT_STATE_RUNNING == g_jt_state);

	err = mq_stats_consumer_unsubscribe(stats_consumer_id);
	assert(!err);

	err = mq_tt_consumer_unsubscribe(tt_consumer_id);
	assert(!err);

	g_jt_state = JT_STATE_PAUSED;
	return 0;
}

int jt_srv_resume(void)
{
	int err;

	if (JT_STATE_PAUSED == g_jt_state) {
		err = mq_stats_consumer_subscribe(&stats_consumer_id);
		assert(!err);

		err = mq_tt_consumer_subscribe(&tt_consumer_id);
		assert(!err);

		g_jt_state = JT_STATE_RUNNING;
	}
	assert(JT_STATE_RUNNING == g_jt_state);
	return 0;
}

/* Counter for periodic pcap status updates */
static int pcap_status_tick = 0;
#define PCAP_STATUS_INTERVAL 100  /* Send pcap status every 100 ticks (~1s) */

int jt_server_tick(void)
{
	switch (g_jt_state) {
	case JT_STATE_STARTING:
		jt_init();
		jt_srv_send_iface_list();
		jt_srv_send_select_iface();
		jt_srv_send_netem_params();
		jt_srv_send_sample_period();
		jt_srv_send_pcap_config();
		jt_srv_send_pcap_status();
		break;
	case JT_STATE_RUNNING:
		/* queue a stats msg (if there is one) */
		jt_srv_send_stats();
		jt_srv_send_tt();

		/* Periodically send pcap status */
		pcap_status_tick++;
		if (pcap_status_tick >= PCAP_STATUS_INTERVAL) {
			jt_srv_send_pcap_status();
			pcap_status_tick = 0;
		}
		break;
	case JT_STATE_PAUSED:
		break;
	}
	return 0;
}

static int jt_msg_handler(char *in_unsafe, int len, const int *msg_type_arr)
{
	json_t *root;
	json_error_t error;
	void *data;
	const int *msg_type;
	char in_safe[1024];

	if (len <= 0) {
		syslog(LOG_ERR, "error: message cannot have negative length");
		return -1;
	}

	/* in_unsafe is not null-terminated and len doesn't include \0 */
	if ((long unsigned int)len >= sizeof(in_safe)) {
		snprintf(in_safe, sizeof(in_safe), "%s", in_unsafe);
		syslog(LOG_DEBUG, "invalid message truncated and ignored: %s\n",
			in_safe);
		return -1;
	}

	snprintf(in_safe, len+1, "%s", in_unsafe);

	root = json_loadb(in_safe, len, 0, &error);
	if (!root) {
		syslog(LOG_ERR, "error: %s loading message:%s\n",
			error.text, in_safe);
		return -1;
	}

	// iterate over array of msg types using pointer arithmetic.
	for (msg_type = msg_type_arr; *msg_type != JT_MSG_END; msg_type++) {
		char printable[1024];
		int err;

		// check if the message type matches.
		err = jt_msg_match_type(root, *msg_type);
		if (err) {
			// type doesn't match, try the next.
			continue;
		}

		// type matches, try to unpack it.
		err = jt_messages[*msg_type].to_struct(root, &data);
		json_decref(root);

		if (err) {
			// type matched, but unpack failed.
			syslog(LOG_ERR, "[%s] type match, unpack failed.\n",
			        jt_messages[*msg_type].key);
			break;
		}

		jt_messages[*msg_type].print(data, printable, sizeof(printable));
		syslog(LOG_DEBUG, "MSG received: %s", printable);

		switch (*msg_type) {
		case JT_MSG_SELECT_IFACE_V1:
			err = select_iface(data);
			break;
		case JT_MSG_SET_NETEM_V1:
			err = set_netem(data);
			break;
		case JT_MSG_HELLO_V1:
			syslog(LOG_INFO, "new session");
			break;
		case JT_MSG_PCAP_CONFIG_V1:
			err = set_pcap_config(data);
			break;
		case JT_MSG_PCAP_TRIGGER_V1:
			err = trigger_pcap(data);
			break;
		default:
			/* no way to get here, right? */
			assert(0);
		}
		jt_messages[*msg_type].free(data);

		return err;
	}
	syslog(LOG_WARNING, "message received and ignored: %s\n", in_safe);
	json_decref(root);
	return -1;
}

/* handle messages received from client in server */
int jt_server_msg_receive(char *in, int len)
{
	return jt_msg_handler(in, len, &jt_msg_types_c2s[0]);
}
