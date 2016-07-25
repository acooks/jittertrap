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


#define QUOTE(str) #str
#define EXPAND_AND_QUOTE(str) QUOTE(str)

enum { JT_STATE_STOPPING,
       JT_STATE_STOPPED,
       JT_STATE_STARTING,
       JT_STATE_RUNNING,
};

int g_jt_state = JT_STATE_STARTING;
char g_selected_iface[MAX_IFACE_LEN];
unsigned long stats_consumer_id;
unsigned long tt_consumer_id;

static int set_netem(void *data)
{
	struct jt_msg_netem_params *p1 = data;
	struct netem_params p2 = {
		.delay = p1->delay, .jitter = p1->jitter, .loss = p1->loss,
	};

	netem_set_params(p1->iface, &p2);
	jt_srv_send_netem_params();
	return 0;
}

static int select_iface(void *data)
{
	char(*iface)[MAX_IFACE_LEN] = data;

	if (!is_iface_allowed(*iface)) {
		fprintf(stderr, "ignoring request to switch to iface: [%s] - "
		                "iface not in allowed list: [%s]\n",
		        *iface, EXPAND_AND_QUOTE(ALLOWED_IFACES));
		return -1;
	}
	snprintf(g_selected_iface, MAX_IFACE_LEN, "%s", *iface);
	printf("switching to iface: [%s]\n", *iface);
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

int jt_srv_send_netem_params()
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

int jt_srv_send_select_iface()
{
	char iface[MAX_IFACE_LEN];
	memcpy(&iface, g_selected_iface, MAX_IFACE_LEN);

	return jt_srv_send(JT_MSG_SELECT_IFACE_V1, &iface);
}

int jt_srv_send_iface_list()
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
		printf("available ifaces: ");
		do {
			printf(" %s", *iface);
			(il->count)++;
			iface++;
		} while (*iface);
		printf("\n");
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

int jt_srv_send_sample_period()
{
	int sp;
	sp = get_sample_period();
	return jt_srv_send(JT_MSG_SAMPLE_PERIOD_V1, &sp);
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

int jt_srv_send_stats()
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

int jt_srv_send_tt()
{
	int ret, cb_err;

	do {
		ret = mq_tt_consume(tt_consumer_id, tt_consumer, NULL, &cb_err);
	} while (JT_WS_MQ_OK == ret);

	return 0;
}

static int jt_init()
{
	int err;
	char *iface;

	if (netem_init() < 0) {
		fprintf(stderr,
		        "Couldn't initialise netlink for netem module.\n");
		return -1;
	}

	err = mq_ws_init();
	if (err) {
		return -1;
	}

	iface = malloc(MAX_IFACE_LEN);
	get_first_iface(iface);
	select_iface(iface);
	free(iface);

	mq_tt_init();
	mq_stats_init();
	compute_thread_init();
	intervals_thread_init();

	err = mq_stats_consumer_subscribe(&stats_consumer_id);
	assert(!err);

	err = mq_tt_consumer_subscribe(&tt_consumer_id);
	assert(!err);

	g_jt_state = JT_STATE_RUNNING;
	return 0;
}

int jt_server_tick()
{
	switch (g_jt_state) {
	case JT_STATE_STARTING:
		jt_init();
		jt_srv_send_iface_list();
		jt_srv_send_select_iface();
		jt_srv_send_netem_params();
		jt_srv_send_sample_period();
		break;
	case JT_STATE_RUNNING:
		/* queue a stats msg (if there is one) */
		jt_srv_send_stats();
		jt_srv_send_tt();
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
		json_decref(root);

		if (err) {
			// type matched, but unpack failed.
			fprintf(stderr, "[%s] type match, unpack failed.\n",
			        jt_messages[*msg_type].key);
			break;
		}

		jt_messages[*msg_type].print(data);

		switch (*msg_type) {
		case JT_MSG_SELECT_IFACE_V1:
			err = select_iface(data);
			break;
		case JT_MSG_SET_NETEM_V1:
			err = set_netem(data);
			break;
		default:
			/* no way to get here, right? */
			assert(0);
		}
		jt_messages[*msg_type].free(data);

		return err;
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
