#include <string.h>
#include <assert.h>
#include <jansson.h>

#include "jt_message_types.h"
#include "jt_messages.h"

#include "jt_msg_set_netem.h"

int jt_set_netem_consumer(void *data)
{
	struct jt_msg_netem_params *p = data;

	printf("Impairment params:\n"
	       "\tInterface:  %s\n"
	       "\tDelay:      %dms\n"
	       "\tJitter:  +/-%dms\n"
	       "\tLoss:       %d\n",
	       p->iface, p->delay, p->jitter, p->loss);
	free(iface);

	return 0;
}

int jt_set_netem_unpacker(json_t *root, void **data)
{
	json_t *params_token;
	struct jt_msg_netem_params *params;

	params_token =
	    json_object_get(root, jt_messages[JT_MSG_NETEM_PARAMS_V1].key);
	assert(JSON_OBJECT == json_typeof(params_token));
	assert(0 < json_object_size(params_token));

	params = malloc(sizeof(struct jt_msg_netem_params));

	json_t *token = json_object_get(params_token, "iface");
	if (!json_is_string(token)) {
		assert(0);
		goto cleanup_unpack_fail;
	}
	snprintf(params->iface, MAX_IFACE_LEN, "%s", json_string_value(token));

	token = json_object_get(params_token, "delay");
	if (!json_is_integer(token)) {
		assert(0);
		goto cleanup_unpack_fail;
	}
	params->delay = json_integer_value(token);

	token = json_object_get(params_token, "jitter");
	if (!json_is_integer(token)) {
		assert(0);
		goto cleanup_unpack_fail;
	}
	params->jitter = json_integer_value(token);

	token = json_object_get(params_token, "loss");
	if (!json_is_integer(token)) {
		assert(0);
		goto cleanup_unpack_fail;
	}
	params->loss = json_integer_value(token);

	*data = params;
	return 0;

cleanup_unpack_fail:
	free(params);
	return -1;
}
