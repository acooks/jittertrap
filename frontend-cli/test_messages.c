#include <string.h>
#include <assert.h>
#include <jansson.h>

#include "jt_message_types.h"
#include "jt_messages.h"

#include "jt_msg_stats.h"
#include "jt_msg_list_ifaces.h"
#include "jt_msg_select_iface.h"
#include "jt_msg_netem_params.h"
#include "jt_msg_sample_period.h"
#include "jt_msg_get_netem.h"
#include "jt_msg_set_netem.h"

static int test_unpack_pack_unpack(int msg_id)
{
	json_error_t err;
	json_t *token;
	void *data;
	const struct jt_msg_type *msg_type;
	int error;

	msg_type = &jt_messages[msg_id];
	assert(msg_type);
	assert(msg_type->type);

	const char *test_msg = msg_type->get_test_msg();
	printf("key : %s\n", msg_type->key);
	printf("testing message: \n\t%s\n", test_msg);

	/* load it */
	token = json_loads(test_msg, 0, &err);

	if (!token) {
		fprintf(stderr, "error: on line %d: %s\n", err.line, err.text);
		return -1;
	}

	/* unpack it */
	error = msg_type->unpack(token, &data);
	json_decref(token);

	if (error) {
		fprintf(stderr, "error: unpacking failed\n");
		return -1;
	}

	/* re-pack it */
	char *s;
	error = msg_type->pack(data, &s);
	msg_type->free(data);
	if (error) {
		fprintf(stderr, "error: packing failed\n");
		free(s);
		return -1;
	}

	/* load it a second time */
	token = json_loads(s, 0, &err);
	free(s);

	if (!token) {
		fprintf(stderr, "error on second load: %s\n", err.text);
		return -1;
	}

	/* unpack it a second time */
	error = msg_type->unpack(token, &data);
	json_decref(token);
	if (error) {
		fprintf(stderr, "error: second unpacking failed\n");
		return -1;
	}
	msg_type->free(data);

	printf("\n%s message load->unpack->pack->load->unpack OK.\n\n\n",
	       msg_type->key);
	return 0;
}

static int test_messages(const int *msg_type_arr)
{
	const int *msg_type_idx = msg_type_arr;
	int error;

	for (msg_type_idx = msg_type_arr; *msg_type_idx != JT_MSG_END;
	     msg_type_idx++) {
		error = test_unpack_pack_unpack(*msg_type_idx);
		if (error) {
			return -1;
		}
	}
	return 0;
}

int test_c2s_messages() { return test_messages(&jt_msg_types_c2s[0]); }

int test_s2c_messages() { return test_messages(&jt_msg_types_s2c[0]); }

int main()
{
	int err;

	printf("testing server -> client messages...\n");
	err = test_s2c_messages();
	assert(!err);
	printf("server -> client messages passed.\n");

	printf("testing client -> server messages...\n");
	err = test_c2s_messages();
	assert(!err);

	printf("Achievement unlocked: all message tests passed.\n");
}
