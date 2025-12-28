/*
 * bench_malloc.c - Benchmark for per-packet allocation overhead
 *
 * Measures the cost of:
 * 1. malloc + free per packet (current behavior)
 * 2. Ring buffer slot reuse (proposed optimization)
 * 3. Pool allocator (alternative)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_common.h"
#include "flow.h"

/* Size of flow_pkt_list structure (approximate) */
#define ALLOC_SIZE 600

/* Ring buffer for proposed optimization */
#define RING_SIZE 4096
static uint8_t ring_buffer[RING_SIZE][ALLOC_SIZE];
static uint32_t ring_head = 0;

/* Simple pool allocator for comparison */
#define POOL_SIZE 4096
static uint8_t pool_buffer[POOL_SIZE][ALLOC_SIZE];
static uint8_t *pool_free_list[POOL_SIZE];
static int pool_free_count = POOL_SIZE;

static void pool_init(void)
{
	for (int i = 0; i < POOL_SIZE; i++) {
		pool_free_list[i] = pool_buffer[i];
	}
	pool_free_count = POOL_SIZE;
}

static void *pool_alloc(void)
{
	if (pool_free_count == 0)
		return NULL;
	return pool_free_list[--pool_free_count];
}

static void pool_free(void *ptr)
{
	if (pool_free_count < POOL_SIZE)
		pool_free_list[pool_free_count++] = ptr;
}

/* Benchmark 1: malloc + free (current behavior) */
static void bench_malloc_free(void *arg)
{
	(void)arg;
	void *p = malloc(ALLOC_SIZE);
	BENCH_DONT_OPTIMIZE(p);
	/* Touch memory to ensure it's allocated */
	memset(p, 0, 64);
	free(p);
}

/* Benchmark 2: Ring buffer slot reuse (proposed) */
static void bench_ring_buffer(void *arg)
{
	(void)arg;
	void *p = ring_buffer[ring_head & (RING_SIZE - 1)];
	ring_head++;
	BENCH_DONT_OPTIMIZE(p);
	/* Touch memory like we would in real usage */
	memset(p, 0, 64);
	/* No free needed - slot is reused on wrap */
}

/* Benchmark 3: Pool allocator */
static void bench_pool_alloc(void *arg)
{
	(void)arg;
	void *p = pool_alloc();
	BENCH_DONT_OPTIMIZE(p);
	if (p) {
		memset(p, 0, 64);
		pool_free(p);
	}
}

int main(void)
{
	struct bench_result r1, r2, r3;
	const uint64_t iterations = 1000000;

	printf("\n=== Per-Packet Allocation Benchmark ===\n");
	printf("Allocation size: %d bytes (approx flow_pkt_list)\n", ALLOC_SIZE);

	pool_init();

	bench_report_header();

	bench_run("malloc + free (current)", bench_malloc_free, NULL,
	          iterations, &r1);
	bench_report(&r1);

	bench_run("ring buffer (proposed)", bench_ring_buffer, NULL,
	          iterations, &r2);
	bench_report(&r2);

	bench_run("pool allocator (alternative)", bench_pool_alloc, NULL,
	          iterations, &r3);
	bench_report(&r3);

	printf("\n--- Analysis ---\n");
	printf("malloc/free cost:    %.1f cycles (%.1f ns)\n",
	       r1.cycles_per_op, r1.ns_per_op);
	printf("Ring buffer cost:    %.1f cycles (%.1f ns)\n",
	       r2.cycles_per_op, r2.ns_per_op);
	printf("Pool allocator cost: %.1f cycles (%.1f ns)\n",
	       r3.cycles_per_op, r3.ns_per_op);
	printf("Ring buffer savings: %.1f%% vs malloc\n",
	       100.0 * (r1.cycles_per_op - r2.cycles_per_op) / r1.cycles_per_op);

	return 0;
}
