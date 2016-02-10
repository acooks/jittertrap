#ifndef SAMPLE_BUF_H
#define SAMPLE_BUF_H

typedef enum {
	JT_WS_MQ_OK = 0,
	JT_WS_MQ_EMPTY = 1,
	JT_WS_MQ_FULL = 2,
	JT_WS_MQ_CB_ERR = 3,
	JT_WS_MQ_NO_CONSUMERS = 4
} jtmq_err;

struct jtmq_msg;

int jtmq_init();
int jtmq_destroy();
int jtmq_consumer_subscribe(unsigned long *subscriber_id);
int jtmq_consumer_unsubscribe(unsigned long subscriber_id);

typedef int (*jtmq_callback)(struct jtmq_msg *m, void *data);

int jtmq_produce(jtmq_callback cb, void *cb_data, int *cb_err);
int jtmq_consume(unsigned long id, jtmq_callback cb, void *cb_data,
                     int *cb_err);

#endif
