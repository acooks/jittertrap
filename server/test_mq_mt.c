#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Test using tier 1 queue (5ms interval) as representative */
#include "mq_msg_ws_1.h"


#ifdef SPEED_TEST
/* Benchmark mode: high iterations, no sleeps, tests raw throughput */
#define TEST_ITERATIONS 1000000
#define DO_PRODUCER_SLEEP 0
#define DO_CONSUMER_SLEEP 0
#else
/* Correctness test: moderate iterations with random sleeps to expose races */
#define TEST_ITERATIONS 10000
#define DO_PRODUCER_SLEEP 1
#define DO_CONSUMER_SLEEP 1
#endif

#define PRINT_TAIL 1

int message_producer(struct mq_ws_1_msg *m, void *data)
{
	char *s = (char *)data;
	snprintf(m->m, MAX_JSON_MSG_LEN, "%s", s);
	return 0;
}

/* a callback for consuming messages. */
int message_printer(struct mq_ws_1_msg *m, void *data __attribute__((unused)))
{
	assert(m);
	printf("m: %s\n", m->m);
	return 0;
}

int string_copier(struct mq_ws_1_msg *m, void *data)
{
	char *s;

	assert(m);
	assert(data);

	s = (char *)data;
	sprintf(s, "%s", m->m);
	return 0;
}

/* dont ask, just condemn it. */
void nsleep(unsigned long delay)
{
	struct timespec deadline;

	clock_gettime(CLOCK_MONOTONIC, &deadline);
	deadline.tv_nsec += delay;

	/* account for the second boundary */
	if (deadline.tv_nsec >= 1E9) {
		deadline.tv_nsec -= 1E9;
		deadline.tv_sec++;
	}

	clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
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

/* Shared flag to signal producer is done */
static volatile int producer_done = 0;

void *produce(void *d __attribute__((unused)))
{
	char msg[MAX_JSON_MSG_LEN];
	int err, cb_err;
	struct timespec deadline;
	int produced = 0;

	printf("new producer for %d messages.\n", TEST_ITERATIONS);

	clock_gettime(CLOCK_MONOTONIC, &deadline);

	int i = TEST_ITERATIONS;
	while (i) {
#if DO_PRODUCER_SLEEP
		int r = rand() % 100;
		nsleep(100 + r);
#endif

		sprintf(msg, "%d", i);

		err = mq_ws_1_produce(message_producer, msg, &cb_err);
		if (!err) {
#if PRINT_TAIL
			if (i < PRINT_TAIL) {
				printf("p%03d\n", i);
			}
#endif
			i--;
			produced++;
		} else if (err == -JT_WS_MQ_NO_CONSUMERS) {
			/* All consumers have exited (marked stale) */
			printf("producer: no consumers left after %d messages\n", produced);
			break;
		}

		if (0 == i % 5000) {
			printf("%2d%%\r",
			(int)(TEST_ITERATIONS - i) * 100 / TEST_ITERATIONS);
			fflush(stdout);
		}
	}
	producer_done = 1;
	printf("producer done (%d messages).\n", produced);
	return NULL;
}

void *consume(void *_tid)
{
	char msg[MAX_JSON_MSG_LEN];
	int err, cb_err;
	unsigned long id;
	int tid = *(int *)_tid;
	char space[MAX_JSON_MSG_LEN];
	int consumed = 0;
	unsigned int dropped;
	int empty_count = 0;
	memset(space, ' ', 20 * tid);

	err = mq_ws_1_consumer_subscribe(&id);
	assert(!err);

	printf("consumer: %d mq id: %lu\n", tid, id);

	/* Consume messages until producer is done AND queue is empty for a while */
	while (1) {
#if DO_CONSUMER_SLEEP
		int r = rand() % 100;
		nsleep(100 + r);
#endif

		err = mq_ws_1_consume(id, string_copier, msg, &cb_err);
		if (!err) {
#if PRINT_TAIL
			if (consumed < PRINT_TAIL) {
				printf("%sc%d-%03d-%s\n", space, tid, consumed, msg);
			}
#endif
			consumed++;
			empty_count = 0;
		} else if (err == -JT_WS_MQ_EMPTY) {
			empty_count++;
			/* If producer is done and queue stays empty, we're done */
			if (producer_done && empty_count > 100) {
				break;
			}
		}
	}

	dropped = mq_ws_1_consumer_dropped_count(id);
	printf("consumer %d: received %d, dropped %u\n", tid, consumed, dropped);

	err = mq_ws_1_consumer_unsubscribe(id);
	assert(!err);

	return NULL;
}

int main()
{
	int x, y, err;
	struct timespec start;
	struct timespec end;

	pthread_t producer_thread;
	pthread_t consumer_thread1;
	pthread_t consumer_thread2;

	err = mq_ws_1_init("test");
	assert(!err);

	x = 1;
	if (pthread_create(&consumer_thread1, NULL, consume, &x)) {
		fprintf(stderr, "Error creating consumer thread 1\n");
		return 1;
	}

	y = 2;
	if (pthread_create(&consumer_thread2, NULL, consume, &y)) {
		fprintf(stderr, "Error creating consumer thread 2\n");
		return 1;
	}

	/* always let the producer wait for the consumers to be ready, or
	 * the second consumer can miss messages at the start and wait
	 * forever. */
	nsleep(1000000);

	clock_gettime(CLOCK_MONOTONIC, &start);

	/* start the producer after the consumers, or they will miss the
	 * messages that were qeued before they subscribed and you will
	 * waste a lot of time looking for a bug that doesn't exist.
	 */
	if (pthread_create(&producer_thread, NULL, produce, &x)) {
		fprintf(stderr, "Error creating producer thread\n");
		return 1;
	}

	/* wait for threads */
	if (pthread_join(producer_thread, NULL)) {
		fprintf(stderr, "Error joining thread\n");
		return 2;
	}

	if (pthread_join(consumer_thread1, NULL)) {
		fprintf(stderr, "Error joining consumer thread 1\n");
		return 2;
	}

	if (pthread_join(consumer_thread2, NULL)) {
		fprintf(stderr, "Error joining consumer thread 2\n");
		return 2;
	}

	clock_gettime(CLOCK_MONOTONIC, &end);

	struct timespec diff;
	double rate;
	double elapsed;
	diff = ts_absdiff(start, end);
	elapsed = ts_to_seconds(diff);
	rate = TEST_ITERATIONS / elapsed;

	printf("message queue MT test: %d iterations in %.2fs (%.0f msgs/s)\n",
	       TEST_ITERATIONS, elapsed, rate);

#ifdef SPEED_TEST
	/* For benchmark mode, check minimum performance threshold */
	if (rate < 100000.0) {
		fprintf(stderr, "PERFORMANCE REGRESSION: %.0f msgs/s < 100000 msgs/s threshold\n", rate);
		return 1;
	}
#endif

	err = mq_ws_1_destroy();
	assert(!err);

	return 0;
}
