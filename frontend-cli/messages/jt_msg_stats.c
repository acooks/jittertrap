#include <string.h>
#include <assert.h>
#include <jansson.h>

#include "jt_message_types.h"
#include "jt_messages.h"
#include "jt_msg_stats.h"

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
	json_t *obj;
	json_error_t error;
	json_t *iface, *samples, *err_mean, *err_max, *err_sd;
	int err;
	struct jt_msg_stats *stats;

	err = json_unpack_ex(root, &error, JSON_VALIDATE_ONLY, "{s:o}",
	                     jt_messages[JT_MSG_STATS_V1].key, &obj);
	if (err) {
		printf("not a stats object\n");
		return err;
	}

	obj = json_object_get(root, jt_messages[JT_MSG_STATS_V1].key);

	assert(JSON_OBJECT == json_typeof(obj));
	assert(0 < json_object_size(obj));

	stats = malloc(sizeof(struct jt_msg_stats));

	/* FIXME: this json_get_object() inteface sucks bricks,
	 * but the unpack API doesn't seem to work right now (jansson-2.7). :(
	 */
	iface = json_object_get(obj, "iface");
	if (!json_is_string(iface)) {
		goto unpack_fail;
	}
	snprintf(stats->iface, 255, "%s", json_string_value(iface));

	samples = json_object_get(obj, "s");
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

	err_mean = json_object_get(obj, "whoosh_err_mean");
	if (!json_is_integer(err_mean)) {
		goto unpack_fail_free_samples;
	}
	stats->err.mean = json_integer_value(err_mean);

	err_max = json_object_get(obj, "whoosh_err_max");
	if (!json_is_integer(err_max)) {
		goto unpack_fail_free_samples;
	}
	stats->err.max = json_integer_value(err_max);

	err_sd = json_object_get(obj, "whoosh_err_sd");
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
