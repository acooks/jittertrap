#include <jansson.h>

#include "jt_messages.h"

static int match_msg_type(json_t *root, int type_id)
{
        json_t *t;
	json_error_t error;

        return json_unpack_ex(root, &error, JSON_VALIDATE_ONLY, "{s:o}",
	                      jt_messages[type_id].key, &t);
}

int jt_msg_handler(char *in)
{
	json_t *root;
	json_error_t error;
	int err;
	int i;
	void *data;

	root = json_loads(in, 0, &error);
	if (!root) {
		fprintf(stderr, "error: on line %d: %s\n", error.line,
		        error.text);
		return -1;
	}

	for (i = 0; i < JT_MSG_END; i++) {
		// check if the message type matches.
		err = match_msg_type(root, i);
		if (err) {
			// type doesn't match, try the next.
			continue;
		}

		// type matches, try to unpack it.
		err = jt_messages[i].unpack(root, &data);
		if (err) {
			// type matched, but unpack failed.
			break;
		}

		jt_messages[i].consume(data);
		json_decref(root);
		return 0;

	}
	printf("couldn't unpack message: %s\n", in);
	json_decref(root);
	return -1;
}
