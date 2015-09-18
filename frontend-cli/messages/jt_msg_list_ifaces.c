#include <string.h>
#include <assert.h>
#include <jansson.h>

#include "jt_message_types.h"
#include "jt_messages.h"
#include "jt_msg_list_ifaces.h"

static const char *jt_iface_list_test_msg = "{\"msg\":\"iface_list\", \"p\": "
                                            "{\"ifaces\":[\"em1\", \"wlp3s0\", "
                                            "\"virbr0\", \"virbr0-nic\"]}}";

const char *jt_iface_list_test_msg_get(void) { return jt_iface_list_test_msg; }

int jt_iface_list_consumer(void *data)
{
	int i;
	struct jt_iface_list *il = (struct jt_iface_list *)data;

	printf("Iface list:\n");
	for (i = 0; i < il->count; i++) {
		printf("\t%s\n", il->ifaces[i]);
	}
	free(il->ifaces);
	free(il);

	return 0;
}

int jt_iface_list_unpacker(json_t *root, void **data)
{
	json_t *params, *iface_array;
	struct jt_iface_list *il;

	params = json_object_get(root, "p");
	assert(params);

	iface_array = json_object_get(params, "ifaces");
	assert(JSON_ARRAY == json_typeof(iface_array));
	assert(0 < json_array_size(iface_array));

	il = malloc(sizeof(struct jt_iface_list));
	il->count = json_array_size(iface_array);
	assert(0 < il->count);

	il->ifaces = malloc(il->count * MAX_IFACE_LEN);

	int i;
	for (i = 0; i < il->count; i++) {
		json_t *s = json_array_get(iface_array, i);
		assert(json_is_string(s));
		assert(JSON_STRING == json_typeof(s));
		snprintf(il->ifaces[i], MAX_IFACE_LEN - 1, "%s",
		         json_string_value(s));
	}

	*data = il;
	return 0;
}

int jt_iface_list_packer(void *data, char **out)
{
	struct jt_iface_list *list = data;
	json_t *msg = json_object();
	json_t *params = json_object();
	json_t *iface_arr = json_array();
	int i;

	for (i = 0; i < list->count; i++) {
		json_array_append_new(iface_arr, json_string(list->ifaces[i]));
	}
	json_object_set(params, "ifaces", iface_arr);
	json_object_set_new(msg, "msg",
	                    json_string(jt_messages[JT_MSG_IFACE_LIST_V1].key));
	json_object_set(msg, "p", params);
	*out = json_dumps(msg, 0);
	return 0;
}
