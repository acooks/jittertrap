#ifndef JT_MESSAGE_TYPE_H
#define JT_MESSAGE_TYPE_H

/* A list of known messages types. */
typedef enum {
	JT_MSG_STATS_V1,

	/* terminator */
	JT_MSG_END
} jt_msg_type_id_t;

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
