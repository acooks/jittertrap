#ifndef JT_MQ_GENERIC_ERRORS
#define JT_MQ_GENERIC_ERRORS
/* this error code enum should be included only once, because it's common to
 * all types of JT_MQ.
 */
typedef enum {
	JT_WS_MQ_OK = 0,
	JT_WS_MQ_EMPTY = 1,
	JT_WS_MQ_FULL = 2,
	JT_WS_MQ_CB_ERR = 3,
	JT_WS_MQ_NO_CONSUMERS = 4
} jtmq_err;

#endif  /* JT_MQ_GENERIC_ERRORS */

/* The remaining parameterized definitions MUST be unique and SHOULD be
 * included every time a specific queue type is used.
 */

struct NS(msg);

int NS(init)(const char *mq_name);
int NS(destroy)(void);
int NS(consumer_subscribe)(unsigned long *subscriber_id);
int NS(consumer_unsubscribe)(unsigned long subscriber_id);

typedef int (*NS(callback))(struct NS(msg) * m, void *data);

int NS(produce)(NS(callback) cb, void *cb_data, int *cb_err);
int NS(consume)(unsigned long id, NS(callback) cb, void *cb_data, int *cb_err);
int NS(maxlen)(void);

