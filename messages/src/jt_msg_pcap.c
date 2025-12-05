/*
 * jt_msg_pcap.c - PCAP buffer message pack/unpack implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <jansson.h>

#include "jt_message_types.h"
#include "jt_messages.h"
#include "jt_msg_pcap.h"

/* ============== PCAP Config Message ============== */

static const char *jt_pcap_config_test_msg =
    "{\"msg\":\"pcap_config\", \"p\":{"
    "\"enabled\":1,"
    "\"max_memory_mb\":256,"
    "\"duration_sec\":30,"
    "\"pre_trigger_sec\":25,"
    "\"post_trigger_sec\":5"
    "}}";

const char *jt_pcap_config_test_msg_get(void)
{
	return jt_pcap_config_test_msg;
}

int jt_pcap_config_free(void *data)
{
	struct jt_msg_pcap_config *cfg = data;
	free(cfg);
	return 0;
}

int jt_pcap_config_printer(void *data, char *out, int len)
{
	struct jt_msg_pcap_config *cfg = data;
	snprintf(out, len,
	         "PCAP Config: enabled=%u, max_mem=%uMB, duration=%us, "
	         "pre=%us, post=%us",
	         cfg->enabled, cfg->max_memory_mb, cfg->duration_sec,
	         cfg->pre_trigger_sec, cfg->post_trigger_sec);
	return 0;
}

int jt_pcap_config_unpacker(json_t *root, void **data)
{
	json_t *params;
	json_t *val;
	struct jt_msg_pcap_config *cfg;

	params = json_object_get(root, "p");
	if (!params)
		return -1;

	cfg = calloc(1, sizeof(struct jt_msg_pcap_config));
	if (!cfg)
		return -1;

	val = json_object_get(params, "enabled");
	if (val && json_is_integer(val))
		cfg->enabled = json_integer_value(val);

	val = json_object_get(params, "max_memory_mb");
	if (val && json_is_integer(val))
		cfg->max_memory_mb = json_integer_value(val);

	val = json_object_get(params, "duration_sec");
	if (val && json_is_integer(val))
		cfg->duration_sec = json_integer_value(val);

	val = json_object_get(params, "pre_trigger_sec");
	if (val && json_is_integer(val))
		cfg->pre_trigger_sec = json_integer_value(val);

	val = json_object_get(params, "post_trigger_sec");
	if (val && json_is_integer(val))
		cfg->post_trigger_sec = json_integer_value(val);

	*data = cfg;
	return 0;
}

int jt_pcap_config_packer(void *data, char **out)
{
	struct jt_msg_pcap_config *cfg = data;
	json_t *params = json_object();
	json_t *msg = json_object();

	json_object_set_new(msg, "msg",
	                    json_string(jt_messages[JT_MSG_PCAP_CONFIG_V1].key));

	json_object_set_new(params, "enabled", json_integer(cfg->enabled));
	json_object_set_new(params, "max_memory_mb",
	                    json_integer(cfg->max_memory_mb));
	json_object_set_new(params, "duration_sec",
	                    json_integer(cfg->duration_sec));
	json_object_set_new(params, "pre_trigger_sec",
	                    json_integer(cfg->pre_trigger_sec));
	json_object_set_new(params, "post_trigger_sec",
	                    json_integer(cfg->post_trigger_sec));

	json_object_set(msg, "p", params);
	*out = json_dumps(msg, 0);

	json_object_clear(params);
	json_decref(params);
	json_object_clear(msg);
	json_decref(msg);

	return 0;
}

/* ============== PCAP Status Message ============== */

static const char *jt_pcap_status_test_msg =
    "{\"msg\":\"pcap_status\", \"p\":{"
    "\"state\":1,"
    "\"total_packets\":45000,"
    "\"total_bytes\":23000000,"
    "\"dropped_packets\":0,"
    "\"current_memory_bytes\":134217728,"
    "\"buffer_percent\":85,"
    "\"oldest_age_sec\":28"
    "}}";

const char *jt_pcap_status_test_msg_get(void)
{
	return jt_pcap_status_test_msg;
}

int jt_pcap_status_free(void *data)
{
	struct jt_msg_pcap_status *status = data;
	free(status);
	return 0;
}

int jt_pcap_status_printer(void *data, char *out, int len)
{
	struct jt_msg_pcap_status *status = data;
	snprintf(out, len,
	         "PCAP Status: state=%u, pkts=%lu, bytes=%lu, "
	         "mem=%luB, buffer=%u%%",
	         status->state,
	         (unsigned long)status->total_packets,
	         (unsigned long)status->total_bytes,
	         (unsigned long)status->current_memory_bytes, status->buffer_percent);
	return 0;
}

int jt_pcap_status_unpacker(json_t *root, void **data)
{
	json_t *params;
	json_t *val;
	struct jt_msg_pcap_status *status;

	params = json_object_get(root, "p");
	if (!params)
		return -1;

	status = calloc(1, sizeof(struct jt_msg_pcap_status));
	if (!status)
		return -1;

	val = json_object_get(params, "state");
	if (val && json_is_integer(val))
		status->state = json_integer_value(val);

	val = json_object_get(params, "total_packets");
	if (val && json_is_integer(val))
		status->total_packets = json_integer_value(val);

	val = json_object_get(params, "total_bytes");
	if (val && json_is_integer(val))
		status->total_bytes = json_integer_value(val);

	val = json_object_get(params, "dropped_packets");
	if (val && json_is_integer(val))
		status->dropped_packets = json_integer_value(val);

	val = json_object_get(params, "current_memory_bytes");
	if (val && json_is_integer(val))
		status->current_memory_bytes = json_integer_value(val);

	val = json_object_get(params, "buffer_percent");
	if (val && json_is_integer(val))
		status->buffer_percent = json_integer_value(val);

	val = json_object_get(params, "oldest_age_sec");
	if (val && json_is_integer(val))
		status->oldest_age_sec = json_integer_value(val);

	*data = status;
	return 0;
}

int jt_pcap_status_packer(void *data, char **out)
{
	struct jt_msg_pcap_status *status = data;
	json_t *params = json_object();
	json_t *msg = json_object();

	json_object_set_new(msg, "msg",
	                    json_string(jt_messages[JT_MSG_PCAP_STATUS_V1].key));

	json_object_set_new(params, "state", json_integer(status->state));
	json_object_set_new(params, "total_packets",
	                    json_integer(status->total_packets));
	json_object_set_new(params, "total_bytes",
	                    json_integer(status->total_bytes));
	json_object_set_new(params, "dropped_packets",
	                    json_integer(status->dropped_packets));
	json_object_set_new(params, "current_memory_bytes",
	                    json_integer(status->current_memory_bytes));
	json_object_set_new(params, "buffer_percent",
	                    json_integer(status->buffer_percent));
	json_object_set_new(params, "oldest_age_sec",
	                    json_integer(status->oldest_age_sec));

	json_object_set(msg, "p", params);
	*out = json_dumps(msg, 0);

	json_object_clear(params);
	json_decref(params);
	json_object_clear(msg);
	json_decref(msg);

	return 0;
}

/* ============== PCAP Trigger Message ============== */

static const char *jt_pcap_trigger_test_msg =
    "{\"msg\":\"pcap_trigger\", \"p\":{\"reason\":\"Manual trigger\"}}";

const char *jt_pcap_trigger_test_msg_get(void)
{
	return jt_pcap_trigger_test_msg;
}

int jt_pcap_trigger_free(void *data)
{
	struct jt_msg_pcap_trigger *trigger = data;
	free(trigger);
	return 0;
}

int jt_pcap_trigger_printer(void *data, char *out, int len)
{
	struct jt_msg_pcap_trigger *trigger = data;
	snprintf(out, len, "PCAP Trigger: reason=%s", trigger->reason);
	return 0;
}

int jt_pcap_trigger_unpacker(json_t *root, void **data)
{
	json_t *params;
	json_t *val;
	struct jt_msg_pcap_trigger *trigger;

	params = json_object_get(root, "p");
	if (!params)
		return -1;

	trigger = calloc(1, sizeof(struct jt_msg_pcap_trigger));
	if (!trigger)
		return -1;

	val = json_object_get(params, "reason");
	if (val && json_is_string(val)) {
		snprintf(trigger->reason, sizeof(trigger->reason),
		         "%s", json_string_value(val));
	}

	*data = trigger;
	return 0;
}

int jt_pcap_trigger_packer(void *data, char **out)
{
	struct jt_msg_pcap_trigger *trigger = data;
	json_t *params = json_object();
	json_t *msg = json_object();

	json_object_set_new(msg, "msg",
	                    json_string(jt_messages[JT_MSG_PCAP_TRIGGER_V1].key));

	json_object_set_new(params, "reason", json_string(trigger->reason));

	json_object_set(msg, "p", params);
	*out = json_dumps(msg, 0);

	json_object_clear(params);
	json_decref(params);
	json_object_clear(msg);
	json_decref(msg);

	return 0;
}

/* ============== PCAP Ready Message ============== */

static const char *jt_pcap_ready_test_msg =
    "{\"msg\":\"pcap_ready\", \"p\":{"
    "\"filename\":\"/pcap/capture_1701234567.pcap\","
    "\"file_size\":23456789,"
    "\"packet_count\":45000,"
    "\"duration_sec\":30"
    "}}";

const char *jt_pcap_ready_test_msg_get(void)
{
	return jt_pcap_ready_test_msg;
}

int jt_pcap_ready_free(void *data)
{
	struct jt_msg_pcap_ready *ready = data;
	free(ready);
	return 0;
}

int jt_pcap_ready_printer(void *data, char *out, int len)
{
	struct jt_msg_pcap_ready *ready = data;
	snprintf(out, len,
	         "PCAP Ready: file=%s, size=%lu, pkts=%u, duration=%us",
	         ready->filename, (unsigned long)ready->file_size,
	         ready->packet_count, ready->duration_sec);
	return 0;
}

int jt_pcap_ready_unpacker(json_t *root, void **data)
{
	json_t *params;
	json_t *val;
	struct jt_msg_pcap_ready *ready;

	params = json_object_get(root, "p");
	if (!params)
		return -1;

	ready = calloc(1, sizeof(struct jt_msg_pcap_ready));
	if (!ready)
		return -1;

	val = json_object_get(params, "filename");
	if (val && json_is_string(val)) {
		snprintf(ready->filename, sizeof(ready->filename),
		         "%s", json_string_value(val));
	}

	val = json_object_get(params, "file_size");
	if (val && json_is_integer(val))
		ready->file_size = json_integer_value(val);

	val = json_object_get(params, "packet_count");
	if (val && json_is_integer(val))
		ready->packet_count = json_integer_value(val);

	val = json_object_get(params, "duration_sec");
	if (val && json_is_integer(val))
		ready->duration_sec = json_integer_value(val);

	*data = ready;
	return 0;
}

int jt_pcap_ready_packer(void *data, char **out)
{
	struct jt_msg_pcap_ready *ready = data;
	json_t *params = json_object();
	json_t *msg = json_object();

	json_object_set_new(msg, "msg",
	                    json_string(jt_messages[JT_MSG_PCAP_READY_V1].key));

	json_object_set_new(params, "filename", json_string(ready->filename));
	json_object_set_new(params, "file_size",
	                    json_integer(ready->file_size));
	json_object_set_new(params, "packet_count",
	                    json_integer(ready->packet_count));
	json_object_set_new(params, "duration_sec",
	                    json_integer(ready->duration_sec));

	json_object_set(msg, "p", params);
	*out = json_dumps(msg, 0);

	json_object_clear(params);
	json_decref(params);
	json_object_clear(msg);
	json_decref(msg);

	return 0;
}
