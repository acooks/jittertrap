#define _POSIX_C_SOURCE 200809L
#include <sys/time.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

#include "jt_ws_mq_config.h"
#include "jt_ws_mq.h"

#define BUF_BYTE_LEN (MAX_Q_DEPTH * sizeof(struct jt_ws_msg))

static pthread_mutex_t mq_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct jt_ws_msg *queue = NULL;
static struct jt_ws_msg *produce_ptr;

static struct jt_ws_msg *consumer_ptrs[MAX_CONSUMERS] = { 0 };
static int consumer_count = 0;

/* lets start at non-zero to make sure our array indices are correct. */
static unsigned long consumer_id_start = 42424242;

int jt_ws_mq_init()
{
	pthread_mutex_lock(&mq_mutex);
	assert(!queue);
	if (0 == consumer_count) {
		printf("creating new queue for %d messages of %lu bytes each\n",
		       MAX_Q_DEPTH - 1, sizeof(struct jt_ws_msg));

		queue = malloc(BUF_BYTE_LEN);
		assert(queue);
		produce_ptr = queue;
	}
	pthread_mutex_unlock(&mq_mutex);

	assert(produce_ptr);

	return consumer_count;
}

int jt_ws_mq_destroy()
{
	pthread_mutex_lock(&mq_mutex);
	assert(queue);

	if (0 == consumer_count) {
		produce_ptr = NULL;
		free(queue);
		queue = NULL;
	}

	pthread_mutex_unlock(&mq_mutex);
	return consumer_count;
}

int jt_ws_mq_consumer_subscribe(unsigned long *subscriber_id)
{
	int real_id = -1;

	pthread_mutex_lock(&mq_mutex);

	if (MAX_CONSUMERS == consumer_count) {
		pthread_mutex_unlock(&mq_mutex);
		return -1;
	}

	/* find the next open slot */
	for (int i = 0; i < MAX_CONSUMERS; i++) {
		if (NULL == consumer_ptrs[i]) {
			real_id = i;
			break;
		}
	}

	assert(real_id >= 0);
	assert(real_id < MAX_CONSUMERS);

	consumer_ptrs[real_id] = produce_ptr;
	*subscriber_id = real_id + consumer_id_start;
	consumer_count++;

	pthread_mutex_unlock(&mq_mutex);

	printf("consumer %lu joined\n", *subscriber_id);

	return 0;
}

int jt_ws_mq_consumer_unsubscribe(unsigned long subscriber_id)
{
	int real_id = subscriber_id - consumer_id_start;

	assert(real_id >= 0);
	assert(real_id < MAX_CONSUMERS);

	pthread_mutex_lock(&mq_mutex);

	assert(consumer_count);
	assert(consumer_ptrs[real_id]);

	consumer_ptrs[real_id] = NULL;
	consumer_count--;

	pthread_mutex_unlock(&mq_mutex);

	printf("consumer %lu left\n", subscriber_id);

	return 0;
}

int jt_ws_mq_produce(jt_ws_mq_callback cb, void *data)
{
	int err;
	struct jt_ws_msg *next;

	pthread_mutex_lock(&mq_mutex);
	assert(NULL != produce_ptr);

	/* check if there are any consumers, to prevent queue overflow
	 * and ensure that you can produce and consume exactly the same number
	 * of messages. */
	if (0 == consumer_count) {
		pthread_mutex_unlock(&mq_mutex);
		return -1;
	}

	next = produce_ptr + 1;
	if ((uint8_t *)next >= ((uint8_t *)queue + BUF_BYTE_LEN)) {
		/* end of buffer, wrap around */
		next = queue;
	}

	/* check if queue is full */
	int i;
	for (i = 0; i < MAX_CONSUMERS; i++) {
		if (next == consumer_ptrs[i]) {
			/* full; one of the consumers haven't consumed this. */
			/* FIXME: must implement purge on disconnect! */
			pthread_mutex_unlock(&mq_mutex);
			return -1;
		}
	}

	err = cb(next, data);
	if (!err) {
		produce_ptr = next;
	}

	pthread_mutex_unlock(&mq_mutex);
	assert((uint8_t *)produce_ptr < ((uint8_t *)queue + BUF_BYTE_LEN));
	return err;
}

int jt_ws_mq_consume(unsigned long id, jt_ws_mq_callback cb, void *data)
{
	int err;
	struct jt_ws_msg *next;
	int real_id = id - consumer_id_start;

	assert(real_id >= 0);
	assert(real_id < MAX_CONSUMERS);

	pthread_mutex_lock(&mq_mutex);

	assert(consumer_ptrs[real_id]);

	/* check if queue is empty */
	if (consumer_ptrs[real_id] == produce_ptr) {
		pthread_mutex_unlock(&mq_mutex);
		return -1;
	}

	next = consumer_ptrs[real_id] + 1;
	if ((uint8_t *)next == ((uint8_t *)queue + BUF_BYTE_LEN)) {
		next = queue;
	}

	/* call the callback */
	err = cb(next, data);
	if (!err) {
		/* callback success, message consumed. */
		consumer_ptrs[real_id] = next;
	}

	pthread_mutex_unlock(&mq_mutex);
	return err;
}
