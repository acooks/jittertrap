#ifndef JT_MESSAGE_TYPE_H
#define JT_MESSAGE_TYPE_H

/* A list of known messages types. */
typedef enum {

	/* Server to Client messages */
	JT_MSG_STATS_V1 = 50,
	JT_MSG_IFACE_LIST_V1 = 100,
	JT_MSG_SELECT_IFACE_V1 = 110,
	JT_MSG_NETEM_PARAMS_V1 = 120,
	JT_MSG_SAMPLE_PERIOD_V1 = 130,

	/* Client to Server messages */
	JT_MSG_GET_NETEM_V1     = 140,
	JT_MSG_SET_NETEM_V1     = 150,

	/* terminator */
	JT_MSG_END = 255
} jt_msg_type_id_t;

static const int jt_msg_types_s2c[] = {
	JT_MSG_STATS_V1,
	JT_MSG_IFACE_LIST_V1,
	JT_MSG_SELECT_IFACE_V1,
	JT_MSG_NETEM_PARAMS_V1,
	JT_MSG_SAMPLE_PERIOD_V1,

	/* terminator */
	JT_MSG_END
};

static const int jt_msg_types_c2s[] = {
	JT_MSG_GET_NETEM_V1,
	JT_MSG_SET_NETEM_V1,

	/* terminator */
	JT_MSG_END
};

/*
 * function pointer type for unpacker function types.
 * Stores unpacked data in *data for consumption by jt_consumer_t.
 */
typedef int (*jt_unpacker_t)(json_t *root, void **data);

/*
 * function pointer type for consumer function types.
 * Must free() data when done.
 */
typedef int (*jt_consumer_t)(void *data);

struct jt_msg_type
{
	jt_msg_type_id_t type;
	const char *key;
	jt_unpacker_t unpack;
	jt_consumer_t consume;
};

#endif
