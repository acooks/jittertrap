#include <string.h>
#include <assert.h>
#include <jansson.h>

#include "jt_message_types.h"
#include "jt_messages.h"
#include "jt_msg_sample_period.h"

static const char *jt_sample_period_test_msg =
    "{\"msg\":\"sample_period\", \"p\":{\"period\":500}}";

const char *jt_sample_period_msg_get(void) { return jt_sample_period_test_msg; }

int jt_sample_period_free(void *data)
{
	int *sp = data;
	free(sp);
	return 0;
}

int jt_sample_period_consumer(void *data)
{
	int *sp = data;
	printf("Sampling period: %d\n", *sp);
	return jt_sample_period_free(data);
}

int jt_sample_period_unpacker(json_t *root, void **data)
{
	json_t *params, *sp_token;
	int *sp;

	params = json_object_get(root, "p");
	assert(params);
	sp_token = json_object_get(params, "period");
	assert(JSON_INTEGER == json_typeof(sp_token));

	// yes, this is stupid, but the unpacker and consumer can only
	// communicate by passing a pointer.
	sp = malloc(sizeof(int));
	*sp = json_integer_value(sp_token);

	*data = sp;
	return 0;
}

int jt_sample_period_packer(void *data, char **out)
{
	int *sample_period = data;
	json_t *t = json_object();
	json_t *msg = json_object();

	json_object_set_new(
	    msg, "msg", json_string(jt_messages[JT_MSG_SAMPLE_PERIOD_V1].key));
	json_object_set_new(t, "period", json_integer(*sample_period));
	json_object_set(msg, "p", t);
	*out = json_dumps(msg, 0);
	json_object_clear(t);
	json_decref(t);
	json_object_clear(msg);
	json_decref(msg);
	return 0;
}
