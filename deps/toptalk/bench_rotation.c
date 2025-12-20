/*
 * bench_rotation.c - Benchmark for interval table rotation
 *
 * Measures the cost of:
 * 1. Copy-based rotation (current clear_table behavior)
 * 2. Pointer swap rotation (proposed optimization)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "bench_common.h"
#include "uthash.h"
#include "flow.h"

/* Simplified flow hash entry for benchmarking */
struct bench_flow_hash {
	struct flow flow;
	int64_t bytes;
	int64_t packets;
	UT_hash_handle hh;
};

/* Tables for copy-based benchmark */
static struct bench_flow_hash *copy_incomplete = NULL;
static struct bench_flow_hash *copy_complete = NULL;

/* Tables for swap-based benchmark */
static struct bench_flow_hash *swap_tables[2] = {NULL, NULL};
static int swap_write_idx = 0;

/* Number of flows to test with */
#define NUM_FLOWS 100

/* Populate a table with NUM_FLOWS entries */
static void populate_table(struct bench_flow_hash **table)
{
	for (int i = 0; i < NUM_FLOWS; i++) {
		struct bench_flow_hash *entry = calloc(1, sizeof(*entry));
		entry->flow.ethertype = 0x0800;
		entry->flow.src_ip.s_addr = htonl(0x0a000001 + i);
		entry->flow.dst_ip.s_addr = htonl(0x0a000100);
		entry->flow.sport = 1024 + i;
		entry->flow.dport = 80;
		entry->bytes = 1000 * (i + 1);
		entry->packets = 10 * (i + 1);
		HASH_ADD(hh, *table, flow, sizeof(struct flow), entry);
	}
}

/* Free all entries in a table */
static void free_table(struct bench_flow_hash **table)
{
	struct bench_flow_hash *entry, *tmp;
	HASH_ITER(hh, *table, entry, tmp) {
		HASH_DEL(*table, entry);
		free(entry);
	}
	*table = NULL;
}

/* Copy-based rotation (current behavior) */
static void rotate_copy(void)
{
	struct bench_flow_hash *entry, *tmp;

	/* Free old complete table */
	free_table(&copy_complete);

	/* Copy incomplete to complete */
	HASH_ITER(hh, copy_incomplete, entry, tmp) {
		struct bench_flow_hash *n = malloc(sizeof(*n));
		memcpy(n, entry, sizeof(*n));
		memset(&n->hh, 0, sizeof(n->hh));
		HASH_ADD(hh, copy_complete, flow, sizeof(struct flow), n);
	}

	/* Free incomplete */
	free_table(&copy_incomplete);
}

/* Swap-based rotation (proposed) */
static void rotate_swap(void)
{
	int read_idx = 1 - swap_write_idx;

	/* Free old read table */
	free_table(&swap_tables[read_idx]);

	/* Swap: write becomes read, allocate new write */
	swap_tables[read_idx] = swap_tables[swap_write_idx];
	swap_tables[swap_write_idx] = NULL;

	/* Flip indices */
	swap_write_idx = read_idx;
}

/* Benchmark 1: Copy-based rotation */
static void bench_copy_rotation(void *arg)
{
	(void)arg;
	/* Re-populate incomplete table for each iteration */
	populate_table(&copy_incomplete);
	rotate_copy();
}

/* Benchmark 2: Swap-based rotation */
static void bench_swap_rotation(void *arg)
{
	(void)arg;
	/* Re-populate write table for each iteration */
	populate_table(&swap_tables[swap_write_idx]);
	rotate_swap();
}

int main(void)
{
	struct bench_result r1, r2;
	const uint64_t iterations = 1000;

	printf("\n=== Interval Table Rotation Benchmark ===\n");
	printf("Flows per table: %d\n", NUM_FLOWS);

	bench_report_header();

	bench_run("copy-based rotation (current)", bench_copy_rotation, NULL,
	          iterations, &r1);
	bench_report(&r1);

	/* Reset for swap benchmark */
	free_table(&swap_tables[0]);
	free_table(&swap_tables[1]);
	swap_write_idx = 0;

	bench_run("swap-based rotation (proposed)", bench_swap_rotation, NULL,
	          iterations, &r2);
	bench_report(&r2);

	printf("\n--- Analysis ---\n");
	printf("Copy rotation:  %.1f cycles (%.1f ns) per rotation\n",
	       r1.cycles_per_op, r1.ns_per_op);
	printf("Swap rotation:  %.1f cycles (%.1f ns) per rotation\n",
	       r2.cycles_per_op, r2.ns_per_op);
	printf("Improvement:    %.1f%% fewer cycles\n",
	       100.0 * (r1.cycles_per_op - r2.cycles_per_op) / r1.cycles_per_op);

	/* Cleanup */
	free_table(&copy_incomplete);
	free_table(&copy_complete);
	free_table(&swap_tables[0]);
	free_table(&swap_tables[1]);

	return 0;
}
