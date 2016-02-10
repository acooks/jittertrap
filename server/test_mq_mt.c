#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "mq_generic.h"
#include "mq_msg_ws.h"

#define TEST_ITERATIONS 1000000

#define DO_PRODUCER_SLEEP 0
#define DO_CONSUMER_SLEEP 0

#define PRINT_TAIL 0

int message_producer(struct jtmq_msg *m, void *data)
{
	char *s = (char *)data;
	snprintf(m->m, MAX_JSON_MSG_LEN, "%s", s);
	return 0;
}

/* a callback for consuming messages. */
int message_printer(struct jtmq_msg *m, void *data __attribute__((unused)))
{
	assert(m);
	printf("m: %s\n", m->m);
	return 0;
}

int string_copier(struct jtmq_msg *m, void *data)
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

void *produce(void *d __attribute__((unused)))
{
	char msg[MAX_JSON_MSG_LEN];
	int err, cb_err;
	struct timespec deadline;

	printf("new producer for %d messages.\n", TEST_ITERATIONS);

	clock_gettime(CLOCK_MONOTONIC, &deadline);

	int i = TEST_ITERATIONS;
	while (i) {
#if DO_PRODUCER_SLEEP
		int r = rand() % 100;
		nsleep(100 + r);
#endif

		sprintf(msg, "%d", i);

		err = jtmq_produce(message_producer, msg, &cb_err);
		if (!err) {
#if PRINT_TAIL
			if (i < PRINT_TAIL) {
				printf("p%03d\n", i);
			}
#endif
			i--;
		}
	}
	assert(!err);
	printf("producer done.\n");
	return NULL;
}

void *consume(void *_tid)
{
	char msg[MAX_JSON_MSG_LEN];
	int err, cb_err;
	unsigned long id;
	struct timespec deadline;
	int tid = *(int *)_tid;
	char space[MAX_JSON_MSG_LEN];
	memset(space, ' ', 20 * tid);

	err = jtmq_consumer_subscribe(&id);
	assert(!err);

	printf("consumer: %d mq id: %lu for %d messages\n", tid, id,
	       TEST_ITERATIONS);

	clock_gettime(CLOCK_MONOTONIC, &deadline);

	int i = TEST_ITERATIONS;
	while (i) {
#if DO_CONSUMER_SLEEP
		int r = rand() % 100;
		nsleep(100 + r);
#endif

		err = jtmq_consume(id, string_copier, msg, &cb_err);
		if (!err) {
#if PRINT_TAIL
			if (i < PRINT_TAIL) {
				printf("%sc%d-%03d-%s\n", space, tid, i, msg);
			}
#endif
			i--;
		}
	}

	assert(!err);

	err = jtmq_consumer_unsubscribe(id);
	assert(!err);

	return NULL;
}

int main()
{
	int x, y, err;
	struct timespec start;
	struct timespec end;
	struct timespec diff;
	double rate;

	pthread_t producer_thread;
	pthread_t consumer_thread1;
	pthread_t consumer_thread2;

	err = jtmq_init();
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

	diff = ts_absdiff(start, end);
	rate = TEST_ITERATIONS / ts_to_seconds(diff);

	printf("message queue OK. %f msgs/s\n", rate);

	err = jtmq_destroy();
	assert(!err);

	return 0;
}
