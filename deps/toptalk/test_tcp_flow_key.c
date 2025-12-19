/*
 * test_tcp_flow_key.c - Unit tests for canonical flow key generation
 *
 * Tests the make_canonical_key() function which provides:
 * - Bidirectional flow key generation (lower IP/port first)
 * - is_forward flag indicating packet direction relative to key
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include <net/ethernet.h>

#include "tcp_flow_key.h"

/* Test helper: create an IPv4 flow structure */
static struct flow make_flow_ipv4(const char *src_ip, uint16_t sport,
                                  const char *dst_ip, uint16_t dport)
{
	struct flow f = {0};
	f.ethertype = ETHERTYPE_IP;
	f.proto = IPPROTO_TCP;
	inet_pton(AF_INET, src_ip, &f.src_ip);
	inet_pton(AF_INET, dst_ip, &f.dst_ip);
	f.sport = sport;
	f.dport = dport;
	return f;
}

/* Test helper: create an IPv6 flow structure */
static struct flow make_flow_ipv6(const char *src_ip, uint16_t sport,
                                  const char *dst_ip, uint16_t dport)
{
	struct flow f = {0};
	f.ethertype = ETHERTYPE_IPV6;
	f.proto = IPPROTO_TCP;
	inet_pton(AF_INET6, src_ip, &f.src_ip6);
	inet_pton(AF_INET6, dst_ip, &f.dst_ip6);
	f.sport = sport;
	f.dport = dport;
	return f;
}

/*
 * Test 1: IPv4 canonical ordering - lower IP first
 *
 * When src < dst numerically, src becomes ip_lo and is_forward = 1.
 */
static int test_ipv4_lower_src(void)
{
	printf("Test 1: IPv4 canonical ordering (src < dst)... ");

	struct flow f = make_flow_ipv4("10.0.0.1", 1234, "10.0.0.2", 80);
	struct tcp_flow_key key;
	int is_forward;

	make_canonical_key(&key, &f, &is_forward);

	/* 10.0.0.1 < 10.0.0.2, so src should be lo */
	char lo_str[INET_ADDRSTRLEN], hi_str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &key.ip_lo, lo_str, sizeof(lo_str));
	inet_ntop(AF_INET, &key.ip_hi, hi_str, sizeof(hi_str));

	if (strcmp(lo_str, "10.0.0.1") != 0) {
		printf("FAIL (ip_lo=%s, expected 10.0.0.1)\n", lo_str);
		return 1;
	}
	if (strcmp(hi_str, "10.0.0.2") != 0) {
		printf("FAIL (ip_hi=%s, expected 10.0.0.2)\n", hi_str);
		return 1;
	}
	if (key.port_lo != 1234 || key.port_hi != 80) {
		printf("FAIL (ports: %u->%u, expected 1234->80)\n",
		       key.port_lo, key.port_hi);
		return 1;
	}
	if (!is_forward) {
		printf("FAIL (is_forward=0, expected 1)\n");
		return 1;
	}

	printf("PASS\n");
	return 0;
}

/*
 * Test 2: IPv4 canonical ordering - higher IP becomes lo when swapped
 *
 * When src > dst numerically, dst becomes ip_lo and is_forward = 0.
 */
static int test_ipv4_higher_src(void)
{
	printf("Test 2: IPv4 canonical ordering (src > dst)... ");

	struct flow f = make_flow_ipv4("10.0.0.2", 80, "10.0.0.1", 1234);
	struct tcp_flow_key key;
	int is_forward;

	make_canonical_key(&key, &f, &is_forward);

	/* 10.0.0.2 > 10.0.0.1, so dst should be lo */
	char lo_str[INET_ADDRSTRLEN], hi_str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &key.ip_lo, lo_str, sizeof(lo_str));
	inet_ntop(AF_INET, &key.ip_hi, hi_str, sizeof(hi_str));

	if (strcmp(lo_str, "10.0.0.1") != 0) {
		printf("FAIL (ip_lo=%s, expected 10.0.0.1)\n", lo_str);
		return 1;
	}
	if (strcmp(hi_str, "10.0.0.2") != 0) {
		printf("FAIL (ip_hi=%s, expected 10.0.0.2)\n", hi_str);
		return 1;
	}
	if (key.port_lo != 1234 || key.port_hi != 80) {
		printf("FAIL (ports: %u->%u, expected 1234->80)\n",
		       key.port_lo, key.port_hi);
		return 1;
	}
	if (is_forward) {
		printf("FAIL (is_forward=1, expected 0)\n");
		return 1;
	}

	printf("PASS\n");
	return 0;
}

/*
 * Test 3: Same IP, port ordering decides
 *
 * When IPs are equal, lower port determines ordering.
 */
static int test_same_ip_port_decides(void)
{
	printf("Test 3: Same IP, port ordering decides... ");

	/* Same IP, sport > dport */
	struct flow f = make_flow_ipv4("10.0.0.1", 5000, "10.0.0.1", 80);
	struct tcp_flow_key key;
	int is_forward;

	make_canonical_key(&key, &f, &is_forward);

	/* sport (5000) > dport (80), so ports should be swapped */
	if (key.port_lo != 80 || key.port_hi != 5000) {
		printf("FAIL (ports: %u->%u, expected 80->5000)\n",
		       key.port_lo, key.port_hi);
		return 1;
	}
	if (is_forward) {
		printf("FAIL (is_forward=1, expected 0)\n");
		return 1;
	}

	/* Now with sport < dport */
	struct flow f2 = make_flow_ipv4("10.0.0.1", 80, "10.0.0.1", 5000);
	make_canonical_key(&key, &f2, &is_forward);

	if (key.port_lo != 80 || key.port_hi != 5000) {
		printf("FAIL (ports: %u->%u, expected 80->5000)\n",
		       key.port_lo, key.port_hi);
		return 1;
	}
	if (!is_forward) {
		printf("FAIL (is_forward=0, expected 1)\n");
		return 1;
	}

	printf("PASS\n");
	return 0;
}

/*
 * Test 4: IPv6 canonical ordering
 *
 * Same logic as IPv4 but with 128-bit addresses.
 */
static int test_ipv6_ordering(void)
{
	printf("Test 4: IPv6 canonical ordering... ");

	struct flow f = make_flow_ipv6("2001:db8::2", 443, "2001:db8::1", 54321);
	struct tcp_flow_key key;
	int is_forward;

	make_canonical_key(&key, &f, &is_forward);

	/* 2001:db8::2 > 2001:db8::1, so dst should be lo */
	char lo_str[INET6_ADDRSTRLEN], hi_str[INET6_ADDRSTRLEN];
	inet_ntop(AF_INET6, &key.ip6_lo, lo_str, sizeof(lo_str));
	inet_ntop(AF_INET6, &key.ip6_hi, hi_str, sizeof(hi_str));

	if (strcmp(lo_str, "2001:db8::1") != 0) {
		printf("FAIL (ip6_lo=%s, expected 2001:db8::1)\n", lo_str);
		return 1;
	}
	if (strcmp(hi_str, "2001:db8::2") != 0) {
		printf("FAIL (ip6_hi=%s, expected 2001:db8::2)\n", hi_str);
		return 1;
	}
	if (key.port_lo != 54321 || key.port_hi != 443) {
		printf("FAIL (ports: %u->%u, expected 54321->443)\n",
		       key.port_lo, key.port_hi);
		return 1;
	}
	if (is_forward) {
		printf("FAIL (is_forward=1, expected 0)\n");
		return 1;
	}

	printf("PASS\n");
	return 0;
}

/*
 * Test 5: Round-trip - key from A->B equals key from B->A
 *
 * This is the key property: both directions produce the same canonical key.
 */
static int test_round_trip(void)
{
	printf("Test 5: Round-trip key equality... ");

	struct flow a_to_b = make_flow_ipv4("192.168.1.100", 54321, "8.8.8.8", 443);
	struct flow b_to_a = make_flow_ipv4("8.8.8.8", 443, "192.168.1.100", 54321);

	struct tcp_flow_key key_ab, key_ba;
	int is_forward_ab, is_forward_ba;

	make_canonical_key(&key_ab, &a_to_b, &is_forward_ab);
	make_canonical_key(&key_ba, &b_to_a, &is_forward_ba);

	/* Keys should be identical */
	if (memcmp(&key_ab, &key_ba, sizeof(key_ab)) != 0) {
		printf("FAIL (keys differ)\n");
		return 1;
	}

	/* is_forward should be opposite */
	if (is_forward_ab == is_forward_ba) {
		printf("FAIL (is_forward both %d, should differ)\n", is_forward_ab);
		return 1;
	}

	printf("PASS\n");
	return 0;
}

/*
 * Test 6: Round-trip for IPv6
 */
static int test_round_trip_ipv6(void)
{
	printf("Test 6: Round-trip key equality (IPv6)... ");

	struct flow a_to_b = make_flow_ipv6("2001:db8::1", 22, "2001:db8::100", 50000);
	struct flow b_to_a = make_flow_ipv6("2001:db8::100", 50000, "2001:db8::1", 22);

	struct tcp_flow_key key_ab, key_ba;
	int is_forward_ab, is_forward_ba;

	make_canonical_key(&key_ab, &a_to_b, &is_forward_ab);
	make_canonical_key(&key_ba, &b_to_a, &is_forward_ba);

	/* Keys should be identical */
	if (memcmp(&key_ab, &key_ba, sizeof(key_ab)) != 0) {
		printf("FAIL (keys differ)\n");
		return 1;
	}

	/* is_forward should be opposite */
	if (is_forward_ab == is_forward_ba) {
		printf("FAIL (is_forward both %d, should differ)\n", is_forward_ab);
		return 1;
	}

	printf("PASS\n");
	return 0;
}

/*
 * Test 7: Ethertype preserved
 */
static int test_ethertype_preserved(void)
{
	printf("Test 7: Ethertype preserved... ");

	struct flow f4 = make_flow_ipv4("10.0.0.1", 80, "10.0.0.2", 443);
	struct flow f6 = make_flow_ipv6("2001:db8::1", 80, "2001:db8::2", 443);

	struct tcp_flow_key key4, key6;
	int is_forward;

	make_canonical_key(&key4, &f4, &is_forward);
	make_canonical_key(&key6, &f6, &is_forward);

	if (key4.ethertype != ETHERTYPE_IP) {
		printf("FAIL (IPv4 ethertype=%u, expected %u)\n",
		       key4.ethertype, ETHERTYPE_IP);
		return 1;
	}
	if (key6.ethertype != ETHERTYPE_IPV6) {
		printf("FAIL (IPv6 ethertype=%u, expected %u)\n",
		       key6.ethertype, ETHERTYPE_IPV6);
		return 1;
	}

	printf("PASS\n");
	return 0;
}

int main(void)
{
	int failures = 0;

	printf("\n=== TCP Flow Key Unit Tests ===\n\n");

	failures += test_ipv4_lower_src();
	failures += test_ipv4_higher_src();
	failures += test_same_ip_port_decides();
	failures += test_ipv6_ordering();
	failures += test_round_trip();
	failures += test_round_trip_ipv6();
	failures += test_ethertype_preserved();

	printf("\n=== Results: %d test(s) failed ===\n", failures);

	return failures;
}
