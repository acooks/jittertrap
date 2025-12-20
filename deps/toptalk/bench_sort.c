/*
 * bench_sort.c - Benchmark for flow sorting
 *
 * Measures the cost of:
 * 1. HASH_SRT (full sort every time - current behavior)
 * 2. Partial sort / selection algorithm (proposed)
 * 3. Incremental heap tracking (alternative)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "bench_common.h"
#include "uthash.h"
#include "flow.h"

/* Flow hash entry for sorting benchmarks */
struct sort_flow_hash {
	struct flow flow;
	int64_t bytes;
	int64_t packets;
	UT_hash_handle hh;
};

/* Tables for different sorting approaches */
static struct sort_flow_hash *hash_srt_table = NULL;
static struct sort_flow_hash *partial_sort_table = NULL;

/* Number of flows and top-N to extract */
#define NUM_FLOWS 100
#define TOP_N 10

/* Comparison function for uthash HASH_SRT */
static int bytes_cmp(struct sort_flow_hash *a, struct sort_flow_hash *b)
{
	/* Descending order */
	return (b->bytes > a->bytes) - (b->bytes < a->bytes);
}

/* Populate a table with randomized byte counts */
static void populate_sort_table(struct sort_flow_hash **table, unsigned int seed)
{
	srand(seed);
	for (int i = 0; i < NUM_FLOWS; i++) {
		struct sort_flow_hash *entry = calloc(1, sizeof(*entry));
		entry->flow.ethertype = 0x0800;
		entry->flow.src_ip.s_addr = htonl(0x0a000001 + i);
		entry->flow.dst_ip.s_addr = htonl(0x0a000100);
		entry->flow.sport = 1024 + i;
		entry->flow.dport = 80;
		entry->bytes = rand() % 1000000;
		entry->packets = entry->bytes / 100;
		HASH_ADD(hh, *table, flow, sizeof(struct flow), entry);
	}
}

/* Free all entries in a table */
static void free_sort_table(struct sort_flow_hash **table)
{
	struct sort_flow_hash *entry, *tmp;
	HASH_ITER(hh, *table, entry, tmp) {
		HASH_DEL(*table, entry);
		free(entry);
	}
	*table = NULL;
}

/* Partial sort: find top N using selection algorithm */
static void find_top_n(struct sort_flow_hash *table,
                       struct sort_flow_hash **top,
                       int n)
{
	/* Simple O(n) approach: track top N in an array */
	int count = 0;
	struct sort_flow_hash *entry, *tmp;

	HASH_ITER(hh, table, entry, tmp) {
		if (count < n) {
			/* Fill top array first */
			top[count++] = entry;
			/* Keep sorted with insertion sort (small array) */
			for (int i = count - 1; i > 0; i--) {
				if (top[i]->bytes > top[i-1]->bytes) {
					struct sort_flow_hash *t = top[i];
					top[i] = top[i-1];
					top[i-1] = t;
				}
			}
		} else if (entry->bytes > top[n-1]->bytes) {
			/* Replace smallest in top */
			top[n-1] = entry;
			/* Re-sort */
			for (int i = n - 1; i > 0; i--) {
				if (top[i]->bytes > top[i-1]->bytes) {
					struct sort_flow_hash *t = top[i];
					top[i] = top[i-1];
					top[i-1] = t;
				}
			}
		}
	}
}

/* Benchmark context */
struct sort_bench_ctx {
	struct sort_flow_hash *top[TOP_N];
};

static struct sort_bench_ctx ctx;

/* Benchmark 1: HASH_SRT (current behavior) */
static void bench_hash_srt(void *arg)
{
	(void)arg;
	HASH_SRT(hh, hash_srt_table, bytes_cmp);

	/* Iterate to get top N (what the real code does) */
	struct sort_flow_hash *entry = hash_srt_table;
	for (int i = 0; i < TOP_N && entry; i++) {
		BENCH_DONT_OPTIMIZE(entry->bytes);
		entry = entry->hh.next;
	}
}

/* Benchmark 2: Partial sort / selection */
static void bench_partial_sort(void *arg)
{
	struct sort_bench_ctx *c = arg;
	find_top_n(partial_sort_table, c->top, TOP_N);

	for (int i = 0; i < TOP_N; i++) {
		BENCH_DONT_OPTIMIZE(c->top[i]->bytes);
	}
}

int main(void)
{
	struct bench_result r1, r2;
	const uint64_t iterations = 1000;

	printf("\n=== Flow Sorting Benchmark ===\n");
	printf("Total flows: %d, extracting top %d\n", NUM_FLOWS, TOP_N);

	/* Create tables with same random data */
	populate_sort_table(&hash_srt_table, 42);
	populate_sort_table(&partial_sort_table, 42);

	bench_report_header();

	bench_run("HASH_SRT full sort (current)", bench_hash_srt, NULL,
	          iterations, &r1);
	bench_report(&r1);

	bench_run("partial sort top-N (proposed)", bench_partial_sort, &ctx,
	          iterations, &r2);
	bench_report(&r2);

	printf("\n--- Analysis ---\n");
	printf("HASH_SRT cost:    %.1f cycles (%.1f ns) per sort\n",
	       r1.cycles_per_op, r1.ns_per_op);
	printf("Partial sort:     %.1f cycles (%.1f ns) per sort\n",
	       r2.cycles_per_op, r2.ns_per_op);
	printf("Improvement:      %.1f%% fewer cycles\n",
	       100.0 * (r1.cycles_per_op - r2.cycles_per_op) / r1.cycles_per_op);

	/* Note: At 1ms tick rate, sorting happens 1000x/sec */
	printf("\nAt 1000 sorts/sec:\n");
	printf("  HASH_SRT:     %.0f us/sec overhead\n",
	       r1.ns_per_op * 1000 / 1000.0);
	printf("  Partial sort: %.0f us/sec overhead\n",
	       r2.ns_per_op * 1000 / 1000.0);

	/* Cleanup */
	free_sort_table(&hash_srt_table);
	free_sort_table(&partial_sort_table);

	return 0;
}
