/*
 * This is a reusable message queue.
 * Use it by creating a new module that:
 * 0. uses a new namespace to prepend to common symbols, eg. mq_stats_
 * 1. defines the NS macro, eg: #define NS(name) PRIMITIVE_CAT(mq_stats_, name)
 * 2. defines MAX_CONSUMERS, MAX_Q_DEPTH and struct NS(msg) in a header file
 * 3. include this C file in the new module's C file.
 */

#ifndef NS
#error "NS macro must be defined."
#endif

#define _POSIX_C_SOURCE 200809L
#include <sys/time.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <syslog.h>
#include <pthread.h>

#include "mq_generic.h"

#define BUF_BYTE_LEN (MAX_Q_DEPTH * sizeof(struct NS(msg)))

static pthread_mutex_t mq_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct NS(msg) *queue = NULL;
static struct NS(msg) * produce_ptr;

static struct NS(msg) * consumer_ptrs[MAX_CONSUMERS] = { 0 };
static int consumer_count = 0;

/* Track dropped and delivered messages per consumer for adaptive tier logic */
static unsigned int consumer_dropped[MAX_CONSUMERS] = { 0 };
static unsigned int consumer_delivered[MAX_CONSUMERS] = { 0 };
static unsigned int consumer_drops_pending[MAX_CONSUMERS] = { 0 };  /* drops since last query */
static unsigned int consumer_delivered_pending[MAX_CONSUMERS] = { 0 };  /* delivered since last query */

/* lets start at non-zero to make sure our array indices are correct. */
static unsigned long consumer_id_start = 42424242;
static const char *qname;


int NS(init)(const char *mq_name)
{
	pthread_mutex_lock(&mq_mutex);
	assert(!queue);
	if (0 == consumer_count) {
		qname = mq_name;
		syslog(LOG_INFO, "creating new queue (%s) holding up to %d messages of %lu bytes each\n",
		       qname, MAX_Q_DEPTH - 1, sizeof(struct NS(msg)));

		queue = malloc(BUF_BYTE_LEN);
		assert(queue);
		produce_ptr = queue;
	}
	pthread_mutex_unlock(&mq_mutex);

	assert(produce_ptr);

	return consumer_count;
}

int NS(destroy)(void)
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

int NS(consumer_subscribe)(unsigned long *subscriber_id)
{
	int real_id = -1;

	pthread_mutex_lock(&mq_mutex);

	if (MAX_CONSUMERS == consumer_count) {
		*subscriber_id = 0;
		syslog(LOG_WARNING,
		       "[%s] consumer limit reached (%d), rejecting connection\n",
		       qname, MAX_CONSUMERS);
		pthread_mutex_unlock(&mq_mutex);
		return -JT_WS_MQ_CONSUMER_LIMIT;
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
	consumer_dropped[real_id] = 0;
	consumer_delivered[real_id] = 0;
	consumer_drops_pending[real_id] = 0;
	consumer_delivered_pending[real_id] = 0;
	*subscriber_id = real_id + consumer_id_start;
	consumer_count++;

	pthread_mutex_unlock(&mq_mutex);

	syslog(LOG_INFO, "[%s] consumer %lu joined (total: %d/%d)\n",
	       qname, *subscriber_id, consumer_count, MAX_CONSUMERS);

	return 0;
}

int NS(consumer_unsubscribe)(unsigned long subscriber_id)
{
	int real_id = subscriber_id - consumer_id_start;
	unsigned int dropped;

	assert(real_id >= 0);
	assert(real_id < MAX_CONSUMERS);

	pthread_mutex_lock(&mq_mutex);

	assert(consumer_count);
	assert(consumer_ptrs[real_id]);

	dropped = consumer_dropped[real_id];
	consumer_ptrs[real_id] = NULL;
	consumer_dropped[real_id] = 0;
	consumer_delivered[real_id] = 0;
	consumer_drops_pending[real_id] = 0;
	consumer_delivered_pending[real_id] = 0;
	consumer_count--;

	pthread_mutex_unlock(&mq_mutex);

	if (dropped > 0) {
		syslog(LOG_INFO,
		       "[%s] consumer %lu left (dropped %u messages, %d remaining)\n",
		       qname, subscriber_id, dropped, consumer_count);
	} else {
		syslog(LOG_INFO, "[%s] consumer %lu left (%d remaining)\n",
		       qname, subscriber_id, consumer_count);
	}

	return 0;
}

int NS(produce)(NS(callback) cb, void *cb_data, int *cb_err)
{
	struct NS(msg) * next;
	struct NS(msg) * skip_to;

	assert(cb_err);

	pthread_mutex_lock(&mq_mutex);
	assert(NULL != produce_ptr);

	/* check if there are any consumers, to prevent queue overflow
	 * and ensure that you can produce and consume exactly the same number
	 * of messages. */
	if (0 == consumer_count) {
		pthread_mutex_unlock(&mq_mutex);
		return -JT_WS_MQ_NO_CONSUMERS;
	}

	next = produce_ptr + 1;
	if ((uint8_t *)next >= ((uint8_t *)queue + BUF_BYTE_LEN)) {
		/* end of buffer, wrap around */
		next = queue;
	}

	/* Calculate where slow consumers should skip to (one past next) */
	skip_to = next + 1;
	if ((uint8_t *)skip_to >= ((uint8_t *)queue + BUF_BYTE_LEN)) {
		skip_to = queue;
	}

	/* Check each consumer - skip slow ones instead of blocking */
	int i;
	for (i = 0; i < MAX_CONSUMERS; i++) {
		if (consumer_ptrs[i] == NULL) {
			continue;
		}
		if (next == consumer_ptrs[i]) {
			/* This consumer is slow - skip them forward */
			consumer_ptrs[i] = skip_to;
			consumer_dropped[i]++;
			consumer_drops_pending[i]++;

			/* Log first drop */
			if (consumer_dropped[i] == 1) {
				syslog(LOG_DEBUG,
				       "[%s] consumer %d falling behind, dropping messages\n",
				       qname, i);
			}
		}
	}

	*cb_err = cb(next, cb_data);
	if (*cb_err) {
		pthread_mutex_unlock(&mq_mutex);
		return -JT_WS_MQ_CB_ERR;
	}

	produce_ptr = next;
	assert((uint8_t *)produce_ptr < ((uint8_t *)queue + BUF_BYTE_LEN));
	pthread_mutex_unlock(&mq_mutex);
	return 0;
}

int NS(consume)(unsigned long id, NS(callback) cb, void *cb_data, int *cb_err)
{
	struct NS(msg) * next;
	int real_id = id - consumer_id_start;

	assert(real_id >= 0);
	assert(real_id < MAX_CONSUMERS);
	assert(cb_err);

	pthread_mutex_lock(&mq_mutex);

	assert(consumer_ptrs[real_id]);

	/* check if queue is empty */
	if (consumer_ptrs[real_id] == produce_ptr) {
		pthread_mutex_unlock(&mq_mutex);
		return -JT_WS_MQ_EMPTY;
	}

	next = consumer_ptrs[real_id] + 1;
	if ((uint8_t *)next == ((uint8_t *)queue + BUF_BYTE_LEN)) {
		next = queue;
	}

	/* call the callback */
	*cb_err = cb(next, cb_data);
	if (*cb_err) {
		pthread_mutex_unlock(&mq_mutex);
		return -JT_WS_MQ_CB_ERR;
	}

	/* callback success, message consumed. */
	consumer_ptrs[real_id] = next;
	consumer_delivered[real_id]++;
	consumer_delivered_pending[real_id]++;

	/* If consumer has caught up (queue now empty for them), reset drop counter.
	 * This allows transient slowdowns without permanent penalty. */
	if (next == produce_ptr && consumer_dropped[real_id] > 0) {
		syslog(LOG_DEBUG,
		       "[%s] consumer %d caught up, resetting drop count (was %u)\n",
		       qname, real_id, consumer_dropped[real_id]);
		consumer_dropped[real_id] = 0;
	}

	pthread_mutex_unlock(&mq_mutex);
	return 0;
}

int NS(maxlen)(void)
{
	return MAX_Q_DEPTH;
}

unsigned int NS(consumer_dropped_count)(unsigned long subscriber_id)
{
	int real_id = subscriber_id - consumer_id_start;
	unsigned int dropped;

	if (real_id < 0 || real_id >= MAX_CONSUMERS) {
		return 0;
	}

	pthread_mutex_lock(&mq_mutex);
	dropped = consumer_dropped[real_id];
	pthread_mutex_unlock(&mq_mutex);

	return dropped;
}

unsigned int NS(consumer_get_and_clear_drops)(unsigned long subscriber_id)
{
	int real_id = subscriber_id - consumer_id_start;
	unsigned int drops;

	if (real_id < 0 || real_id >= MAX_CONSUMERS) {
		return 0;
	}

	pthread_mutex_lock(&mq_mutex);
	drops = consumer_drops_pending[real_id];
	consumer_drops_pending[real_id] = 0;
	pthread_mutex_unlock(&mq_mutex);

	return drops;
}

/* Get and clear both dropped and delivered counts for drop percentage calculation.
 * Returns dropped count, writes delivered count to *delivered_out. */
unsigned int NS(consumer_get_and_clear_stats)(unsigned long subscriber_id,
                                              unsigned int *delivered_out)
{
	int real_id = subscriber_id - consumer_id_start;
	unsigned int drops;

	if (real_id < 0 || real_id >= MAX_CONSUMERS) {
		if (delivered_out)
			*delivered_out = 0;
		return 0;
	}

	pthread_mutex_lock(&mq_mutex);
	drops = consumer_drops_pending[real_id];
	consumer_drops_pending[real_id] = 0;
	if (delivered_out) {
		*delivered_out = consumer_delivered_pending[real_id];
		consumer_delivered_pending[real_id] = 0;
	}
	pthread_mutex_unlock(&mq_mutex);

	return drops;
}
