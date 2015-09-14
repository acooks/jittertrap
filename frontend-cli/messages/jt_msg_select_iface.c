#include <string.h>
#include <assert.h>
#include <jansson.h>

#include "jt_message_types.h"
#include "jt_messages.h"
#include "jt_msg_stats.h"

int jt_select_iface_consumer(void *data)
{
	char (*iface)[MAX_IFACE_LEN] = data;

	printf("Selected Iface %s\n", *iface);
	free(iface);

	return 0;
}

int jt_select_iface_unpacker(json_t *root, void **data)
{
	json_t *iface_token;
	json_error_t error;
	int err;
	char (*iface)[MAX_IFACE_LEN];

	err =
	    json_unpack_ex(root, &error, JSON_VALIDATE_ONLY, "{s:o}",
	                   jt_messages[JT_MSG_SELECT_IFACE_V1].key,
	                   &iface_token);
	if (err) {
		return err;
	}

	iface_token = json_object_get(root,
	                              jt_messages[JT_MSG_SELECT_IFACE_V1].key);
	assert(JSON_STRING == json_typeof(iface_token));
	assert(0 < json_string_length(iface_token));

	iface = malloc(MAX_IFACE_LEN);
	snprintf(*iface, MAX_IFACE_LEN, "%s",
	         json_string_value(iface_token));

	*data = iface;
	return 0;
}
