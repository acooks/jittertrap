#define _POSIX_C_SOURCE 200809L
#include <time.h>

#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <jansson.h>
#include <inttypes.h>

#include "jt_message_types.h"
#include "jt_messages.h"
#include "jt_msg_pgap_distr.h"

/* FIXME: incomplete. needs min, max, mean for bytes, packets, pgaps */
static const char *jt_pgap_distr_test_msg =
    "{\"msg\":\"pgap_distr\", \"p\":{\"iface\": \"em1\","
    "\"t\": {\"tv_sec\":0, \"tv_nsec\":0}, "
    "\"ival_ns\": 0, "
    "\"distr\": "
    "[ 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,"
    " 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,"
    " 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,"
    " 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,"
    " 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,"
    " 50,  1,  2,  3,  4,  5,  6,  7,  8,  9,"
    " 60,  1,  2,  3,  4,  5,  6,  7,  8,  9,"
    " 70,  1,  2,  3,  4,  5,  6,  7,  8,  9,"
    " 80,  1,  2,  3,  4,  5,  6,  7,  8,  9,"
    " 90,  1,  2,  3,  4,  5,  6,  7,  8,  9,"
    "100,  1,  2,  3,  4,  5,  6,  7,  8,  9,"
    "110,  1,  2,  3,  4,  5,  6,  7,  8,  9,"
    "120,  1,  2,  3,  4,  5,  6,  7,  8,  9,"
    "130,  1,  2,  3,  4,  5,  6,  7,  8,  9,"
    "140,  1,  2,  3,  4,  5,  6,  7,  8,  9,"
    "150,  1,  2,  3,  4,  5,  6,  7,  8,  9,"
    "160,  1,  2,  3,  4,  5,  6,  7,  8,  9,"
    "170,  1,  2,  3,  4,  5,  6,  7,  8,  9,"
    "180,  1,  2,  3,  4,  5,  6,  7,  8,  9,"
    "190,  1,  2,  3,  4,  5,  6,  7,  8,  9]"
    "}}";

const char *jt_pgap_distr_test_msg_get() { return jt_pgap_distr_test_msg; }

int jt_pgap_distr_free(void *data)
{
	struct jt_msg_pgap_distr *d = (struct jt_msg_pgap_distr *)data;
	free(d);
	return 0;
}

int jt_pgap_distr_printer(void *data)
{
	struct jt_msg_pgap_distr *d = (struct jt_msg_pgap_distr *)data;

	for (int i = 0; i < PGAP_DISTR_BINS; i++) {
		printf("%d ",d->pgap_distr[i]);
	}
	printf("\n");
	return 0;
}

int jt_pgap_distr_unpacker(json_t *root, void **data)
{
	json_t *params;
	json_t *iface, *json_distr;

	struct jt_msg_pgap_distr *distr;

	params = json_object_get(root, "p");
	assert(params);
	assert(JSON_OBJECT == json_typeof(params));
	assert(0 < json_object_size(params));

	distr = malloc(sizeof(struct jt_msg_pgap_distr));

	/* FIXME: this json_get_object() inteface sucks bricks,
	 * but the unpack API doesn't seem to work right now (jansson-2.7). :(
	 */
	iface = json_object_get(params, "iface");
	if (!json_is_string(iface)) {
		goto unpack_fail;
	}
	snprintf(distr->iface, MAX_IFACE_LEN, "%s", json_string_value(iface));

	json_distr = json_object_get(params, "distr");
	if (!json_is_array(json_distr)) {
		goto unpack_fail;
	}

	int len = json_array_size(json_distr);

	if (len != PGAP_DISTR_BINS) {
		printf("json pgap distribution has %d samples. %d expected\n",
		       len, PGAP_DISTR_BINS);
	}

	json_t *t;

	for (int i = 0; i < len; i++) {
		t = json_array_get(json_distr, i);
		if (!json_is_integer(t)) {
			goto unpack_fail;
		}
		distr->pgap_distr[i] = json_integer_value(t);
	}

	t = json_object_get(params, "ival_ns");
        if (!json_is_integer(t)) {
                goto unpack_fail;
        }
        distr->interval_ns = json_integer_value(t);

        /* get the message timestamp */
        json_t *mts = json_object_get(params, "t");
        if (!json_is_object(mts)) {
                goto unpack_fail;
        }

        t = json_object_get(mts, "tv_sec");
        if (!json_is_integer(t)) {
                goto unpack_fail;
        }
        distr->timestamp.tv_sec = json_integer_value(t);

        t = json_object_get(mts, "tv_nsec");
        if (!json_is_integer(t)) {
                goto unpack_fail;
        }
        distr->timestamp.tv_nsec = json_integer_value(t);

	*data = distr;
	json_object_clear(params);
	return 0;

unpack_fail:
	free(distr);
	return -1;
}

int jt_pgap_distr_packer(void *data, char **out)
{
	struct timespec mts; /* message timestamp */
	struct jt_msg_pgap_distr *msg = data;
	json_t *t = json_object();
	json_t *distr = json_array();
	json_t *params = json_object();
        json_t *jmts = json_object();

	json_object_set_new(params, "iface", json_string(msg->iface));
	json_object_set_new(params, "ival_ns",
	                    json_integer(msg->interval_ns));

	for (int i = 0; i < PGAP_DISTR_BINS; i++) {
		json_array_append_new(distr, json_integer(msg->pgap_distr[i]));
	}

	json_object_set(params, "distr", distr);
	json_object_set_new(t, "msg",
	                    json_string(jt_messages[JT_MSG_PGAP_DISTR_V1].key));
	json_object_set(t, "p", params);

	/* timestamp the new message */
	clock_gettime(CLOCK_MONOTONIC, &mts);
	json_object_set_new(jmts, "tv_sec", json_integer(mts.tv_sec));
	json_object_set_new(jmts, "tv_nsec", json_integer(mts.tv_nsec));
	json_object_set_new(params, "t", jmts);

	*out = json_dumps(t, 0);
	json_object_clear(distr);
	json_decref(distr);
	json_object_clear(params);
	json_decref(params);
	json_object_clear(t);
	json_decref(t);
	return 0;
}
