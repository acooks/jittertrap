#define _POSIX_C_SOURCE 200809L
#include <sys/time.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

#include "websocket_message_queue.h"

#define BUF_BYTE_LEN (MAX_Q_DEPTH * sizeof(struct jt_ws_msg))

pthread_mutex_t mq_mutex;

static struct jt_ws_msg *queue;
static struct jt_ws_msg *produce_ptr;
static struct jt_ws_msg *consume_ptr;

void jt_ws_mq_init()
{
	printf("creating new queue for %d messages of %lu bytes each\n",
	       MAX_Q_DEPTH - 1, sizeof(struct jt_ws_msg));

	queue = malloc(BUF_BYTE_LEN);
	assert(queue);
	produce_ptr = queue;
	consume_ptr = queue;
}

void jt_ws_mq_destroy()
{
	assert(queue);
	produce_ptr = NULL;
	consume_ptr = NULL;
	free(queue);
}

struct jt_ws_msg *jt_ws_mq_produce_next()
{
	struct jt_ws_msg *next;

	pthread_mutex_lock(&mq_mutex);

	next = produce_ptr + 1;
	if ((uint8_t *)next == ((uint8_t *)queue + BUF_BYTE_LEN)) {
		/* end of buffer, wrap around */
		next = queue;
	}

	/* check if queue is full */
	if (next == consume_ptr) {
		pthread_mutex_unlock(&mq_mutex);
		return NULL;
	}

	produce_ptr++;
	if ((uint8_t *)produce_ptr == ((uint8_t *)queue + BUF_BYTE_LEN)) {
		/* end of buffer, wrap around */
		produce_ptr = queue;
	}

	/* check one element of separation */
	/* FIXME: needs some kind of lock instead */
	assert(produce_ptr != consume_ptr);
	pthread_mutex_unlock(&mq_mutex);
	assert((uint8_t *)produce_ptr < ((uint8_t *)queue + BUF_BYTE_LEN));
	return produce_ptr;
}

int jt_ws_mq_produce(jt_ws_mq_callback cb, void *data)
{
	int err;
	struct jt_ws_msg *next;

	pthread_mutex_lock(&mq_mutex);

	next = produce_ptr + 1;
	if ((uint8_t *)next >= ((uint8_t *)queue + BUF_BYTE_LEN)) {
		/* end of buffer, wrap around */
		next = queue;
	}

	/* check if queue is full */
	if (next == consume_ptr) {
		pthread_mutex_unlock(&mq_mutex);
		return -1;
	}

	err = cb(next, data);
	if (!err) {
		produce_ptr = next;
	}

	/* check one element of separation */
	/* FIXME: needs some kind of lock instead */
	assert(produce_ptr != consume_ptr);
	pthread_mutex_unlock(&mq_mutex);
	assert((uint8_t *)produce_ptr < ((uint8_t *)queue + BUF_BYTE_LEN));
	return err;
}

struct jt_ws_msg *jt_ws_mq_consume_next()
{
	pthread_mutex_lock(&mq_mutex);

	/* check if queue is empty */
	if (consume_ptr == produce_ptr) {
		pthread_mutex_unlock(&mq_mutex);
		return NULL;
	}

	consume_ptr++;
	if ((uint8_t *)consume_ptr == ((uint8_t *)queue + BUF_BYTE_LEN)) {
		consume_ptr = queue;
	}

	pthread_mutex_unlock(&mq_mutex);
	return consume_ptr;
}

int jt_ws_mq_consume(jt_ws_mq_callback cb, void *data)
{
	int err;
	struct jt_ws_msg *next;

	pthread_mutex_lock(&mq_mutex);

	/* check if queue is empty */
	if (consume_ptr == produce_ptr) {
		pthread_mutex_unlock(&mq_mutex);
		return -1;
	}

	next = consume_ptr + 1;
	if ((uint8_t *)next == ((uint8_t *)queue + BUF_BYTE_LEN)) {
		next = queue;
	}

	/* call the callback */
	err = cb(next, data);
	if (!err) {
		/* callback success, message consumed. */
		consume_ptr = next;
	}

	pthread_mutex_unlock(&mq_mutex);
	return err;
}
