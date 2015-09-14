#include <string.h>
#include <assert.h>
#include <jansson.h>

#include "jt_message_types.h"
#include "jt_messages.h"
#include "jt_msg_sample_period.h"

int jt_sample_period_consumer(void *data)
{
	int *sp = data;

	printf("Sampling period: %d\n", *sp);

	return 0;
}

int jt_sample_period_unpacker(json_t *root, void **data)
{
	json_t *sp_token;
	json_error_t error;
	int err;
	int *sp;

	err =
	    json_unpack_ex(root, &error, JSON_VALIDATE_ONLY, "{s:o}",
	                   jt_messages[JT_MSG_SAMPLE_PERIOD_V1].key,
	                   &sp_token);
	if (err) {
		return err;
	}

	sp_token = json_object_get(root,
	                           jt_messages[JT_MSG_SAMPLE_PERIOD_V1].key);
	assert(JSON_INTEGER == json_typeof(sp_token));

	// yes, this is stupid, but the unpacker and consumer can only
	// communicate by passing a pointer.
	sp = malloc(sizeof(int));
	*sp = json_integer_value(sp_token);

	*data = sp;
	return 0;
}
