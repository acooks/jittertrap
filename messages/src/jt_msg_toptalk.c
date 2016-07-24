#define _GNU_SOURCE
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <pthread.h>

#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <jansson.h>
#include <inttypes.h>

#include "jt_message_types.h"
#include "jt_messages.h"
#include "jt_msg_toptalk.h"

static const char *tt_test_msg =
    "{\"msg\":\"tt\","
    " \"p\":{\"tflows\":5, \"tbytes\": 9999, \"tpackets\": 888,"
    " \"interval_ns\": 123,"
    " \"flows\": ["
    "{\"src\":\"192.168.0.1\", \"dst\": \"192.168.0.2\", \"sport\":32000, \"dport\":32000, \"proto\": \"udp\", \"bytes\":100, \"packets\":10},"
    "{\"src\":\"192.168.0.1\", \"dst\": \"192.168.0.2\", \"sport\":32001, \"dport\":32001, \"proto\": \"udp\", \"bytes\":100, \"packets\":10},"
    "{\"src\":\"192.168.0.1\", \"dst\": \"192.168.0.2\", \"sport\":32002, \"dport\":32002, \"proto\": \"udp\", \"bytes\":100, \"packets\":10},"
    "{\"src\":\"192.168.0.1\", \"dst\": \"192.168.0.2\", \"sport\":32003, \"dport\":32003, \"proto\": \"udp\", \"bytes\":100, \"packets\":10},"
    "{\"src\":\"192.168.0.1\", \"dst\": \"192.168.0.2\", \"sport\":32004, \"dport\":32004, \"proto\": \"udp\", \"bytes\":100, \"packets\":10}"
    "]}}";

const char* jt_toptalk_test_msg_get() { return tt_test_msg; }

int jt_toptalk_printer(void *data)
{
	struct jt_msg_toptalk *t = (struct jt_msg_toptalk*)data;

	printf("\r fc:%"PRId32", b: %"PRId32", p:%"PRId32,
	       t->tflows, t->tbytes, t->tpackets);
	return 0;
}

int jt_toptalk_unpacker(json_t *root, void **data)
{
	json_t *params;
	json_t *t, *flows;

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
	json_t *params = json_object();
	json_t *flows_arr = json_array();
	json_t *flows[MAX_FLOWS];

	json_object_set_new(params, "tflows", json_integer(tt_msg->tflows));
	json_object_set_new(params, "tbytes", json_integer(tt_msg->tbytes));
	json_object_set_new(params, "tpackets", json_integer(tt_msg->tpackets));
	json_object_set_new(params, "interval_ns",
	                    json_integer(tt_msg->interval_ns));

	assert(tt_msg);

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
		json_object_set_new(flows[i], "sport",
		                    json_integer(tt_msg->flows[i].sport));
		json_object_set_new(flows[i], "dport",
		                    json_integer(tt_msg->flows[i].dport));
		json_object_set_new(flows[i], "src",
		                    json_string(tt_msg->flows[i].src));
		json_object_set_new(flows[i], "dst",
		                    json_string(tt_msg->flows[i].dst));
		json_object_set_new(flows[i], "proto",
		                    json_string(tt_msg->flows[i].proto));
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
