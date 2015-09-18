#include <string.h>
#include <assert.h>
#include <jansson.h>

#include "jt_message_types.h"
#include "jt_messages.h"

static const char *jt_get_netem_test_msg =
    "{\"msg\": \"get_netem\", \"p\":{\"dev\": \"em1\"}}";

const char *jt_get_netem_test_msg_get(void) { return jt_get_netem_test_msg; }

int jt_get_netem_free(void *data)
{
	char(*iface)[MAX_IFACE_LEN] = data;
	free(iface);
	return 0;
}

int jt_get_netem_consumer(void *data)
{
	char(*iface)[MAX_IFACE_LEN] = data;

	printf("Get netem params for iface: %s\n", *iface);

	return jt_get_netem_free(data);
}

int jt_get_netem_unpacker(json_t *root, void **data)
{
	json_t *params, *iface_token;
	char(*iface)[MAX_IFACE_LEN];

	params = json_object_get(root, "p");
	assert(params);

	iface_token = json_object_get(params, "dev");
	assert(JSON_STRING == json_typeof(iface_token));
	assert(0 < json_string_length(iface_token));
	// assert(MAX_IFACE_LEN > json_string_length(iface_token));

	iface = malloc(MAX_IFACE_LEN);
	snprintf(*iface, MAX_IFACE_LEN, "%s", json_string_value(iface_token));

	*data = iface;
	return 0;
}

int jt_get_netem_packer(void *data, char **out)
{
	json_t *t;
	char(*iface)[MAX_IFACE_LEN] = data;

	t = json_pack("{s:s, s:{s:s}}", "msg",
	              jt_messages[JT_MSG_GET_NETEM_V1].key, "p", "dev", iface);
	*out = json_dumps(t, 0);
	json_object_clear(t);
	json_decref(t);
	return 0;
}
