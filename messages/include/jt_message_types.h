#ifndef JT_MESSAGE_TYPE_H
#define JT_MESSAGE_TYPE_H

/* A list of known messages types. */
typedef enum {

	/* Server to Client messages */
	JT_MSG_STATS_V1         = 50,
	JT_MSG_TOPTALK_V1       = 60,
	JT_MSG_IFACE_LIST_V1    = 100,
	JT_MSG_SELECT_IFACE_V1  = 110, // used in both s2c and c2s directions
	JT_MSG_NETEM_PARAMS_V1  = 120,
	JT_MSG_SAMPLE_PERIOD_V1 = 130,

	/* Client to Server messages */
	JT_MSG_SET_NETEM_V1 = 140,

	/* terminator */
	JT_MSG_END = 255
} jt_msg_type_id_t;

static const int jt_msg_types_s2c[] = {
	JT_MSG_STATS_V1,
	JT_MSG_TOPTALK_V1,
        JT_MSG_IFACE_LIST_V1,
        JT_MSG_SELECT_IFACE_V1,
	JT_MSG_NETEM_PARAMS_V1,
        JT_MSG_SAMPLE_PERIOD_V1,

	/* terminator */
	JT_MSG_END
};

static const int jt_msg_types_c2s[] = {
	JT_MSG_SELECT_IFACE_V1,
	JT_MSG_SET_NETEM_V1,

	/* terminator */
	JT_MSG_END
};

/*
 * function pointer type for packer (struct to string) function.
 * Takes a struct in *data and creates a string and points *out to it.
 * *out must be uninitialised and the caller must free it.
 */
typedef int (*jt_packer_t)(void *data, char **out);

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


typedef int (*jt_print_t)(void *data, char *out, int len);

typedef const char *(*jt_test_msg_getter_t)(void);

struct jt_msg_type
{
	jt_msg_type_id_t type;
	const char *key;
	jt_packer_t to_json_string;
	jt_unpacker_t to_struct;
	jt_print_t print;
	jt_consumer_t free;
	jt_test_msg_getter_t get_test_msg;
};

#endif
