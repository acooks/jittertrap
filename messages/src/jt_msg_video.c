/*
 * jt_msg_video.c - WebRTC video playback message handlers
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <jansson.h>

#include "jt_message_types.h"
#include "jt_messages.h"
#include "jt_msg_video.h"

/* Test messages */
static const char *video_error_test_msg =
    "{\"msg\":\"video_error\","
    " \"p\":{\"code\":\"not_available\","
    " \"message\":\"Video playback not available\"}}";

const char *jt_video_error_test_msg_get(void) { return video_error_test_msg; }

/* ========== video_error ========== */

int jt_video_error_packer(void *data, char **out)
{
	struct jt_msg_video_error *msg = data;
	json_t *t = json_object();
	json_t *params = json_object();

	assert(msg);

	json_object_set_new(params, "code", json_string(msg->code));
	json_object_set_new(params, "message", json_string(msg->message));

	json_object_set_new(t, "msg",
	                    json_string(jt_messages[JT_MSG_VIDEO_ERROR_V1].key));
	json_object_set(t, "p", params);

	*out = json_dumps(t, 0);

	json_object_clear(params);
	json_decref(params);
	json_object_clear(t);
	json_decref(t);

	return 0;
}

int jt_video_error_unpacker(json_t *root, void **data)
{
	json_t *params, *t;
	struct jt_msg_video_error *msg;

	params = json_object_get(root, "p");
	if (!params || JSON_OBJECT != json_typeof(params))
		return -1;

	msg = malloc(sizeof(struct jt_msg_video_error));
	if (!msg)
		return -1;

	memset(msg, 0, sizeof(*msg));

	t = json_object_get(params, "code");
	if (json_is_string(t)) {
		snprintf(msg->code, sizeof(msg->code), "%s", json_string_value(t));
	}

	t = json_object_get(params, "message");
	if (json_is_string(t)) {
		snprintf(msg->message, sizeof(msg->message), "%s",
		         json_string_value(t));
	}

	*data = msg;
	json_object_clear(params);
	return 0;
}

int jt_video_error_printer(void *data, char *out, int len)
{
	struct jt_msg_video_error *msg = data;
	snprintf(out, len, "video_error: code=%s msg=%s", msg->code, msg->message);
	return 0;
}

int jt_video_error_free(void *data)
{
	free(data);
	return 0;
}

/* ========== WebRTC messages ========== */

static const char *webrtc_offer_test_msg =
    "{\"msg\":\"webrtc_offer\","
    " \"p\":{\"fkey\":\"100000000/192.168.1.1/5004/192.168.1.2/5004/UDP/__\","
    " \"ssrc\":12345678, \"codec\":0, \"sdp\":\"v=0\\r\\no=- 0 0 IN IP4 127.0.0.1\\r\\n\"}}";

static const char *webrtc_answer_test_msg =
    "{\"msg\":\"webrtc_answer\","
    " \"p\":{\"viewer_id\":1, \"sdp\":\"v=0\\r\\no=- 0 0 IN IP4 127.0.0.1\\r\\n\"}}";

static const char *webrtc_ice_test_msg =
    "{\"msg\":\"webrtc_ice\","
    " \"p\":{\"viewer_id\":1, \"candidate\":\"candidate:1 1 UDP 2122252543 192.168.1.1 50000 typ host\","
    " \"mid\":\"0\"}}";

static const char *webrtc_stop_test_msg =
    "{\"msg\":\"webrtc_stop\", \"p\":{\"viewer_id\":1}}";

static const char *webrtc_status_test_msg =
    "{\"msg\":\"webrtc_status\","
    " \"p\":{\"viewer_id\":1, \"active\":1, \"packets_sent\":1000, \"bytes_sent\":1500000}}";

const char *jt_webrtc_offer_test_msg_get(void) { return webrtc_offer_test_msg; }
const char *jt_webrtc_answer_test_msg_get(void) { return webrtc_answer_test_msg; }
const char *jt_webrtc_ice_test_msg_get(void) { return webrtc_ice_test_msg; }
const char *jt_webrtc_stop_test_msg_get(void) { return webrtc_stop_test_msg; }
const char *jt_webrtc_status_test_msg_get(void) { return webrtc_status_test_msg; }

/* ========== webrtc_offer ========== */

int jt_webrtc_offer_packer(void *data, char **out)
{
	struct jt_msg_webrtc_offer *msg = data;
	json_t *t = json_object();
	json_t *params = json_object();

	assert(msg);

	json_object_set_new(params, "fkey", json_string(msg->fkey));
	json_object_set_new(params, "ssrc", json_integer(msg->ssrc));
	json_object_set_new(params, "codec", json_integer(msg->codec));
	json_object_set_new(params, "sdp", json_string(msg->sdp));

	json_object_set_new(t, "msg",
	                    json_string(jt_messages[JT_MSG_WEBRTC_OFFER_V1].key));
	json_object_set(t, "p", params);

	*out = json_dumps(t, 0);

	json_object_clear(params);
	json_decref(params);
	json_object_clear(t);
	json_decref(t);

	return 0;
}

int jt_webrtc_offer_unpacker(json_t *root, void **data)
{
	json_t *params, *t;
	struct jt_msg_webrtc_offer *msg;

	params = json_object_get(root, "p");
	if (!params || JSON_OBJECT != json_typeof(params))
		return -1;

	msg = malloc(sizeof(struct jt_msg_webrtc_offer));
	if (!msg)
		return -1;

	memset(msg, 0, sizeof(*msg));

	t = json_object_get(params, "fkey");
	if (json_is_string(t)) {
		snprintf(msg->fkey, sizeof(msg->fkey), "%s", json_string_value(t));
	}

	t = json_object_get(params, "ssrc");
	if (json_is_integer(t)) {
		msg->ssrc = json_integer_value(t);
	}

	t = json_object_get(params, "codec");
	if (json_is_integer(t)) {
		msg->codec = json_integer_value(t);
	}

	t = json_object_get(params, "sdp");
	if (json_is_string(t)) {
		snprintf(msg->sdp, sizeof(msg->sdp), "%s", json_string_value(t));
	}

	*data = msg;
	json_object_clear(params);
	return 0;
}

int jt_webrtc_offer_printer(void *data, char *out, int len)
{
	struct jt_msg_webrtc_offer *msg = data;
	snprintf(out, len, "webrtc_offer: fkey=%s ssrc=%u codec=%d",
	         msg->fkey, msg->ssrc, msg->codec);
	return 0;
}

int jt_webrtc_offer_free(void *data)
{
	free(data);
	return 0;
}

/* ========== webrtc_answer ========== */

int jt_webrtc_answer_packer(void *data, char **out)
{
	struct jt_msg_webrtc_answer *msg = data;
	json_t *t = json_object();
	json_t *params = json_object();

	assert(msg);

	json_object_set_new(params, "viewer_id", json_integer(msg->viewer_id));
	json_object_set_new(params, "sdp", json_string(msg->sdp));

	json_object_set_new(t, "msg",
	                    json_string(jt_messages[JT_MSG_WEBRTC_ANSWER_V1].key));
	json_object_set(t, "p", params);

	*out = json_dumps(t, 0);

	json_object_clear(params);
	json_decref(params);
	json_object_clear(t);
	json_decref(t);

	return 0;
}

int jt_webrtc_answer_unpacker(json_t *root, void **data)
{
	json_t *params, *t;
	struct jt_msg_webrtc_answer *msg;

	params = json_object_get(root, "p");
	if (!params || JSON_OBJECT != json_typeof(params))
		return -1;

	msg = malloc(sizeof(struct jt_msg_webrtc_answer));
	if (!msg)
		return -1;

	memset(msg, 0, sizeof(*msg));

	t = json_object_get(params, "viewer_id");
	if (json_is_integer(t)) {
		msg->viewer_id = json_integer_value(t);
	}

	t = json_object_get(params, "sdp");
	if (json_is_string(t)) {
		snprintf(msg->sdp, sizeof(msg->sdp), "%s", json_string_value(t));
	}

	*data = msg;
	json_object_clear(params);
	return 0;
}

int jt_webrtc_answer_printer(void *data, char *out, int len)
{
	struct jt_msg_webrtc_answer *msg = data;
	snprintf(out, len, "webrtc_answer: viewer_id=%d sdp_len=%zu",
	         msg->viewer_id, strlen(msg->sdp));
	return 0;
}

int jt_webrtc_answer_free(void *data)
{
	free(data);
	return 0;
}

/* ========== webrtc_ice ========== */

int jt_webrtc_ice_packer(void *data, char **out)
{
	struct jt_msg_webrtc_ice *msg = data;
	json_t *t = json_object();
	json_t *params = json_object();

	assert(msg);

	json_object_set_new(params, "viewer_id", json_integer(msg->viewer_id));
	json_object_set_new(params, "candidate", json_string(msg->candidate));
	json_object_set_new(params, "mid", json_string(msg->mid));

	json_object_set_new(t, "msg",
	                    json_string(jt_messages[JT_MSG_WEBRTC_ICE_V1].key));
	json_object_set(t, "p", params);

	*out = json_dumps(t, 0);

	json_object_clear(params);
	json_decref(params);
	json_object_clear(t);
	json_decref(t);

	return 0;
}

int jt_webrtc_ice_unpacker(json_t *root, void **data)
{
	json_t *params, *t;
	struct jt_msg_webrtc_ice *msg;

	params = json_object_get(root, "p");
	if (!params || JSON_OBJECT != json_typeof(params))
		return -1;

	msg = malloc(sizeof(struct jt_msg_webrtc_ice));
	if (!msg)
		return -1;

	memset(msg, 0, sizeof(*msg));

	t = json_object_get(params, "viewer_id");
	if (json_is_integer(t)) {
		msg->viewer_id = json_integer_value(t);
	}

	t = json_object_get(params, "candidate");
	if (json_is_string(t)) {
		snprintf(msg->candidate, sizeof(msg->candidate), "%s",
		         json_string_value(t));
	}

	t = json_object_get(params, "mid");
	if (json_is_string(t)) {
		snprintf(msg->mid, sizeof(msg->mid), "%s", json_string_value(t));
	}

	*data = msg;
	json_object_clear(params);
	return 0;
}

int jt_webrtc_ice_printer(void *data, char *out, int len)
{
	struct jt_msg_webrtc_ice *msg = data;
	snprintf(out, len, "webrtc_ice: viewer_id=%d mid=%s",
	         msg->viewer_id, msg->mid);
	return 0;
}

int jt_webrtc_ice_free(void *data)
{
	free(data);
	return 0;
}

/* ========== webrtc_stop ========== */

int jt_webrtc_stop_packer(void *data, char **out)
{
	struct jt_msg_webrtc_stop *msg = data;
	json_t *t = json_object();
	json_t *params = json_object();

	assert(msg);

	json_object_set_new(params, "viewer_id", json_integer(msg->viewer_id));

	json_object_set_new(t, "msg",
	                    json_string(jt_messages[JT_MSG_WEBRTC_STOP_V1].key));
	json_object_set(t, "p", params);

	*out = json_dumps(t, 0);

	json_object_clear(params);
	json_decref(params);
	json_object_clear(t);
	json_decref(t);

	return 0;
}

int jt_webrtc_stop_unpacker(json_t *root, void **data)
{
	json_t *params, *t;
	struct jt_msg_webrtc_stop *msg;

	params = json_object_get(root, "p");
	if (!params || JSON_OBJECT != json_typeof(params))
		return -1;

	msg = malloc(sizeof(struct jt_msg_webrtc_stop));
	if (!msg)
		return -1;

	memset(msg, 0, sizeof(*msg));

	t = json_object_get(params, "viewer_id");
	if (json_is_integer(t)) {
		msg->viewer_id = json_integer_value(t);
	}

	*data = msg;
	json_object_clear(params);
	return 0;
}

int jt_webrtc_stop_printer(void *data, char *out, int len)
{
	struct jt_msg_webrtc_stop *msg = data;
	snprintf(out, len, "webrtc_stop: viewer_id=%d", msg->viewer_id);
	return 0;
}

int jt_webrtc_stop_free(void *data)
{
	free(data);
	return 0;
}

/* ========== webrtc_status ========== */

int jt_webrtc_status_packer(void *data, char **out)
{
	struct jt_msg_webrtc_status *msg = data;
	json_t *t = json_object();
	json_t *params = json_object();

	assert(msg);

	json_object_set_new(params, "viewer_id", json_integer(msg->viewer_id));
	json_object_set_new(params, "active", json_integer(msg->active));
	json_object_set_new(params, "waiting_for_keyframe",
	                    json_integer(msg->waiting_for_keyframe));
	json_object_set_new(params, "packets_sent",
	                    json_integer(msg->packets_sent));
	json_object_set_new(params, "bytes_sent",
	                    json_integer(msg->bytes_sent));

	json_object_set_new(t, "msg",
	                    json_string(jt_messages[JT_MSG_WEBRTC_STATUS_V1].key));
	json_object_set(t, "p", params);

	*out = json_dumps(t, 0);

	json_object_clear(params);
	json_decref(params);
	json_object_clear(t);
	json_decref(t);

	return 0;
}

int jt_webrtc_status_unpacker(json_t *root, void **data)
{
	json_t *params, *t;
	struct jt_msg_webrtc_status *msg;

	params = json_object_get(root, "p");
	if (!params || JSON_OBJECT != json_typeof(params))
		return -1;

	msg = malloc(sizeof(struct jt_msg_webrtc_status));
	if (!msg)
		return -1;

	memset(msg, 0, sizeof(*msg));

	t = json_object_get(params, "viewer_id");
	if (json_is_integer(t)) {
		msg->viewer_id = json_integer_value(t);
	}

	t = json_object_get(params, "active");
	if (json_is_integer(t)) {
		msg->active = json_integer_value(t);
	}

	t = json_object_get(params, "waiting_for_keyframe");
	if (json_is_integer(t)) {
		msg->waiting_for_keyframe = json_integer_value(t);
	}

	t = json_object_get(params, "packets_sent");
	if (json_is_integer(t)) {
		msg->packets_sent = json_integer_value(t);
	}

	t = json_object_get(params, "bytes_sent");
	if (json_is_integer(t)) {
		msg->bytes_sent = json_integer_value(t);
	}

	*data = msg;
	json_object_clear(params);
	return 0;
}

int jt_webrtc_status_printer(void *data, char *out, int len)
{
	struct jt_msg_webrtc_status *msg = data;
	snprintf(out, len, "webrtc_status: viewer_id=%d active=%u waiting=%u pkts=%llu",
	         msg->viewer_id, msg->active, msg->waiting_for_keyframe,
	         (unsigned long long)msg->packets_sent);
	return 0;
}

int jt_webrtc_status_free(void *data)
{
	free(data);
	return 0;
}
