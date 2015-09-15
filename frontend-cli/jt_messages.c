#include <jansson.h>

#include "jt_messages.h"

static int match_msg_type(json_t *root, int type_id)
{
        json_t *t;
	json_error_t error;

        return json_unpack_ex(root, &error, JSON_VALIDATE_ONLY, "{s:o}",
	                      jt_messages[type_id].key, &t);
}

int jt_client_msg_handler(char *in)
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

	// iterate over s2c_msg_types using pointer arithmetic.
	for (msg_type = &jt_msg_types_s2c[0]; *msg_type != JT_MSG_END;
	     msg_type++) {
		// check if the message type matches.
		err = match_msg_type(root, *msg_type);
		if (err) {
			// type doesn't match, try the next.
			continue;
		}

		// type matches, try to unpack it.
		err = jt_messages[*msg_type].unpack(root, &data);
		if (err) {
			// type matched, but unpack failed.
			break;
		}

		jt_messages[*msg_type].consume(data);
		json_decref(root);
		return 0;
	}
	printf("couldn't unpack message: %s\n", in);
	json_decref(root);
	return -1;
}
