#include <string.h>
#include <assert.h>
#include <jansson.h>

#include "jt_message_types.h"
#include "jt_messages.h"
#include "jt_msg_stats.h"

static const char *jt_stats_test_msg =
    "{\"msg\":\"stats\", \"p\":{\"iface\": \"em1\",\"s\": "
    "[{\"rxDelta\":0,\"txDelta\":0,\"rxPktDelta\":0,\"txPktDelta\":0}, "
    "{\"rxDelta\":0,\"txDelta\":0,\"rxPktDelta\":0,\"txPktDelta\":0},{"
    "\"rxDelta\":0,\"txDelta\":0,\"rxPktDelta\":0,\"txPktDelta\":0}], "
    "\"whoosh_err_mean\": 42809, \"whoosh_err_max\": 54759, \"whoosh_err_sd\": "
    "43249}}";

const char *jt_stats_test_msg_get() { return jt_stats_test_msg; }

int jt_stats_consumer(void *data)
{
	struct jt_msg_stats *stats = (struct jt_msg_stats *)data;
	double avgRx = 0, avgTx = 0;
	double avgPktRx = 0, avgPktTx = 0;
	int i;
	for (i = stats->sample_count - 1; i > 0; i--) {
		avgRx += stats->samples[i].rx;
		avgTx += stats->samples[i].tx;
		avgPktRx += stats->samples[i].rxPkt;
		avgPktTx += stats->samples[i].txPkt;
	}
	avgRx /= stats->sample_count;
	avgTx /= stats->sample_count;
	avgPktRx /= stats->sample_count;
	avgPktTx /= stats->sample_count;
	printf("\rRx: %10.1f Tx: %10.1f PtkRx: %10.1f PktTx: %10.1f E: %10d",
	       avgRx, avgTx, avgPktRx, avgPktTx, stats->err.mean);

	free(stats->samples);
	free(stats);
	return 0;
}

int jt_stats_unpacker(json_t *root, void **data)
{
	json_t *params;
	json_t *iface, *samples, *err_mean, *err_max, *err_sd;
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
	snprintf(stats->iface, 255, "%s", json_string_value(iface));

	samples = json_object_get(params, "s");
	if (!json_is_array(samples)) {
		goto unpack_fail;
	}

	stats->sample_count = json_array_size(samples);
	stats->samples =
	    malloc(stats->sample_count * sizeof(struct stats_sample));

	int i;
	for (i = 0; i < stats->sample_count; i++) {
		json_t *s = json_array_get(samples, i);
		assert(json_is_object(s));
		json_t *t;

		t = json_object_get(s, "rxDelta");
		assert(json_is_integer(t));
		stats->samples[i].rx = json_integer_value(t);

		t = json_object_get(s, "txDelta");
		assert(json_is_integer(t));
		stats->samples[i].tx = json_integer_value(t);

		t = json_object_get(s, "rxPktDelta");
		assert(json_is_integer(t));
		stats->samples[i].rxPkt = json_integer_value(t);

		t = json_object_get(s, "txPktDelta");
		assert(json_is_integer(t));
		stats->samples[i].txPkt = json_integer_value(t);
	}

	err_mean = json_object_get(params, "whoosh_err_mean");
	if (!json_is_integer(err_mean)) {
		goto unpack_fail_free_samples;
	}
	stats->err.mean = json_integer_value(err_mean);

	err_max = json_object_get(params, "whoosh_err_max");
	if (!json_is_integer(err_max)) {
		goto unpack_fail_free_samples;
	}
	stats->err.max = json_integer_value(err_max);

	err_sd = json_object_get(params, "whoosh_err_sd");
	if (!json_is_integer(err_sd)) {
		goto unpack_fail_free_samples;
	}
	stats->err.sd = json_integer_value(err_sd);

	*data = stats;
	return 0;

unpack_fail_free_samples:
	free(stats->samples);
unpack_fail:
	free(stats);
	return -1;
}

int jt_stats_packer(void *data, char **out)
{
	struct jt_msg_stats *stats_msg = data;
	json_t *t = json_object();
	json_t *samples_arr = json_array();
	json_t *params = json_object();

	json_object_set_new(params, "iface", json_string(stats_msg->iface));

	json_object_set_new(params, "whoosh_err_mean",
	                    json_integer(stats_msg->err.mean));
	json_object_set_new(params, "whoosh_err_max",
	                    json_integer(stats_msg->err.max));
	json_object_set_new(params, "whoosh_err_sd",
	                    json_integer(stats_msg->err.sd));

	json_t *sample[stats_msg->sample_count];
	// order matters!
	for (int i = 0; i < stats_msg->sample_count; i++) {
		sample[i] = json_object();
		json_object_set_new(sample[i], "rxDelta",
		                    json_integer(stats_msg->samples->rx));
		json_object_set_new(sample[i], "txDelta",
		                    json_integer(stats_msg->samples->rx));
		json_object_set_new(sample[i], "rxPktDelta",
		                    json_integer(stats_msg->samples->rx));
		json_object_set_new(sample[i], "txPktDelta",
		                    json_integer(stats_msg->samples->rx));
		json_array_append(samples_arr, sample[i]);
	}

	json_object_set_new(
	    t, "msg", json_string(jt_messages[JT_MSG_NETEM_PARAMS_V1].key));
	json_object_set(params, "s", samples_arr);
	json_object_set(t, "p", params);
	*out = json_dumps(t, 0);
	return 0;
}
