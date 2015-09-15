#include <string.h>
#include <assert.h>
#include <jansson.h>

#include "jt_message_types.h"
#include "jt_messages.h"

int jt_get_netem_consumer(void *data)
{
	char(*iface)[MAX_IFACE_LEN] = data;

	printf("Get netem params for iface: %s\n", *iface);
	free(iface);

	return 0;
}

int jt_get_netem_unpacker(json_t *root, void **data)
{
	json_t *iface_token;
	char(*iface)[MAX_IFACE_LEN];

	iface_token =
	    json_object_get(root, jt_messages[JT_MSG_GET_NETEM_V1].key);
	assert(JSON_STRING == json_typeof(iface_token));
	assert(0 < json_string_length(iface_token));

	iface = malloc(MAX_IFACE_LEN);
	snprintf(*iface, MAX_IFACE_LEN, "%s", json_string_value(iface_token));

	*data = iface;
	return 0;
}
