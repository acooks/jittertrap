#include <string.h>
#include <assert.h>
#include <jansson.h>

#include "jt_message_types.h"
#include "jt_messages.h"
#include "jt_msg_stats.h"

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
	json_t *iface_array;
	json_error_t error;
	int err;
	struct jt_iface_list *il;

	err =
	    json_unpack_ex(root, &error, JSON_VALIDATE_ONLY, "{s:o}",
	                   jt_messages[JT_MSG_IFACE_LIST_V1].key, &iface_array);
	if (err) {
		return err;
	}

	iface_array = json_object_get(root, "ifaces");
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
