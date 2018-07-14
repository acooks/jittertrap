#include <string.h>
#include <jansson.h>

#include "jt_messages.h"

static int jt_msg_handler(char *in, const int *msg_type_arr)
{
	json_t *root;
	json_error_t error;
	int err;
	void *data;
	const int *msg_type;
	char printable[128];

	root = json_loads(in, 0, &error);
	if (!root) {
		fprintf(stderr, "error: on line %d: %s\n", error.line,
		        error.text);
		return -1;
	}

	// iterate over array of msg types using pointer arithmetic.
	for (msg_type = msg_type_arr; *msg_type != JT_MSG_END; msg_type++) {
		// check if the message type matches.
		err = jt_msg_match_type(root, *msg_type);
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

		jt_messages[*msg_type].print(data, printable,
		                             sizeof(printable));

		switch (*msg_type) {
		case JT_MSG_TOPTALK_V1:
			/* FIXME: printable doesn't look very usable yet. */
			break;
		case JT_MSG_STATS_V1:
			printf("\r%s", printable);
			break;
		default:
			printf("%s\n", printable);
		}

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
