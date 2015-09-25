#define _POSIX_C_SOURCE 200809L
#include <sys/time.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

#include "jittertrap.h"
#include "iface_stats.h"
#include "stats_thread.h"
#include "sample_buf.h"

/* a circular buffer with one element of separation */
#define CIRC_BUF_SIZE 3
#define BUF_BYTE_LEN (CIRC_BUF_SIZE * sizeof(struct iface_stats))

pthread_mutex_t pc_mutex;

struct iface_stats *sample_buf;
struct iface_stats *produce_ptr;
struct iface_stats *consume_ptr;

void raw_sample_buf_init()
{
	sample_buf = malloc(BUF_BYTE_LEN);
	assert(sample_buf);
	produce_ptr = sample_buf;
	consume_ptr = sample_buf;
}

struct iface_stats *raw_sample_buf_produce_next()
{
	pthread_mutex_lock(&pc_mutex);
	produce_ptr++;
	if ((uint8_t *)produce_ptr == ((uint8_t *)sample_buf + BUF_BYTE_LEN)) {
		/* end of buffer, wrap around */
		produce_ptr = sample_buf;
	}

	/* check one element of separation */
	/* FIXME: needs some kind of lock instead */
	assert(produce_ptr != consume_ptr);
	pthread_mutex_unlock(&pc_mutex);
	assert((uint8_t *)produce_ptr < ((uint8_t *)sample_buf + BUF_BYTE_LEN));
	return produce_ptr;
}

struct iface_stats *raw_sample_buf_consume_next()
{
	pthread_mutex_lock(&pc_mutex);
	consume_ptr++;
	if ((uint8_t *)consume_ptr == ((uint8_t *)sample_buf + BUF_BYTE_LEN)) {
		consume_ptr = sample_buf;
	}

	assert(consume_ptr != produce_ptr);
	pthread_mutex_unlock(&pc_mutex);
	return consume_ptr;
}
