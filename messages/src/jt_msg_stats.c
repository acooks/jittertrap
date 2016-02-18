#define _POSIX_C_SOURCE 200809L
#include <time.h>

#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <jansson.h>
#include <inttypes.h>

#include "jt_message_types.h"
#include "jt_messages.h"
#include "jt_msg_stats.h"

/* FIXME: incomplete. needs min, max, mean for bytes, packets, pgaps */
static const char *jt_stats_test_msg =
    "{\"msg\":\"stats\", \"p\":{\"iface\": \"em1\","
    "\"t\": {\"tv_sec\":0, \"tv_nsec\":0}, "
    "\"ival_ns\": 0, "
    "\"s\": "
    "{\"rx\":0,\"tx\":0,\"rxP\":0,\"txP\":0, "
    "\"min_rx_pgap\":0,\"max_rx_pgap\":0,\"mean_rx_pgap\":0, "
    "\"min_tx_pgap\":0,\"max_tx_pgap\":0,\"mean_tx_pgap\":0}, "
    "\"whoosh_err_mean\": 42809, \"whoosh_err_max\": 54759, \"whoosh_err_sd\": "
    "43249}}";

const char *jt_stats_test_msg_get() { return jt_stats_test_msg; }

int jt_stats_free(void *data)
{
	struct jt_msg_stats *stats = (struct jt_msg_stats *)data;
	free(stats);
	return 0;
}

int jt_stats_printer(void *data)
{
	struct jt_msg_stats *stats = (struct jt_msg_stats *)data;

	printf("\rT: %"PRId64".%09"PRId64", Rx: %10"PRId64" Tx: %10"PRId64" PtkRx: %10"PRId32" PktTx: %10"PRId32" E: %10"PRId32,
	       stats->timestamp.tv_sec,
	       stats->timestamp.tv_nsec,
	       stats->mean_rx_bytes,
	       stats->mean_tx_bytes,
	       stats->mean_rx_packets,
	       stats->mean_tx_packets,
	       stats->mean_whoosh);
	return 0;
}

int jt_stats_unpacker(json_t *root, void **data)
{
	json_t *params;
	json_t *iface, *json_stats;
	json_t *mts;

	struct jt_msg_stats *stats;

	params = json_object_get(root, "p");
	assert(params);
	assert(JSON_OBJECT == json_typeof(params));
	assert(0 < json_object_size(params));

	stats = malloc(sizeof(struct jt_msg_stats));

	/* FIXME: this json_get_object() inteface sucks bricks,
	 * but the unpack API doesn't seem to work right now (jansson-2.7). :(
	 */
	iface = json_object_get(params, "iface");
	if (!json_is_string(iface)) {
		goto unpack_fail;
	}
	snprintf(stats->iface, MAX_IFACE_LEN, "%s", json_string_value(iface));

	json_stats = json_object_get(params, "s");
	if (!json_is_object(json_stats)) {
		goto unpack_fail;
	}

	json_t *t;
	t = json_object_get(json_stats, "rx");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	stats->mean_rx_bytes = json_integer_value(t);

	t = json_object_get(json_stats, "tx");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	stats->mean_tx_bytes = json_integer_value(t);

	t = json_object_get(json_stats, "rxP");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	stats->mean_rx_packets = json_integer_value(t);

	t = json_object_get(json_stats, "txP");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	stats->mean_tx_packets = json_integer_value(t);

	t = json_object_get(json_stats, "min_rx_pgap");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	stats->min_rx_packet_gap = json_integer_value(t);

	t = json_object_get(json_stats, "max_rx_pgap");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	stats->max_rx_packet_gap = json_integer_value(t);

	t = json_object_get(json_stats, "mean_rx_pgap");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	stats->mean_rx_packet_gap = json_integer_value(t);

	t = json_object_get(json_stats, "min_tx_pgap");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	stats->min_tx_packet_gap = json_integer_value(t);

	t = json_object_get(json_stats, "max_tx_pgap");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	stats->max_tx_packet_gap = json_integer_value(t);

	t = json_object_get(json_stats, "mean_tx_pgap");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	stats->mean_tx_packet_gap = json_integer_value(t);

	/* get the stats sampling time error */
	t = json_object_get(params, "whoosh_err_mean");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	stats->mean_whoosh = json_integer_value(t);

	t = json_object_get(params, "whoosh_err_max");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	stats->max_whoosh = json_integer_value(t);

	t = json_object_get(params, "whoosh_err_sd");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	stats->sd_whoosh = json_integer_value(t);

	t = json_object_get(params, "ival_ns");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	stats->interval_ns = json_integer_value(t);

	/* get the message timestamp */
	mts = json_object_get(params, "t");
	if (!json_is_object(mts)) {
		goto unpack_fail;
	}

	t = json_object_get(mts, "tv_sec");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	stats->timestamp.tv_sec = json_integer_value(t);

	t = json_object_get(mts, "tv_nsec");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	stats->timestamp.tv_nsec = json_integer_value(t);

	*data = stats;
	json_object_clear(params);
	return 0;

unpack_fail:
	free(stats);
	return -1;
}

int jt_stats_packer(void *data, char **out)
{
	struct timespec mts; /* message timestamp */
	struct jt_msg_stats *stats_msg = data;
	json_t *t = json_object();
	json_t *stats = json_object();
	json_t *params = json_object();
	json_t *jmts = json_object();

	json_object_set_new(params, "iface", json_string(stats_msg->iface));
	json_object_set_new(params, "ival_ns",
	                    json_integer(stats_msg->interval_ns));

	json_object_set_new(params, "whoosh_err_mean",
	                    json_integer(stats_msg->mean_whoosh));
	json_object_set_new(params, "whoosh_err_max",
	                    json_integer(stats_msg->max_whoosh));
	json_object_set_new(params, "whoosh_err_sd",
	                    json_integer(stats_msg->sd_whoosh));

	json_object_set_new(stats, "rx",
			    json_integer(stats_msg->mean_rx_bytes));
	json_object_set_new(stats, "tx",
	                    json_integer(stats_msg->mean_tx_bytes));
	json_object_set_new(stats, "rxP",
	                    json_integer(stats_msg->mean_rx_packets));
	json_object_set_new(stats, "txP",
	                    json_integer(stats_msg->mean_tx_packets));

	json_object_set_new(stats, "min_rx_pgap",
	                    json_integer(stats_msg->min_rx_packet_gap));
	json_object_set_new(stats, "max_rx_pgap",
	                    json_integer(stats_msg->max_rx_packet_gap));
	json_object_set_new(stats, "mean_rx_pgap",
	                    json_integer(stats_msg->mean_rx_packet_gap));

	json_object_set_new(stats, "min_tx_pgap",
	                    json_integer(stats_msg->min_tx_packet_gap));
	json_object_set_new(stats, "max_tx_pgap",
	                    json_integer(stats_msg->max_tx_packet_gap));
	json_object_set_new(stats, "mean_tx_pgap",
	                    json_integer(stats_msg->mean_tx_packet_gap));

	json_object_set(params, "s", stats);
	json_object_set_new(t, "msg",
	                    json_string(jt_messages[JT_MSG_STATS_V1].key));
	json_object_set(t, "p", params);

	/* timestamp the new message */
	clock_gettime(CLOCK_MONOTONIC, &mts);
	json_object_set_new(jmts, "tv_sec", json_integer(mts.tv_sec));
	json_object_set_new(jmts, "tv_nsec", json_integer(mts.tv_nsec));
	json_object_set_new(params, "t", jmts);

	*out = json_dumps(t, 0);
	json_object_clear(stats);
	json_decref(stats);
	json_object_clear(params);
	json_decref(params);
	json_object_clear(t);
	json_decref(t);
	return 0;
}
