#include <stdio.h>
#include <assert.h>

#include "websocket_message_queue.h"


int test_consume_from_empty()
{
	struct jt_ws_msg *m;

	printf("test for consume-from-empty case\n");

	jt_ws_mq_init();

	m = jt_ws_mq_consume_next();
	assert(NULL == m);
	jt_ws_mq_destroy();
	printf("OK.\n");
	return 0;
}

int test_produce_overflow()
{
	int i;
	struct jt_ws_msg *m;

	printf("test for produce overflow handling.\n");

	jt_ws_mq_init();

	for (i = 0 ; i < MAX_Q_DEPTH -1; i++) {
		m = jt_ws_mq_produce_next();
		assert(m);
	}
	printf("queue full: %d\n", i);
	m = jt_ws_mq_produce_next();
	assert(!m);

	jt_ws_mq_destroy();
	printf("OK.\n");
	return 0;
}


int test_produce_consume_without_recovery()
{
	int i;
	struct jt_ws_msg *m;

	printf("test_produce_consume_without_recovery\n");

	jt_ws_mq_init();

	i = 0;
	do {
		m = jt_ws_mq_produce_next();
		if (m) {
			snprintf(m->m, MAX_JSON_MSG_LEN, "message number %d", i);
			printf("produced message number %d\n", i);
			i++;
		}
	} while (m);
	printf("queue max length: %d\n", i);

	for (; i > 0; i--) {
		m = jt_ws_mq_consume_next();
		assert(m);

		printf("consumed message %d: %s\n", i, m->m);
	}
	m = jt_ws_mq_consume_next();
	assert(!m);

	jt_ws_mq_destroy();
	printf("OK.\n");
	return 0;
}

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

/* test for filling up the queue, then emptying it */
int test_produce_consume()
{
	int i, err;
	void *d = NULL;

	printf("Testing produce-til-full, consume-til-empty case \n");

	jt_ws_mq_init();

	/* fill up the queue */
	i = 0;
	char s[MAX_JSON_MSG_LEN];
	do {
		snprintf(s, MAX_JSON_MSG_LEN, "message number %d", i);
		err = jt_ws_mq_produce(message_producer, s);
		if (!err) { i++; }
	} while (!err);
	printf("queue max length: %d\n", i);

	/* we hava a full message queue, now consume it all */
	for (; i > 0; i--) {
		err = jt_ws_mq_consume(message_printer, d);
		assert(!err);
	}

	/* consuming from an empty queue must return error */
	err = jt_ws_mq_consume(message_printer, d);
	assert(err);

	jt_ws_mq_destroy();
	printf("OK.\n");
	return 0;
}

int test_ppcc()
{
	int err;
	struct jt_ws_msg *m;
	void *d = NULL;

	printf("Testing PPCC case\n");

	jt_ws_mq_init();

	m = jt_ws_mq_produce_next();
	assert(m);
	snprintf(m->m, MAX_JSON_MSG_LEN, "message number 1");

	m = jt_ws_mq_produce_next();
	assert(m);
	snprintf(m->m, MAX_JSON_MSG_LEN, "message number 2");

	err = jt_ws_mq_consume(message_printer, d);
	assert(!err);

	err = jt_ws_mq_consume(message_printer, d);
	assert(!err);

	/* consuming from an empty queue must return error */
	err = jt_ws_mq_consume(message_printer, d);
	assert(err);

	jt_ws_mq_destroy();
	printf("OK.\n");
	return 0;
}

int test_pcpc()
{
	int err;
	struct jt_ws_msg *m;
	void *d = NULL;

	printf("Testing PCPC case\n");

	jt_ws_mq_init();

	m = jt_ws_mq_produce_next();
	assert(m);
	snprintf(m->m, MAX_JSON_MSG_LEN, "message number 1");

	err = jt_ws_mq_consume(message_printer, d);
	assert(!err);

	m = jt_ws_mq_produce_next();
	assert(m);
	snprintf(m->m, MAX_JSON_MSG_LEN, "message number 2");

	err = jt_ws_mq_consume(message_printer, d);
	assert(!err);

	/* consuming from an empty queue must return error */
	err = jt_ws_mq_consume(message_printer, d);
	assert(err);

	jt_ws_mq_destroy();
	printf("OK.\n");
	return 0;
}

int test_pccp()
{
	int err;
	struct jt_ws_msg *m;
	void *d = NULL;

	printf("Testing PCCP case\n");

	jt_ws_mq_init();

	m = jt_ws_mq_produce_next();
	assert(m);
	snprintf(m->m, MAX_JSON_MSG_LEN, "message number 1");

	err = jt_ws_mq_consume(message_printer, d);
	assert(!err);

	/* consuming from an empty queue must return error */
	err = jt_ws_mq_consume(message_printer, d);
	assert(err);

	m = jt_ws_mq_produce_next();
	assert(m);
	snprintf(m->m, MAX_JSON_MSG_LEN, "message number 2");

	err = jt_ws_mq_consume(message_printer, d);
	assert(!err);

	/* consuming from an empty queue must return error */
	err = jt_ws_mq_consume(message_printer, d);
	assert(err);

	jt_ws_mq_destroy();
	printf("OK.\n");
	return 0;
}



int main()
{
	assert(0 == test_consume_from_empty());
	assert(0 == test_produce_overflow());
	assert(0 == test_produce_consume());
	assert(0 == test_produce_consume_without_recovery());
	assert(0 == test_ppcc());
	assert(0 == test_pcpc());
	assert(0 == test_pccp());
	printf("message queue tests passed.\n");
	return 0;
}
