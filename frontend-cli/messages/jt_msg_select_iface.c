#include <string.h>
#include <assert.h>
#include <jansson.h>

#include "jt_message_types.h"
#include "jt_messages.h"
#include "jt_msg_stats.h"

static const char *jt_select_iface_test_msg =
    "{\"msg\":\"dev_select\", \"p\":{\"iface\":\"em1\"}}";

const char *jt_select_iface_test_msg_get(void)
{
	return jt_select_iface_test_msg;
}

int jt_select_iface_free(void *data)
{
	char(*iface)[MAX_IFACE_LEN] = data;
	free(iface);
	return 0;
}

int jt_select_iface_consumer(void *data)
{
	char(*iface)[MAX_IFACE_LEN] = data;
	printf("Selected Iface %s\n", *iface);
	return jt_select_iface_free(data);
}

int jt_select_iface_unpacker(json_t *root, void **data)
{
	json_t *params, *iface_token;
	char(*iface)[MAX_IFACE_LEN];

	params = json_object_get(root, "p");
	assert(params);

	iface_token = json_object_get(params, "iface");
	if (!iface_token
	    || (JSON_STRING != json_typeof(iface_token))
	    || (0 >= json_string_length(iface_token)))
	{
		return -1;
	}

	iface = malloc(MAX_IFACE_LEN);
	snprintf(*iface, MAX_IFACE_LEN, "%s", json_string_value(iface_token));

	*data = iface;
	return 0;
}

int jt_select_iface_packer(void *data, char **out)
{
	char(*iface)[MAX_IFACE_LEN] = data;
	json_t *t = json_object();
	json_t *params = json_object();
	json_object_set_new(
	    t, "msg", json_string(jt_messages[JT_MSG_SELECT_IFACE_V1].key));
	json_object_set_new(params, "iface", json_string(*iface));
	json_object_set(t, "p", params);
	*out = json_dumps(t, 0);
	json_object_clear(params);
	json_decref(params);
	json_object_clear(t);
	json_decref(t);
	return 0;
}
