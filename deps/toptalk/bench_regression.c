/*
 * bench_regression.c - Performance regression tests
 *
 * Verifies that key operations meet minimum performance thresholds.
 * Run with: make bench-regression && ./bench-regression
 *
 * Returns 0 if all tests pass, 1 if any test fails.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <pcap.h>

#include "bench_common.h"
#include "flow.h"
#include "decode.h"
#include "uthash.h"

/* Test thresholds (cycles per operation) - set conservatively high */
#define DECODE_THRESHOLD_CYCLES      500    /* Header parsing */
#define MALLOC_THRESHOLD_CYCLES      200    /* Allocation overhead */
#define RINGBUF_THRESHOLD_CYCLES     100    /* Ring buffer ops including memset */
#define TOPN_THRESHOLD_CYCLES       5000    /* Top-N selection for 100 flows */

/* Test iteration counts - enough for stable measurements */
#define DECODE_ITERATIONS     10000
#define MALLOC_ITERATIONS    100000
#define TOPN_ITERATIONS        1000

static int tests_failed = 0;

/* Synthetic TCP packet for decode test */
static uint8_t test_packet[128];
static struct pcap_pkthdr test_pkthdr;

static void build_test_packet(void)
{
	memset(test_packet, 0, sizeof(test_packet));

	/* Ethernet header (14 bytes) */
	uint8_t *eth = test_packet;
	eth[12] = 0x08;  /* EtherType: IPv4 (0x0800) */
	eth[13] = 0x00;

	/* IPv4 header (20 bytes) at offset 14 */
	uint8_t *ip4 = test_packet + 14;
	ip4[0] = 0x45;         /* Version 4, IHL 5 (20 bytes) */
	ip4[1] = 0x00;         /* DSCP/ECN */
	ip4[2] = 0x00;         /* Total length: 66 (20 IP + 20 TCP + 26 payload) */
	ip4[3] = 0x42;
	ip4[8] = 0x40;         /* TTL */
	ip4[9] = 0x06;         /* Protocol: TCP */
	/* Source IP: 10.0.0.1 */
	ip4[12] = 10; ip4[13] = 0; ip4[14] = 0; ip4[15] = 1;
	/* Dest IP: 10.0.0.2 */
	ip4[16] = 10; ip4[17] = 0; ip4[18] = 0; ip4[19] = 2;

	/* TCP header (20 bytes) at offset 34 */
	uint8_t *tcp = test_packet + 34;
	tcp[0] = 0x04;  /* Source port: 1234 */
	tcp[1] = 0xD2;
	tcp[2] = 0x00;  /* Dest port: 80 */
	tcp[3] = 0x50;
	/* Sequence number */
	tcp[4] = 0x00; tcp[5] = 0x00; tcp[6] = 0x10; tcp[7] = 0x00;
	/* Ack number */
	tcp[8] = 0x00; tcp[9] = 0x00; tcp[10] = 0x20; tcp[11] = 0x00;
	tcp[12] = 0x50;  /* Data offset: 5 (20 bytes), flags: 0 */
	tcp[13] = 0x10;  /* ACK flag */
	tcp[14] = 0xFF;  /* Window: 65535 */
	tcp[15] = 0xFF;

	/* Payload starts at offset 54 */
	memset(test_packet + 54, 'A', 26);

	/* Pcap header */
	test_pkthdr.ts.tv_sec = 0;
	test_pkthdr.ts.tv_usec = 0;
	test_pkthdr.caplen = 80;
	test_pkthdr.len = 80;
}

/*
 * Test 1: Decode performance
 */
static void bench_decode_fn(void *arg)
{
	(void)arg;
	struct flow_pkt pkt;
	char errbuf[256];
	int ret = decode_ethernet(&test_pkthdr, test_packet, &pkt, errbuf);
	BENCH_DONT_OPTIMIZE(ret);
	BENCH_DONT_OPTIMIZE(pkt.flow_rec.flow.proto);
}

static int test_decode_performance(void)
{
	struct bench_result r;

	build_test_packet();
	bench_run("decode_ethernet", bench_decode_fn, NULL, DECODE_ITERATIONS, &r);

	printf("  decode: %.1f cycles/op (threshold: %d)\n",
	       r.cycles_per_op, DECODE_THRESHOLD_CYCLES);

	if (r.cycles_per_op > DECODE_THRESHOLD_CYCLES) {
		printf("  FAIL: decode too slow\n");
		return 1;
	}
	return 0;
}

/*
 * Test 2: Ring buffer performance (simulated)
 */
#define RING_SIZE 4096
static uint8_t ring_buffer[RING_SIZE][600];
static uint32_t ring_head = 0;

static void bench_ringbuf_fn(void *arg)
{
	(void)arg;
	void *p = ring_buffer[ring_head & (RING_SIZE - 1)];
	ring_head++;
	BENCH_DONT_OPTIMIZE(p);
	memset(p, 0, 64);
}

static int test_ringbuf_performance(void)
{
	struct bench_result r;

	ring_head = 0;
	bench_run("ring_buffer", bench_ringbuf_fn, NULL, MALLOC_ITERATIONS, &r);

	printf("  ring buffer: %.1f cycles/op (threshold: %d)\n",
	       r.cycles_per_op, RINGBUF_THRESHOLD_CYCLES);

	if (r.cycles_per_op > RINGBUF_THRESHOLD_CYCLES) {
		printf("  FAIL: ring buffer too slow\n");
		return 1;
	}
	return 0;
}

/*
 * Test 3: Top-N selection performance
 */
struct topn_flow_hash {
	struct flow flow;
	int64_t bytes;
	UT_hash_handle hh;
};

static struct topn_flow_hash *topn_table = NULL;
#define TOPN_FLOWS 100
#define TOPN_N 10

static struct topn_flow_hash *topn_result[TOPN_N];

static void populate_topn_table(void)
{
	for (int i = 0; i < TOPN_FLOWS; i++) {
		struct topn_flow_hash *entry = calloc(1, sizeof(*entry));
		entry->flow.ethertype = 0x0800;
		entry->flow.src_ip.s_addr = htonl(0x0a000001 + i);
		entry->flow.dst_ip.s_addr = htonl(0x0a000100);
		entry->flow.sport = 1024 + i;
		entry->flow.dport = 80;
		entry->bytes = rand() % 1000000;
		HASH_ADD(hh, topn_table, flow, sizeof(struct flow), entry);
	}
}

static void free_topn_table(void)
{
	struct topn_flow_hash *entry, *tmp;
	HASH_ITER(hh, topn_table, entry, tmp) {
		HASH_DEL(topn_table, entry);
		free(entry);
	}
	topn_table = NULL;
}

static void find_top_n(void)
{
	struct topn_flow_hash *iter, *tmp;
	int count = 0;

	HASH_ITER(hh, topn_table, iter, tmp) {
		if (count < TOPN_N) {
			topn_result[count++] = iter;
			for (int i = count - 1; i > 0; i--) {
				if (topn_result[i]->bytes > topn_result[i-1]->bytes) {
					struct topn_flow_hash *t = topn_result[i];
					topn_result[i] = topn_result[i-1];
					topn_result[i-1] = t;
				}
			}
		} else if (iter->bytes > topn_result[TOPN_N-1]->bytes) {
			topn_result[TOPN_N-1] = iter;
			for (int i = TOPN_N - 1; i > 0; i--) {
				if (topn_result[i]->bytes > topn_result[i-1]->bytes) {
					struct topn_flow_hash *t = topn_result[i];
					topn_result[i] = topn_result[i-1];
					topn_result[i-1] = t;
				}
			}
		}
	}
}

static void bench_topn_fn(void *arg)
{
	(void)arg;
	find_top_n();
	BENCH_DONT_OPTIMIZE(topn_result[0]);
}

static int test_topn_performance(void)
{
	struct bench_result r;

	srand(42);
	populate_topn_table();

	bench_run("top-N selection", bench_topn_fn, NULL, TOPN_ITERATIONS, &r);

	free_topn_table();

	printf("  top-N: %.1f cycles/op (threshold: %d)\n",
	       r.cycles_per_op, TOPN_THRESHOLD_CYCLES);

	if (r.cycles_per_op > TOPN_THRESHOLD_CYCLES) {
		printf("  FAIL: top-N selection too slow\n");
		return 1;
	}
	return 0;
}

int main(void)
{
	printf("\n=== Performance Regression Tests ===\n\n");

	printf("Test 1: Header decoding\n");
	tests_failed += test_decode_performance();

	printf("\nTest 2: Ring buffer allocation\n");
	tests_failed += test_ringbuf_performance();

	printf("\nTest 3: Top-N flow selection\n");
	tests_failed += test_topn_performance();

	printf("\n=== Results: %d test(s) failed ===\n",
	       tests_failed);

	return tests_failed > 0 ? 1 : 0;
}
