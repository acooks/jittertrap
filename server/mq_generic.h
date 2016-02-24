#ifndef SAMPLE_BUF_H
#define SAMPLE_BUF_H

typedef enum {
	JT_WS_MQ_OK = 0,
	JT_WS_MQ_EMPTY = 1,
	JT_WS_MQ_FULL = 2,
	JT_WS_MQ_CB_ERR = 3,
	JT_WS_MQ_NO_CONSUMERS = 4
} jtmq_err;

struct NS(msg);

int NS(init)();
int NS(destroy)();
int NS(consumer_subscribe)(unsigned long *subscriber_id);
int NS(consumer_unsubscribe)(unsigned long subscriber_id);

typedef int (*NS(callback))(struct NS(msg) * m, void *data);

int NS(produce)(NS(callback) cb, void *cb_data, int *cb_err);
int NS(consume)(unsigned long id, NS(callback) cb, void *cb_data, int *cb_err);

#endif
