#define _GNU_SOURCE
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <pthread.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <jansson.h>
#include <inttypes.h>

#include "jt_message_types.h"
#include "jt_messages.h"
#include "jt_msg_toptalk.h"

static const char *tt_test_msg =
    "{\"msg\":\"toptalk\","
    " \"p\":{\"tflows\":5, \"tbytes\": 9999, \"tpackets\": 888,"
    " \"interval_ns\": 123,"
    " \"timestamp\": {\"tv_sec\": 123, \"tv_nsec\": 456},"
    " \"flows\": ["
    "{\"src\":\"192.168.0.1\", \"dst\": \"192.168.0.2\", \"sport\":32000, \"dport\":32000, \"proto\": \"udp\", \"bytes\":100, \"packets\":10, \"tclass\":\"af11\" },"
    "{\"src\":\"192.168.0.1\", \"dst\": \"192.168.0.2\", \"sport\":32001, \"dport\":32001, \"proto\": \"udp\", \"bytes\":100, \"packets\":10, \"tclass\":\"BE\"},"
    "{\"src\":\"192.168.0.1\", \"dst\": \"192.168.0.2\", \"sport\":32002, \"dport\":32002, \"proto\": \"udp\", \"tclass\":\"EF\", \"bytes\":100, \"packets\":10},"
    "{\"src\":\"192.168.0.1\", \"dst\": \"192.168.0.2\", \"sport\":32003, \"dport\":32003, \"tclass\":\"cs1\", \"proto\": \"udp\", \"bytes\":100, \"packets\":10},"
    "{\"src\":\"192.168.0.1\", \"dst\": \"192.168.0.2\", \"sport\":32004, \"dport\":32004, \"proto\": \"udp\", \"bytes\":100, \"packets\":10, \"tclass\":\"AF41\"}"
    "]}}";

const char* jt_toptalk_test_msg_get(void) { return tt_test_msg; }

int jt_toptalk_printer(void *data, char *out, int len)
{
	struct jt_msg_toptalk *t = (struct jt_msg_toptalk*)data;

	snprintf(out, len,
	         "t:%ld.%09ld fc:%"PRId32", b: %"PRId64", p:%"PRId64"",
	         t->timestamp.tv_sec, t->timestamp.tv_nsec, t->tflows, t->tbytes,
	         t->tpackets);
	return 0;
}

int jt_toptalk_unpacker(json_t *root, void **data)
{
	json_t *params;
	json_t *t, *flows, *timestamp;

	struct jt_msg_toptalk *tt;

	params = json_object_get(root, "p");
	assert(params);
	assert(JSON_OBJECT == json_typeof(params));
	assert(0 < json_object_size(params));

	tt = malloc(sizeof(struct jt_msg_toptalk));

	t = json_object_get(params, "tflows");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	tt->tflows = json_integer_value(t);

	t = json_object_get(params, "tbytes");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	tt->tbytes = json_integer_value(t);

	t = json_object_get(params, "tpackets");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	tt->tpackets = json_integer_value(t);

	t = json_object_get(params, "interval_ns");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	tt->interval_ns = json_integer_value(t);

	timestamp = json_object_get(params, "timestamp");
	if ((JSON_OBJECT != json_typeof(timestamp))
	    || (0 == json_object_size(timestamp)))
	{
		goto unpack_fail;
	}

	t = json_object_get(timestamp, "tv_sec");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	tt->timestamp.tv_sec = json_integer_value(t);

	t = json_object_get(timestamp, "tv_nsec");
	if (!json_is_integer(t)) {
		goto unpack_fail;
	}
	tt->timestamp.tv_nsec = json_integer_value(t);

	flows = json_object_get(params, "flows");
	if (!json_is_array(flows)) {
		goto unpack_fail;
	}

	int l = json_array_size(flows);

	/* tt->tflows may be more than the number of flows we send! */

	int i;
	for (i = 0; i < l; i++) {
		json_t *f = json_array_get(flows, i);
		t = json_object_get(f, "bytes");
		if (!json_is_integer(t)) {
			goto unpack_fail;
		}
		tt->flows[i].bytes = json_integer_value(t);

		t = json_object_get(f, "packets");
		if (!json_is_integer(t)) {
			goto unpack_fail;
		}
		tt->flows[i].packets = json_integer_value(t);

		t = json_object_get(f, "rtt_us");
		if (json_is_integer(t)) {
			tt->flows[i].rtt_us = json_integer_value(t);
		} else {
			tt->flows[i].rtt_us = -1;  /* Default if not present */
		}

		t = json_object_get(f, "tcp_state");
		if (json_is_integer(t)) {
			tt->flows[i].tcp_state = json_integer_value(t);
		} else {
			tt->flows[i].tcp_state = -1;  /* Default if not present */
		}

		t = json_object_get(f, "saw_syn");
		tt->flows[i].saw_syn = json_is_integer(t) ?
		                       json_integer_value(t) : 0;

		/* Window/Congestion tracking fields */
		t = json_object_get(f, "rwnd_bytes");
		tt->flows[i].rwnd_bytes = json_is_integer(t) ?
		                          json_integer_value(t) : -1;

		t = json_object_get(f, "window_scale");
		tt->flows[i].window_scale = json_is_integer(t) ?
		                            json_integer_value(t) : -1;

		t = json_object_get(f, "zero_window_cnt");
		tt->flows[i].zero_window_cnt = json_is_integer(t) ?
		                               json_integer_value(t) : 0;

		t = json_object_get(f, "dup_ack_cnt");
		tt->flows[i].dup_ack_cnt = json_is_integer(t) ?
		                           json_integer_value(t) : 0;

		t = json_object_get(f, "retransmit_cnt");
		tt->flows[i].retransmit_cnt = json_is_integer(t) ?
		                              json_integer_value(t) : 0;

		t = json_object_get(f, "ece_cnt");
		tt->flows[i].ece_cnt = json_is_integer(t) ?
		                       json_integer_value(t) : 0;

		t = json_object_get(f, "recent_events");
		tt->flows[i].recent_events = json_is_integer(t) ?
		                             json_integer_value(t) : 0;

		/* TCP health indicator fields (optional) */
		t = json_object_get(f, "health_rtt_hist");
		if (json_is_array(t)) {
			size_t hist_size = json_array_size(t);
			for (size_t h = 0; h < 14 && h < hist_size; h++) {
				json_t *bucket = json_array_get(t, h);
				tt->flows[i].health_rtt_hist[h] = json_is_integer(bucket) ?
				                                  json_integer_value(bucket) : 0;
			}
		} else {
			memset(tt->flows[i].health_rtt_hist, 0,
			       sizeof(tt->flows[i].health_rtt_hist));
		}

		t = json_object_get(f, "health_rtt_samples");
		tt->flows[i].health_rtt_samples = json_is_integer(t) ?
		                                  json_integer_value(t) : 0;

		t = json_object_get(f, "health_status");
		tt->flows[i].health_status = json_is_integer(t) ?
		                             json_integer_value(t) : 0;

		t = json_object_get(f, "health_flags");
		tt->flows[i].health_flags = json_is_integer(t) ?
		                            json_integer_value(t) : 0;

		/* IPG histogram (optional) */
		t = json_object_get(f, "ipg_hist");
		if (json_is_array(t)) {
			size_t hist_size = json_array_size(t);
			for (size_t h = 0; h < 12 && h < hist_size; h++) {
				json_t *bucket = json_array_get(t, h);
				tt->flows[i].ipg_hist[h] = json_is_integer(bucket) ?
				                           json_integer_value(bucket) : 0;
			}
		} else {
			memset(tt->flows[i].ipg_hist, 0,
			       sizeof(tt->flows[i].ipg_hist));
		}

		t = json_object_get(f, "ipg_samples");
		tt->flows[i].ipg_samples = json_is_integer(t) ?
		                           json_integer_value(t) : 0;

		t = json_object_get(f, "ipg_mean_us");
		tt->flows[i].ipg_mean_us = json_is_integer(t) ?
		                           json_integer_value(t) : 0;

		/* Frame size histogram (optional) */
		t = json_object_get(f, "frame_size_hist");
		if (json_is_array(t)) {
			size_t hist_size = json_array_size(t);
			for (size_t h = 0; h < 20 && h < hist_size; h++) {
				json_t *bucket = json_array_get(t, h);
				tt->flows[i].frame_size_hist[h] = json_is_integer(bucket) ?
				                                  json_integer_value(bucket) : 0;
			}
		} else {
			memset(tt->flows[i].frame_size_hist, 0,
			       sizeof(tt->flows[i].frame_size_hist));
		}

		t = json_object_get(f, "frame_size_samples");
		tt->flows[i].frame_size_samples = json_is_integer(t) ?
		                                  json_integer_value(t) : 0;

		t = json_object_get(f, "frame_size_mean");
		tt->flows[i].frame_size_mean = json_is_integer(t) ?
		                               json_integer_value(t) : 0;

		t = json_object_get(f, "frame_size_variance");
		tt->flows[i].frame_size_variance = json_is_integer(t) ?
		                                   json_integer_value(t) : 0;

		t = json_object_get(f, "frame_size_min");
		tt->flows[i].frame_size_min = json_is_integer(t) ?
		                              json_integer_value(t) : 0;

		t = json_object_get(f, "frame_size_max");
		tt->flows[i].frame_size_max = json_is_integer(t) ?
		                              json_integer_value(t) : 0;

		/* PPS histogram (optional) */
		t = json_object_get(f, "pps_hist");
		if (json_is_array(t)) {
			size_t hist_size = json_array_size(t);
			for (size_t h = 0; h < 12 && h < hist_size; h++) {
				json_t *bucket = json_array_get(t, h);
				tt->flows[i].pps_hist[h] = json_is_integer(bucket) ?
				                           json_integer_value(bucket) : 0;
			}
		} else {
			memset(tt->flows[i].pps_hist, 0,
			       sizeof(tt->flows[i].pps_hist));
		}

		t = json_object_get(f, "pps_samples");
		tt->flows[i].pps_samples = json_is_integer(t) ?
		                           json_integer_value(t) : 0;

		t = json_object_get(f, "pps_mean");
		tt->flows[i].pps_mean = json_is_integer(t) ?
		                        json_integer_value(t) : 0;

		t = json_object_get(f, "pps_variance");
		tt->flows[i].pps_variance = json_is_integer(t) ?
		                            json_integer_value(t) : 0;

		/* Video stream fields (optional) */
		t = json_object_get(f, "video_type");
		tt->flows[i].video_type = json_is_integer(t) ?
		                          json_integer_value(t) : 0;

		t = json_object_get(f, "video_codec");
		tt->flows[i].video_codec = json_is_integer(t) ?
		                           json_integer_value(t) : 0;

		t = json_object_get(f, "video_jitter_us");
		tt->flows[i].video_jitter_us = json_is_integer(t) ?
		                               json_integer_value(t) : 0;

		/* Video jitter histogram (optional) */
		t = json_object_get(f, "video_jitter_hist");
		if (json_is_array(t)) {
			size_t hist_size = json_array_size(t);
			for (size_t h = 0; h < 12 && h < hist_size; h++) {
				json_t *bucket = json_array_get(t, h);
				tt->flows[i].video_jitter_hist[h] = json_is_integer(bucket) ?
				                                    json_integer_value(bucket) : 0;
			}
		} else {
			memset(tt->flows[i].video_jitter_hist, 0,
			       sizeof(tt->flows[i].video_jitter_hist));
		}

		t = json_object_get(f, "video_seq_loss");
		tt->flows[i].video_seq_loss = json_is_integer(t) ?
		                              json_integer_value(t) : 0;

		t = json_object_get(f, "video_cc_errors");
		tt->flows[i].video_cc_errors = json_is_integer(t) ?
		                               json_integer_value(t) : 0;

		t = json_object_get(f, "video_ssrc");
		tt->flows[i].video_ssrc = json_is_integer(t) ?
		                          json_integer_value(t) : 0;

		/* Extended video telemetry fields (optional) */
		t = json_object_get(f, "video_codec_source");
		tt->flows[i].video_codec_source = json_is_integer(t) ?
		                                  json_integer_value(t) : 0;

		t = json_object_get(f, "video_width");
		tt->flows[i].video_width = json_is_integer(t) ?
		                           json_integer_value(t) : 0;

		t = json_object_get(f, "video_height");
		tt->flows[i].video_height = json_is_integer(t) ?
		                            json_integer_value(t) : 0;

		t = json_object_get(f, "video_profile");
		tt->flows[i].video_profile = json_is_integer(t) ?
		                             json_integer_value(t) : 0;

		t = json_object_get(f, "video_level");
		tt->flows[i].video_level = json_is_integer(t) ?
		                           json_integer_value(t) : 0;

		t = json_object_get(f, "video_fps_x100");
		tt->flows[i].video_fps_x100 = json_is_integer(t) ?
		                              json_integer_value(t) : 0;

		t = json_object_get(f, "video_bitrate_kbps");
		tt->flows[i].video_bitrate_kbps = json_is_integer(t) ?
		                                  json_integer_value(t) : 0;

		t = json_object_get(f, "video_gop_frames");
		tt->flows[i].video_gop_frames = json_is_integer(t) ?
		                                json_integer_value(t) : 0;

		t = json_object_get(f, "video_keyframes");
		tt->flows[i].video_keyframes = json_is_integer(t) ?
		                               json_integer_value(t) : 0;

		t = json_object_get(f, "video_frames");
		tt->flows[i].video_frames = json_is_integer(t) ?
		                            json_integer_value(t) : 0;

		/* Audio stream fields (optional) */
		t = json_object_get(f, "audio_type");
		tt->flows[i].audio_type = json_is_integer(t) ?
		                          json_integer_value(t) : 0;

		t = json_object_get(f, "audio_codec");
		tt->flows[i].audio_codec = json_is_integer(t) ?
		                           json_integer_value(t) : 0;

		t = json_object_get(f, "audio_sample_rate");
		tt->flows[i].audio_sample_rate = json_is_integer(t) ?
		                                 json_integer_value(t) : 0;

		t = json_object_get(f, "audio_jitter_us");
		tt->flows[i].audio_jitter_us = json_is_integer(t) ?
		                               json_integer_value(t) : 0;

		t = json_object_get(f, "audio_seq_loss");
		tt->flows[i].audio_seq_loss = json_is_integer(t) ?
		                              json_integer_value(t) : 0;

		t = json_object_get(f, "audio_ssrc");
		tt->flows[i].audio_ssrc = json_is_integer(t) ?
		                          json_integer_value(t) : 0;

		t = json_object_get(f, "audio_bitrate_kbps");
		tt->flows[i].audio_bitrate_kbps = json_is_integer(t) ?
		                                  json_integer_value(t) : 0;

		t = json_object_get(f, "sport");
		if (!json_is_integer(t)) {
			goto unpack_fail;
		}
		tt->flows[i].sport = json_integer_value(t);

		t = json_object_get(f, "dport");
		if (!json_is_integer(t)) {
			goto unpack_fail;
		}
		tt->flows[i].dport = json_integer_value(t);

		t = json_object_get(f, "src");
		if (!json_is_string(t)) {
			goto unpack_fail;
		}
		snprintf(tt->flows[i].src, ADDR_LEN, "%s",
		         json_string_value(t));

		t = json_object_get(f, "dst");
		if (!json_is_string(t)) {
			goto unpack_fail;
		}
		snprintf(tt->flows[i].dst, ADDR_LEN, "%s",
		         json_string_value(t));

		t = json_object_get(f, "proto");
		if (!json_is_string(t)) {
			goto unpack_fail;
		}
		snprintf(tt->flows[i].proto, PROTO_LEN, "%s",
		         json_string_value(t));

		t = json_object_get(f, "tclass");
		if (!json_is_string(t)) {
			goto unpack_fail;
		}
		snprintf(tt->flows[i].tclass, TCLASS_LEN, "%s",
		         json_string_value(t));
	}

	*data = tt;
	json_object_clear(params);
	return 0;

unpack_fail:
	free(tt);
	return -1;
}

int jt_toptalk_packer(void *data, char **out)
{
	struct jt_msg_toptalk *tt_msg = data;
	json_t *t = json_object();
	json_t *timestamp = json_object();
	json_t *params = json_object();
	json_t *flows_arr = json_array();
	json_t *flows[MAX_FLOWS];

	assert(tt_msg);

	json_object_set_new(params, "tflows", json_integer(tt_msg->tflows));
	json_object_set_new(params, "tbytes", json_integer(tt_msg->tbytes));
	json_object_set_new(params, "tpackets", json_integer(tt_msg->tpackets));
	json_object_set_new(params, "interval_ns",
	                    json_integer(tt_msg->interval_ns));

	json_object_set_new(timestamp, "tv_sec", json_integer(tt_msg->timestamp.tv_sec));
	json_object_set_new(timestamp, "tv_nsec", json_integer(tt_msg->timestamp.tv_nsec));
	json_object_set_new(params, "timestamp", timestamp);

	/* tt_msg->tflows is the Total flows recorded, not the number of flows
	 * listed in the message, so it will be more than MAX_FLOWS...
	 * So this is wrong >>> assert(tt_msg->tflows <= MAX_FLOWS);
	 */

	const int stop = (tt_msg->tflows < MAX_FLOWS) ?
	                  tt_msg->tflows : MAX_FLOWS;

	for (int i = 0; i < stop; i++) {
		flows[i] = json_object();
		json_object_set_new(flows[i], "bytes",
		                    json_integer(tt_msg->flows[i].bytes));
		json_object_set_new(flows[i], "packets",
		                    json_integer(tt_msg->flows[i].packets));
		json_object_set_new(flows[i], "rtt_us",
		                    json_integer(tt_msg->flows[i].rtt_us));
		json_object_set_new(flows[i], "tcp_state",
		                    json_integer(tt_msg->flows[i].tcp_state));
		json_object_set_new(flows[i], "saw_syn",
		                    json_integer(tt_msg->flows[i].saw_syn));
		/* Window/Congestion tracking fields */
		json_object_set_new(flows[i], "rwnd_bytes",
		                    json_integer(tt_msg->flows[i].rwnd_bytes));
		json_object_set_new(flows[i], "window_scale",
		                    json_integer(tt_msg->flows[i].window_scale));
		json_object_set_new(flows[i], "zero_window_cnt",
		                    json_integer(tt_msg->flows[i].zero_window_cnt));
		json_object_set_new(flows[i], "dup_ack_cnt",
		                    json_integer(tt_msg->flows[i].dup_ack_cnt));
		json_object_set_new(flows[i], "retransmit_cnt",
		                    json_integer(tt_msg->flows[i].retransmit_cnt));
		json_object_set_new(flows[i], "ece_cnt",
		                    json_integer(tt_msg->flows[i].ece_cnt));
		json_object_set_new(flows[i], "recent_events",
		                    json_integer(tt_msg->flows[i].recent_events));
		/* TCP health fields (only include if health status is known) */
		if (tt_msg->flows[i].health_status > 0 ||
		    tt_msg->flows[i].health_rtt_samples > 0) {
			/* Send histogram as JSON array */
			json_t *hist_arr = json_array();
			for (int h = 0; h < 14; h++) {
				json_array_append_new(hist_arr,
				        json_integer(tt_msg->flows[i].health_rtt_hist[h]));
			}
			json_object_set_new(flows[i], "health_rtt_hist", hist_arr);
			json_object_set_new(flows[i], "health_rtt_samples",
			                    json_integer(tt_msg->flows[i].health_rtt_samples));
			json_object_set_new(flows[i], "health_status",
			                    json_integer(tt_msg->flows[i].health_status));
			json_object_set_new(flows[i], "health_flags",
			                    json_integer(tt_msg->flows[i].health_flags));
		}
		/* IPG histogram (for all flows with samples) */
		if (tt_msg->flows[i].ipg_samples > 0) {
			json_t *ipg_hist_arr = json_array();
			for (int h = 0; h < 12; h++) {
				json_array_append_new(ipg_hist_arr,
				        json_integer(tt_msg->flows[i].ipg_hist[h]));
			}
			json_object_set_new(flows[i], "ipg_hist", ipg_hist_arr);
			json_object_set_new(flows[i], "ipg_samples",
			                    json_integer(tt_msg->flows[i].ipg_samples));
			json_object_set_new(flows[i], "ipg_mean_us",
			                    json_integer(tt_msg->flows[i].ipg_mean_us));
		}
		/* Frame size histogram (for all flows with samples) */
		if (tt_msg->flows[i].frame_size_samples > 0) {
			json_t *frame_hist_arr = json_array();
			for (int h = 0; h < 20; h++) {
				json_array_append_new(frame_hist_arr,
				        json_integer(tt_msg->flows[i].frame_size_hist[h]));
			}
			json_object_set_new(flows[i], "frame_size_hist", frame_hist_arr);
			json_object_set_new(flows[i], "frame_size_samples",
			                    json_integer(tt_msg->flows[i].frame_size_samples));
			json_object_set_new(flows[i], "frame_size_mean",
			                    json_integer(tt_msg->flows[i].frame_size_mean));
			json_object_set_new(flows[i], "frame_size_variance",
			                    json_integer(tt_msg->flows[i].frame_size_variance));
			json_object_set_new(flows[i], "frame_size_min",
			                    json_integer(tt_msg->flows[i].frame_size_min));
			json_object_set_new(flows[i], "frame_size_max",
			                    json_integer(tt_msg->flows[i].frame_size_max));
		}
		/* PPS histogram (for all flows with samples) */
		if (tt_msg->flows[i].pps_samples > 0) {
			json_t *pps_hist_arr = json_array();
			for (int h = 0; h < 12; h++) {
				json_array_append_new(pps_hist_arr,
				        json_integer(tt_msg->flows[i].pps_hist[h]));
			}
			json_object_set_new(flows[i], "pps_hist", pps_hist_arr);
			json_object_set_new(flows[i], "pps_samples",
			                    json_integer(tt_msg->flows[i].pps_samples));
			json_object_set_new(flows[i], "pps_mean",
			                    json_integer(tt_msg->flows[i].pps_mean));
			json_object_set_new(flows[i], "pps_variance",
			                    json_integer(tt_msg->flows[i].pps_variance));
		}
		/* Video stream fields (only include if video stream detected) */
		if (tt_msg->flows[i].video_type != 0) {
			json_object_set_new(flows[i], "video_type",
			                    json_integer(tt_msg->flows[i].video_type));
			json_object_set_new(flows[i], "video_codec",
			                    json_integer(tt_msg->flows[i].video_codec));
			json_object_set_new(flows[i], "video_jitter_us",
			                    json_integer(tt_msg->flows[i].video_jitter_us));
			/* Send jitter histogram as JSON array */
			json_t *jitter_hist_arr = json_array();
			for (int h = 0; h < 12; h++) {
				json_array_append_new(jitter_hist_arr,
				        json_integer(tt_msg->flows[i].video_jitter_hist[h]));
			}
			json_object_set_new(flows[i], "video_jitter_hist", jitter_hist_arr);
			json_object_set_new(flows[i], "video_seq_loss",
			                    json_integer(tt_msg->flows[i].video_seq_loss));
			json_object_set_new(flows[i], "video_cc_errors",
			                    json_integer(tt_msg->flows[i].video_cc_errors));
			json_object_set_new(flows[i], "video_ssrc",
			                    json_integer(tt_msg->flows[i].video_ssrc));
			/* Extended video telemetry fields */
			json_object_set_new(flows[i], "video_codec_source",
			                    json_integer(tt_msg->flows[i].video_codec_source));
			json_object_set_new(flows[i], "video_width",
			                    json_integer(tt_msg->flows[i].video_width));
			json_object_set_new(flows[i], "video_height",
			                    json_integer(tt_msg->flows[i].video_height));
			json_object_set_new(flows[i], "video_profile",
			                    json_integer(tt_msg->flows[i].video_profile));
			json_object_set_new(flows[i], "video_level",
			                    json_integer(tt_msg->flows[i].video_level));
			json_object_set_new(flows[i], "video_fps_x100",
			                    json_integer(tt_msg->flows[i].video_fps_x100));
			json_object_set_new(flows[i], "video_bitrate_kbps",
			                    json_integer(tt_msg->flows[i].video_bitrate_kbps));
			json_object_set_new(flows[i], "video_gop_frames",
			                    json_integer(tt_msg->flows[i].video_gop_frames));
			json_object_set_new(flows[i], "video_keyframes",
			                    json_integer(tt_msg->flows[i].video_keyframes));
			json_object_set_new(flows[i], "video_frames",
			                    json_integer(tt_msg->flows[i].video_frames));
		}
		/* Audio stream fields (only include if audio stream detected) */
		if (tt_msg->flows[i].audio_type != 0) {
			json_object_set_new(flows[i], "audio_type",
			                    json_integer(tt_msg->flows[i].audio_type));
			json_object_set_new(flows[i], "audio_codec",
			                    json_integer(tt_msg->flows[i].audio_codec));
			json_object_set_new(flows[i], "audio_sample_rate",
			                    json_integer(tt_msg->flows[i].audio_sample_rate));
			json_object_set_new(flows[i], "audio_jitter_us",
			                    json_integer(tt_msg->flows[i].audio_jitter_us));
			json_object_set_new(flows[i], "audio_seq_loss",
			                    json_integer(tt_msg->flows[i].audio_seq_loss));
			json_object_set_new(flows[i], "audio_ssrc",
			                    json_integer(tt_msg->flows[i].audio_ssrc));
			json_object_set_new(flows[i], "audio_bitrate_kbps",
			                    json_integer(tt_msg->flows[i].audio_bitrate_kbps));
		}
		json_object_set_new(flows[i], "sport",
		                    json_integer(ntohs(tt_msg->flows[i].sport)));
		json_object_set_new(flows[i], "dport",
		                    json_integer(ntohs(tt_msg->flows[i].dport)));
		json_object_set_new(flows[i], "src",
		                    json_string(tt_msg->flows[i].src));
		json_object_set_new(flows[i], "dst",
		                    json_string(tt_msg->flows[i].dst));
		json_object_set_new(flows[i], "proto",
		                    json_string(tt_msg->flows[i].proto));
		json_object_set_new(flows[i], "tclass",
		                    json_string(tt_msg->flows[i].tclass));
		json_array_append(flows_arr, flows[i]);
	}

	json_object_set_new(t, "msg",
	                    json_string(jt_messages[JT_MSG_TOPTALK_V1].key));
	json_object_set(params, "flows", flows_arr);
	json_object_set(t, "p", params);

	*out = json_dumps(t, 0);
	for (int i = 0; i < stop; i++) {
		json_decref(flows[i]);
	}
	json_array_clear(flows_arr);
	json_decref(flows_arr);
	json_object_clear(params);
	json_decref(params);
	/* timestamp ownership transferred to params via json_object_set_new() */
	json_object_clear(t);
	json_decref(t);
	return 0;
}

int jt_toptalk_free(void *data)
{
	struct jt_msg_toptalk *t = (struct jt_msg_toptalk *)data;
	free(t);
	return 0;
}
