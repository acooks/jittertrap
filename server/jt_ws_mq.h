#ifndef SAMPLE_BUF_H
#define SAMPLE_BUF_H

typedef enum {
	JT_WS_MQ_OK = 0,
	JT_WS_MQ_EMPTY = 1,
	JT_WS_MQ_FULL = 2,
	JT_WS_MQ_CB_ERR = 3,
	JT_WS_MQ_NO_CONSUMERS = 4
} jt_ws_mq_err;

struct jt_ws_msg;

int jt_ws_mq_init();
int jt_ws_mq_destroy();
int jt_ws_mq_consumer_subscribe(unsigned long *subscriber_id);
int jt_ws_mq_consumer_unsubscribe(unsigned long subscriber_id);

typedef int (*jt_ws_mq_callback)(struct jt_ws_msg *m, void *data);

int jt_ws_mq_produce(jt_ws_mq_callback cb, void *cb_data, int *cb_err);
int jt_ws_mq_consume(unsigned long id, jt_ws_mq_callback cb, void *cb_data,
                     int *cb_err);

#endif
