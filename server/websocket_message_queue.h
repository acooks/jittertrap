#ifndef SAMPLE_BUF_H
#define SAMPLE_BUF_H

#define MAX_JSON_MSG_LEN 3000

/* Mem use:
 *    MAX_Q_DEPTH * MAX_JSON_MSG_LEN
 *    32 * 3000 = 96k
 */

#define MAX_Q_DEPTH 32

struct jt_ws_msg
{
	char m[MAX_JSON_MSG_LEN];
};

void jt_ws_mq_init();
void jt_ws_mq_destroy();

/* Returns a pointer to the next produce-able message buffer, or NULL when
 * the queue is full.
 */
struct jt_ws_msg *jt_ws_mq_produce_next();

/*
 * Returns a pointer to the next consumable message, or NULL when the queue
 * is empty.
 */
struct jt_ws_msg *jt_ws_mq_consume_next();

typedef int (*jt_ws_mq_callback)(struct jt_ws_msg *m, void *data);

int jt_ws_mq_produce(jt_ws_mq_callback cb, void *data);
int jt_ws_mq_consume(jt_ws_mq_callback cb, void *data);

#endif
