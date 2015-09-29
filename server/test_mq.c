#include <stdio.h>
#include <assert.h>

#include "jt_ws_mq_config.h"
#include "jt_ws_mq.h"

int message_producer(struct jt_ws_msg *m, void *data)
{
	char *s = (char *)data;
	snprintf(m->m, MAX_JSON_MSG_LEN, "%s", s);
	return 0;
}

/* a callback for consuming messages. */
int message_printer(struct jt_ws_msg *m, void *data  __attribute__((unused)))
{
	assert(m);
	printf("m: %s\n", m->m);
	return 0;
}

int string_copier(struct jt_ws_msg *m, void *data)
{
	char *s;

	assert(m);
	assert(data);

	s = (char *)data;
	sprintf(s, m->m);
	return 0;
}

int test_consume_from_empty()
{
	char msg[MAX_JSON_MSG_LEN];
	int err;
	unsigned long id;


	printf("test for consume-from-empty case\n");
	err = jt_ws_mq_init();
	assert(!err);

	err = jt_ws_mq_consumer_subscribe(&id);
	assert(!err);

	err = jt_ws_mq_consume(id, message_printer, msg);
	assert(err);

	err = jt_ws_mq_consumer_unsubscribe(id);
	assert(!err);

	err = jt_ws_mq_destroy();
	assert(!err);

	printf("OK.\n");
	return 0;
}

int test_produce_overflow()
{
	int i, err;
	unsigned long id;
	char msg[MAX_JSON_MSG_LEN];

	printf("test for produce overflow handling.\n");

	err = jt_ws_mq_init();
	assert(!err);

	err = jt_ws_mq_consumer_subscribe(&id);
	assert(!err);

	for (i = 0 ; i < MAX_Q_DEPTH -1; i++) {
		snprintf(msg, MAX_JSON_MSG_LEN, "Message %d", i);
		printf("msg: %s\n", msg);
		err = jt_ws_mq_produce(message_producer, msg);
		assert(!err);
	}
	printf("queue full: %d\n", i);
	err = jt_ws_mq_produce(message_producer, msg);
	assert(err);

	err = jt_ws_mq_consumer_unsubscribe(id);
	assert(!err);

	err = jt_ws_mq_destroy();
	assert(!err);
	printf("OK.\n");
	return 0;
}

/* test for filling up the queue, then emptying it */
int test_produce_consume()
{
	int i, err;
	unsigned long id;
	char s[MAX_JSON_MSG_LEN];

	printf("Testing produce-til-full, consume-til-empty case \n");

	err = jt_ws_mq_init();
	assert(!err);

	err = jt_ws_mq_consumer_subscribe(&id);
	assert(!err);

	/* fill up the queue */
	i = 0;
	do {
		snprintf(s, MAX_JSON_MSG_LEN, "message number %d", i);
		err = jt_ws_mq_produce(message_producer, s);
		if (!err) { i++; }
	} while (!err);
	printf("queue max length: %d\n", i);

	/* we hava a full message queue, now consume it all */
	for (; i > 0; i--) {
		err = jt_ws_mq_consume(id, message_printer, s);
		assert(!err);
	}

	/* consuming from an empty queue must return error */
	err = jt_ws_mq_consume(id, message_printer, s);
	assert(err);

	err = jt_ws_mq_consumer_unsubscribe(id);
	assert(!err);

	err = jt_ws_mq_destroy();
	assert(!err);

	printf("OK.\n");
	return 0;
}

int test_ppcc()
{
	int err;
	unsigned long id;
	char s[MAX_JSON_MSG_LEN];

	printf("Testing PPCC case\n");

	err = jt_ws_mq_init();
	assert(!err);

	err = jt_ws_mq_consumer_subscribe(&id);
	assert(!err);

	err = jt_ws_mq_produce(message_producer, "Message 1");
	assert(!err);

	err = jt_ws_mq_produce(message_producer, "Message 2");
	assert(!err);

	err = jt_ws_mq_consume(id, string_copier, s);
	assert(!err);
	printf("consumed 1: %s\n", s);

	err = jt_ws_mq_consume(id, string_copier, s);
	assert(!err);
	printf("consumed 2: %s\n", s);

	/* consuming from an empty queue must return error */
	err = jt_ws_mq_consume(id, message_printer, s);
	assert(err);

	err = jt_ws_mq_consumer_unsubscribe(id);
	assert(!err);

	err = jt_ws_mq_destroy();
	assert(!err);

	printf("OK.\n");
	return 0;
}

int test_pcpc()
{
	int err;
	unsigned long id;
	char s[MAX_JSON_MSG_LEN];

	printf("Testing PCPC case\n");

	err = jt_ws_mq_init();
	assert(!err);

	err = jt_ws_mq_consumer_subscribe(&id);
	assert(!err);

	err = jt_ws_mq_produce(message_producer, "message number 1");
	assert(!err);

	err = jt_ws_mq_consume(id, message_printer, s);
	assert(!err);

	err = jt_ws_mq_produce(message_producer, "message number 2");
	assert(!err);

	err = jt_ws_mq_consume(id, message_printer, s);
	assert(!err);

	/* consuming from an empty queue must return error */
	err = jt_ws_mq_consume(id, message_printer, s);
	assert(err);

	err = jt_ws_mq_consumer_unsubscribe(id);
	assert(!err);

	err = jt_ws_mq_destroy();
	assert(!err);

	printf("OK.\n");
	return 0;
}

int test_pccpcc()
{
	int err;
	unsigned long id;
	char s[MAX_JSON_MSG_LEN];

	printf("Testing PCCP case\n");

	err = jt_ws_mq_init();
	assert(!err);

	err = jt_ws_mq_consumer_subscribe(&id);

	err = jt_ws_mq_produce(message_producer, "message number 1");
	assert(!err);

	err = jt_ws_mq_consume(id, message_printer, s);
	assert(!err);

	/* consuming from an empty queue must return error */
	err = jt_ws_mq_consume(id, message_printer, s);
	assert(err);

	err = jt_ws_mq_produce(message_producer, "message number 2");
	assert(!err);

	err = jt_ws_mq_consume(id, message_printer, s);
	assert(!err);

	/* consuming from an empty queue must return error */
	err = jt_ws_mq_consume(id, message_printer, s);
	assert(err);

	err = jt_ws_mq_consumer_unsubscribe(id);
	assert(!err);

	err = jt_ws_mq_destroy();
	assert(!err);

	printf("OK.\n");
	return 0;
}



int main()
{
	assert(0 == test_consume_from_empty());
	assert(0 == test_produce_overflow());
	assert(0 == test_produce_consume());
	assert(0 == test_ppcc());
	assert(0 == test_pcpc());
	assert(0 == test_pccpcc());
	printf("message queue tests passed.\n");
	return 0;
}
