/*
 * bench_decode.c - Benchmark for header parsing overhead
 *
 * Measures the cost of:
 * 1. Single decode pass (decode_ethernet only)
 * 2. Decode + find_tcp_header (current behavior)
 * 3. Decode with stored offset (proposed optimization)
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

/* Synthetic TCP packet: Ethernet + IPv4 + TCP + payload */
static uint8_t test_packet[128];
static struct pcap_pkthdr test_pkthdr;

/* Pre-computed L4 offset for the optimized path */
static uint16_t precomputed_l4_offset;

/* Build a synthetic TCP packet for benchmarking */
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

	/* Pre-compute L4 offset: Ethernet (14) + IPv4 (20) = 34 */
	precomputed_l4_offset = 34;
}

/* Benchmark 1: decode_ethernet only (baseline) */
static void bench_decode_only(void *arg)
{
	(void)arg;
	struct flow_pkt pkt;
	char errbuf[256];

	int ret = decode_ethernet(&test_pkthdr, test_packet, &pkt, errbuf);
	BENCH_DONT_OPTIMIZE(ret);
	BENCH_DONT_OPTIMIZE(pkt.flow_rec.flow.proto);
}

/* Forward declaration - find_tcp_header is static in intervals.c,
 * so we simulate its work here */
static const struct hdr_tcp *find_tcp_header_sim(const uint8_t *packet,
                                                  uint32_t caplen)
{
	/* Simulate the header traversal that find_tcp_header does */
	if (caplen < 14)
		return NULL;

	/* Check ethertype */
	uint16_t ethertype = (packet[12] << 8) | packet[13];
	const uint8_t *ip_start;

	if (ethertype == 0x8100) {
		/* VLAN - skip 4 more bytes */
		ethertype = (packet[16] << 8) | packet[17];
		ip_start = packet + 18;
		caplen -= 18;
	} else {
		ip_start = packet + 14;
		caplen -= 14;
	}

	if (ethertype != 0x0800)  /* Not IPv4 for simplicity */
		return NULL;

	if (caplen < 20)
		return NULL;

	/* IPv4 header length */
	uint8_t ihl = (ip_start[0] & 0x0F) * 4;
	if (caplen < ihl + 20)
		return NULL;

	/* Check protocol is TCP */
	if (ip_start[9] != 6)
		return NULL;

	return (const struct hdr_tcp *)(ip_start + ihl);
}

/* Benchmark 2: decode_ethernet + find_tcp_header (current behavior) */
static void bench_decode_plus_find(void *arg)
{
	(void)arg;
	struct flow_pkt pkt;
	char errbuf[256];

	int ret = decode_ethernet(&test_pkthdr, test_packet, &pkt, errbuf);
	BENCH_DONT_OPTIMIZE(ret);

	/* Simulate what handle_packet does - re-parse to find TCP header */
	const struct hdr_tcp *tcp = find_tcp_header_sim(test_packet,
	                                                 test_pkthdr.caplen);
	BENCH_DONT_OPTIMIZE(tcp);
}

/* Benchmark 3: decode with stored offset (proposed optimization) */
static void bench_decode_with_offset(void *arg)
{
	(void)arg;
	struct flow_pkt pkt;
	char errbuf[256];

	int ret = decode_ethernet(&test_pkthdr, test_packet, &pkt, errbuf);
	BENCH_DONT_OPTIMIZE(ret);

	/* Use pre-computed offset - O(1) pointer arithmetic */
	const struct hdr_tcp *tcp = (const struct hdr_tcp *)(test_packet + precomputed_l4_offset);
	BENCH_DONT_OPTIMIZE(tcp);
}

int main(void)
{
	struct bench_result r1, r2, r3;
	const uint64_t iterations = 100000;

	printf("\n=== Header Parsing Benchmark ===\n");
	printf("Packet: Ethernet + IPv4 + TCP (80 bytes)\n");

	build_test_packet();

	bench_report_header();

	bench_run("decode_ethernet (baseline)", bench_decode_only, NULL,
	          iterations, &r1);
	bench_report(&r1);

	bench_run("decode + find_tcp_header (current)", bench_decode_plus_find, NULL,
	          iterations, &r2);
	bench_report(&r2);

	bench_run("decode + stored offset (proposed)", bench_decode_with_offset, NULL,
	          iterations, &r3);
	bench_report(&r3);

	printf("\n--- Analysis ---\n");
	printf("find_tcp_header overhead: %.1f cycles (%.1f ns)\n",
	       r2.cycles_per_op - r1.cycles_per_op,
	       r2.ns_per_op - r1.ns_per_op);
	printf("Stored offset overhead:   %.1f cycles (%.1f ns)\n",
	       r3.cycles_per_op - r1.cycles_per_op,
	       r3.ns_per_op - r1.ns_per_op);
	printf("Savings from optimization: %.1f cycles (%.1f%%)\n",
	       r2.cycles_per_op - r3.cycles_per_op,
	       100.0 * (r2.cycles_per_op - r3.cycles_per_op) / r2.cycles_per_op);

	return 0;
}
