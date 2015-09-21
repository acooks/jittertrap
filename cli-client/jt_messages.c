#include <string.h>
#include <jansson.h>

#include "jt_messages.h"

/*
 * FIXME: This will break when another top-level key:value pair is present.
 *
 * All messages must have format:
 * {'msg':'type', 'p':{}}
 */
static int match_msg_type(json_t *root, int type_id)
{
	json_t *msg_type;
	int cmp;
	msg_type = json_object_get(root, "msg");
	if (!msg_type || JSON_STRING != json_typeof(msg_type)) {
		fprintf(stderr, "not a jt message\n");
		return -1;
	}
	cmp = strncmp(jt_messages[type_id].key, json_string_value(msg_type),
	               strlen(jt_messages[type_id].key));
	if (cmp != 0) {
#if DEBUG
		fprintf(stderr, "[%s] type doesn't match [%s]\n",
			        jt_messages[type_id].key,
		                json_string_value(msg_type));
#endif
		return -1;
	}
	return 0;
}

static int jt_msg_handler(char *in, const int *msg_type_arr)
{
	json_t *root;
	json_error_t error;
	int err;
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
		err = match_msg_type(root, *msg_type);
		if (err) {
			// type doesn't match, try the next.
			continue;
		}

		// type matches, try to unpack it.
		err = jt_messages[*msg_type].to_struct(root, &data);
		if (err) {
			// type matched, but unpack failed.
			fprintf(stderr, "[%s] type match, unpack failed.\n",
			        jt_messages[*msg_type].key);
			break;
		}

		jt_messages[*msg_type].consume(data);
		json_decref(root);
		return 0;
	}
	fprintf(stderr, "couldn't unpack message: %s\n", in);
	json_decref(root);
	return -1;
}

/* handle messages received from server in client */
int jt_client_msg_handler(char *in)
{
	return jt_msg_handler(in, &jt_msg_types_s2c[0]);
}

/* handle messages received from client in server */
int jt_server_msg_handler(char *in)
{
	return jt_msg_handler(in, &jt_msg_types_c2s[0]);
}
