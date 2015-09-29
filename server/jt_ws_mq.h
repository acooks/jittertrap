#ifndef SAMPLE_BUF_H
#define SAMPLE_BUF_H

struct jt_ws_msg;

int jt_ws_mq_init();
int jt_ws_mq_destroy();
int jt_ws_mq_consumer_subscribe(unsigned long *subscriber_id);
int jt_ws_mq_consumer_unsubscribe(unsigned long subscriber_id);

typedef int (*jt_ws_mq_callback)(struct jt_ws_msg *m, void *data);

int jt_ws_mq_produce(jt_ws_mq_callback cb, void *data);
int jt_ws_mq_consume(unsigned long id, jt_ws_mq_callback cb, void *data);

#endif
