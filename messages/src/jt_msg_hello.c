#include <string.h>
#include <assert.h>
#include <jansson.h>

#include "jt_message_types.h"
#include "jt_messages.h"

#include "jt_msg_hello.h"

static const char *jt_hello_test_msg = "{\"msg\":\"hello\"}";

const char *jt_hello_test_msg_get(void) { return jt_hello_test_msg; }

int jt_hello_free(void *data)
{
	return 0;
}

int jt_hello_printer(void *data, char *out, int len)
{
	snprintf(out, len, "Hi!");
	return 0;
}

int jt_hello_packer(void *data, char **out)
{
	json_t *t = json_object();

	json_object_set_new(
	    t, "msg", json_string(jt_messages[JT_MSG_HELLO_V1].key));
	*out = json_dumps(t, 0);
	json_object_clear(t);
	json_decref(t);
	return 0;
}

int jt_hello_unpacker(json_t *root, void **data)
{
	return 0;
}
