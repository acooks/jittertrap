#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <assert.h>

#include "mq_msg_ws.h"

#define MAX_Q_DEPTH 16

int message_producer(struct mq_ws_msg *m, void *data)
{
	int *d = (int *)data;
	sprintf(m->m, "message %d", *d);
	return 0;
}

/* a callback for consuming messages. */
int message_printer(struct mq_ws_msg *m, void *data __attribute__((unused)))
{
	assert(m);
	printf("m: %s\n", m->m);
	return 0;
}

int string_copier(struct mq_ws_msg *m, void *data)
{
	char *s;

	assert(m);
	assert(data);

	s = (char *)data;
	sprintf(s, m->m);
	return 0;
}

int benchmark_produce(struct mq_ws_msg *m, void *data __attribute__((unused)))
{
	m->m[0] = '\0';
	return 0;
}

int benchmark_consumer(struct mq_ws_msg *m __attribute__((unused)),
                       void *data __attribute__((unused)))
{
	return 0;
}

/* Calculate the absolute difference between t1 and t2. */
struct timespec ts_absdiff(struct timespec t1, struct timespec t2)
{
	struct timespec t;
	if ((t1.tv_sec < t2.tv_sec) ||
	    ((t1.tv_sec == t2.tv_sec) && (t1.tv_nsec <= t2.tv_nsec))) {
		/* t1 <= t2 */
		t.tv_sec = t2.tv_sec - t1.tv_sec;
		if (t2.tv_nsec < t1.tv_nsec) {
			t.tv_nsec = t2.tv_nsec + 1E9 - t1.tv_nsec;
			t.tv_sec--;
		} else {
			t.tv_nsec = t2.tv_nsec - t1.tv_nsec;
		}
	} else {
		/* t1 > t2 */
		t.tv_sec = t1.tv_sec - t2.tv_sec;
		if (t1.tv_nsec < t2.tv_nsec) {
			t.tv_nsec = t1.tv_nsec + 1E9 - t2.tv_nsec;
			t.tv_sec--; /* Borrow a second. */
		} else {
			t.tv_nsec = t1.tv_nsec - t2.tv_nsec;
		}
	}
	return t;
}

double ts_to_seconds(struct timespec t)
{
	return t.tv_sec + t.tv_nsec / 1E9;
}

int test_consume_from_empty()
{
	char msg[MAX_JSON_MSG_LEN];
	int err, cb_err;
	unsigned long id;

	printf("test for consume-from-empty case\n");
	err = mq_ws_init();
	assert(!err);

	err = mq_ws_consumer_subscribe(&id);
	assert(!err);

	err = mq_ws_consume(id, message_printer, msg, &cb_err);
	assert(-JT_WS_MQ_EMPTY == err);

	err = mq_ws_consumer_unsubscribe(id);
	assert(!err);

	err = mq_ws_destroy();
	assert(!err);

	printf("OK.\n");
	return 0;
}

int test_produce_overflow()
{
	int i, err, cb_err;
	unsigned long id;

	printf("test for produce overflow handling.\n");

	err = mq_ws_init();
	assert(!err);

	err = mq_ws_consumer_subscribe(&id);
	assert(!err);

	for (i = 0; i < MAX_Q_DEPTH - 1; i++) {
		err = mq_ws_produce(message_producer, &i, &cb_err);
		assert(!err);
	}
	printf("queue full: %d messages\n", i + 1);
	err = mq_ws_produce(message_producer, &i, &cb_err);
	assert(-JT_WS_MQ_FULL == err);

	err = mq_ws_consumer_unsubscribe(id);
	assert(!err);

	err = mq_ws_destroy();
	assert(!err);
	printf("OK.\n");
	return 0;
}

/* test for filling up the queue, then emptying it */
int test_produce_consume()
{
	int i, err, cb_err;
	unsigned long id;
	char s[MAX_JSON_MSG_LEN];

	printf("Testing produce-til-full, consume-til-empty case \n");

	err = mq_ws_init();
	assert(!err);

	err = mq_ws_consumer_subscribe(&id);
	assert(!err);

	/* fill up the queue */
	i = 0;
	do {
		err = mq_ws_produce(message_producer, &i, &cb_err);
		if (!err) {
			i++;
		}
	} while (!err);
	printf("queue max length: %d\n", i);

	/* we hava a full message queue, now consume it all */
	for (; i > 0; i--) {
		err = mq_ws_consume(id, message_printer, s, &cb_err);
		assert(!err);
	}

	/* consuming from an empty queue must return error  */
	err = mq_ws_consume(id, message_printer, s, &cb_err);
	assert(-JT_WS_MQ_EMPTY == err);

	err = mq_ws_consumer_unsubscribe(id);
	assert(!err);

	err = mq_ws_destroy();
	assert(!err);

	printf("OK.\n");
	return 0;
}

int test_ppcc()
{
	int err, cb_err;
	unsigned long id;
	char s[MAX_JSON_MSG_LEN];
	int msg_id;

	printf("Testing PPCC case\n");

	err = mq_ws_init();
	assert(!err);

	err = mq_ws_consumer_subscribe(&id);
	assert(!err);

	msg_id = 1;
	err = mq_ws_produce(message_producer, &msg_id, &cb_err);
	assert(!err);

	msg_id = 2;
	err = mq_ws_produce(message_producer, &msg_id, &cb_err);
	assert(!err);

	err = mq_ws_consume(id, string_copier, s, &cb_err);
	assert(!err);
	printf("consumed 1: %s\n", s);

	err = mq_ws_consume(id, string_copier, s, &cb_err);
	assert(!err);
	printf("consumed 2: %s\n", s);

	/* consuming from an empty queue must return error */
	err = mq_ws_consume(id, message_printer, s, &cb_err);
	assert(-JT_WS_MQ_EMPTY == err);

	err = mq_ws_consumer_unsubscribe(id);
	assert(!err);

	err = mq_ws_destroy();
	assert(!err);

	printf("OK.\n");
	return 0;
}

int test_pcpc()
{
	int err, cb_err;
	unsigned long id;
	char s[MAX_JSON_MSG_LEN];
	int msg_id;

	printf("Testing PCPC case\n");

	err = mq_ws_init();
	assert(!err);

	err = mq_ws_consumer_subscribe(&id);
	assert(!err);

	msg_id = 1;
	err = mq_ws_produce(message_producer, &msg_id, &cb_err);
	assert(!err);

	err = mq_ws_consume(id, message_printer, s, &cb_err);
	assert(!err);

	msg_id = 2;
	err = mq_ws_produce(message_producer, &msg_id, &cb_err);
	assert(!err);

	err = mq_ws_consume(id, message_printer, s, &cb_err);
	assert(!err);

	/* consuming from an empty queue must return error */
	err = mq_ws_consume(id, message_printer, s, &cb_err);
	assert(-JT_WS_MQ_EMPTY == err);

	err = mq_ws_consumer_unsubscribe(id);
	assert(!err);

	err = mq_ws_destroy();
	assert(!err);

	printf("OK.\n");
	return 0;
}

int test_pccpcc()
{
	int err, cb_err;
	unsigned long id;
	char s[MAX_JSON_MSG_LEN];
	int msg_id;

	printf("Testing PCCP case\n");

	err = mq_ws_init();
	assert(!err);

	err = mq_ws_consumer_subscribe(&id);

	msg_id = 1;
	err = mq_ws_produce(message_producer, &msg_id, &cb_err);
	assert(!err);

	err = mq_ws_consume(id, message_printer, s, &cb_err);
	assert(!err);

	/* consuming from an empty queue must return error */
	err = mq_ws_consume(id, message_printer, s, &cb_err);
	assert(-JT_WS_MQ_EMPTY == err);

	msg_id = 2;
	err = mq_ws_produce(message_producer, &msg_id, &cb_err);
	assert(!err);

	err = mq_ws_consume(id, message_printer, s, &cb_err);
	assert(!err);

	/* consuming from an empty queue must return error */
	err = mq_ws_consume(id, message_printer, s, &cb_err);
	assert(-JT_WS_MQ_EMPTY == err);

	err = mq_ws_consumer_unsubscribe(id);
	assert(!err);

	err = mq_ws_destroy();
	assert(!err);

	printf("OK.\n");
	return 0;
}

int benchmark()
{
	int i, j, err, cb_err;
	unsigned long id;
	char s[MAX_JSON_MSG_LEN];
	struct timespec start;
	struct timespec end;
	struct timespec diff;
	double rate;

#define TEST_ITERATIONS 100000

	printf("Benchmarking... %d iterations \n", TEST_ITERATIONS);

	err = mq_ws_init();
	assert(!err);

	err = mq_ws_consumer_subscribe(&id);
	assert(!err);

	clock_gettime(CLOCK_MONOTONIC, &start);

	j = TEST_ITERATIONS;
	while (j--) {

		/* fill up the queue */
		i = 0;
		do {
			int msg_id = j * i;
			err =
			    mq_ws_produce(benchmark_produce, &msg_id, &cb_err);
			if (!err) {
				i++;
			}
		} while (!err);

		/* we hava a full message queue, now consume it all */
		for (; i > 0; i--) {
			err = mq_ws_consume(id, benchmark_consumer, s, &cb_err);
			assert(!err);
		}

		/* queue must be empty and consuming from an empty queue
		 *  must return error */
		err = mq_ws_consume(id, message_printer, s, &cb_err);
		assert(-JT_WS_MQ_EMPTY == err);
	}
	clock_gettime(CLOCK_MONOTONIC, &end);

	diff = ts_absdiff(start, end);
	rate = TEST_ITERATIONS / ts_to_seconds(diff);

	printf("Rate: %f msgs/s\n", rate);

	err = mq_ws_consumer_unsubscribe(id);
	assert(!err);

	err = mq_ws_destroy();
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
	assert(0 == benchmark());

	printf("message queue tests passed.\n");
	return 0;
}
