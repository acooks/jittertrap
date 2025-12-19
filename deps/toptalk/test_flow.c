/*
 * test_flow.c - Unit tests for flow utilities
 *
 * Tests the flow module which provides:
 * - flow_cmp() - Flow comparison
 * - flow_reverse() - Flow direction reversal
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include <net/ethernet.h>

#include "flow.h"

/* Test helper: create an IPv4 flow structure */
static struct flow make_flow_ipv4(const char *src_ip, uint16_t sport,
                                  const char *dst_ip, uint16_t dport,
                                  uint16_t proto)
{
	struct flow f = {0};
	f.ethertype = ETHERTYPE_IP;
	f.proto = proto;
	inet_pton(AF_INET, src_ip, &f.src_ip);
	inet_pton(AF_INET, dst_ip, &f.dst_ip);
	f.sport = sport;
	f.dport = dport;
	return f;
}

/* Test helper: create an IPv6 flow structure */
static struct flow make_flow_ipv6(const char *src_ip, uint16_t sport,
                                  const char *dst_ip, uint16_t dport,
                                  uint16_t proto)
{
	struct flow f = {0};
	f.ethertype = ETHERTYPE_IPV6;
	f.proto = proto;
	inet_pton(AF_INET6, src_ip, &f.src_ip6);
	inet_pton(AF_INET6, dst_ip, &f.dst_ip6);
	f.sport = sport;
	f.dport = dport;
	return f;
}

/*
 * Test 1: IPv4 flow reversal
 *
 * Verify that flow_reverse swaps src/dst addresses and ports for IPv4.
 */
static int test_reverse_ipv4(void)
{
	printf("Test 1: IPv4 flow reversal... ");

	struct flow orig = make_flow_ipv4("10.0.0.1", 1234, "10.0.0.2", 80, IPPROTO_TCP);
	struct flow rev = flow_reverse(&orig);

	/* Check addresses swapped */
	char src_str[INET_ADDRSTRLEN], dst_str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &rev.src_ip, src_str, sizeof(src_str));
	inet_ntop(AF_INET, &rev.dst_ip, dst_str, sizeof(dst_str));

	if (strcmp(src_str, "10.0.0.2") != 0) {
		printf("FAIL (src=%s, expected 10.0.0.2)\n", src_str);
		return 1;
	}
	if (strcmp(dst_str, "10.0.0.1") != 0) {
		printf("FAIL (dst=%s, expected 10.0.0.1)\n", dst_str);
		return 1;
	}

	/* Check ports swapped */
	if (rev.sport != 80) {
		printf("FAIL (sport=%u, expected 80)\n", rev.sport);
		return 1;
	}
	if (rev.dport != 1234) {
		printf("FAIL (dport=%u, expected 1234)\n", rev.dport);
		return 1;
	}

	/* Check ethertype and proto preserved */
	if (rev.ethertype != ETHERTYPE_IP) {
		printf("FAIL (ethertype changed)\n");
		return 1;
	}
	if (rev.proto != IPPROTO_TCP) {
		printf("FAIL (proto changed)\n");
		return 1;
	}

	printf("PASS\n");
	return 0;
}

/*
 * Test 2: IPv6 flow reversal
 *
 * Verify that flow_reverse swaps src/dst addresses and ports for IPv6.
 */
static int test_reverse_ipv6(void)
{
	printf("Test 2: IPv6 flow reversal... ");

	struct flow orig = make_flow_ipv6("2001:db8::1", 5000, "2001:db8::2", 443, IPPROTO_TCP);
	struct flow rev = flow_reverse(&orig);

	/* Check addresses swapped */
	char src_str[INET6_ADDRSTRLEN], dst_str[INET6_ADDRSTRLEN];
	inet_ntop(AF_INET6, &rev.src_ip6, src_str, sizeof(src_str));
	inet_ntop(AF_INET6, &rev.dst_ip6, dst_str, sizeof(dst_str));

	if (strcmp(src_str, "2001:db8::2") != 0) {
		printf("FAIL (src=%s, expected 2001:db8::2)\n", src_str);
		return 1;
	}
	if (strcmp(dst_str, "2001:db8::1") != 0) {
		printf("FAIL (dst=%s, expected 2001:db8::1)\n", dst_str);
		return 1;
	}

	/* Check ports swapped */
	if (rev.sport != 443) {
		printf("FAIL (sport=%u, expected 443)\n", rev.sport);
		return 1;
	}
	if (rev.dport != 5000) {
		printf("FAIL (dport=%u, expected 5000)\n", rev.dport);
		return 1;
	}

	/* Check ethertype preserved */
	if (rev.ethertype != ETHERTYPE_IPV6) {
		printf("FAIL (ethertype changed)\n");
		return 1;
	}

	printf("PASS\n");
	return 0;
}

/*
 * Test 3: Double reversal returns original
 *
 * Verify that reversing a flow twice returns the original flow.
 */
static int test_double_reverse(void)
{
	printf("Test 3: Double reversal returns original... ");

	struct flow orig = make_flow_ipv4("192.168.1.100", 54321, "8.8.8.8", 53, IPPROTO_UDP);
	orig.tclass = 0x28;  /* Set DSCP for extra verification */

	struct flow rev = flow_reverse(&orig);
	struct flow restored = flow_reverse(&rev);

	if (flow_cmp(&orig, &restored) != 0) {
		printf("FAIL (double reverse != original)\n");
		return 1;
	}

	printf("PASS\n");
	return 0;
}

/*
 * Test 4: Original flow unchanged
 *
 * Verify that flow_reverse doesn't modify the original flow.
 */
static int test_original_unchanged(void)
{
	printf("Test 4: Original flow unchanged... ");

	struct flow orig = make_flow_ipv4("10.1.1.1", 9999, "10.2.2.2", 8080, IPPROTO_TCP);
	struct flow copy = orig;  /* Save a copy */

	/* Perform reversal */
	struct flow rev = flow_reverse(&orig);
	(void)rev;  /* Suppress unused warning */

	/* Check original unchanged */
	if (flow_cmp(&orig, &copy) != 0) {
		printf("FAIL (original was modified)\n");
		return 1;
	}

	printf("PASS\n");
	return 0;
}

/*
 * Test 5: flow_cmp symmetry with reverse
 *
 * A and B are different, but A and reverse(reverse(A)) are equal.
 */
static int test_cmp_with_reverse(void)
{
	printf("Test 5: flow_cmp symmetry with reverse... ");

	struct flow a = make_flow_ipv4("1.2.3.4", 100, "5.6.7.8", 200, IPPROTO_TCP);
	struct flow b = flow_reverse(&a);

	/* A and B should be different */
	if (flow_cmp(&a, &b) == 0) {
		printf("FAIL (a == reverse(a))\n");
		return 1;
	}

	/* A and reverse(B) should be equal */
	struct flow b_rev = flow_reverse(&b);
	if (flow_cmp(&a, &b_rev) != 0) {
		printf("FAIL (a != reverse(reverse(a)))\n");
		return 1;
	}

	printf("PASS\n");
	return 0;
}

int main(void)
{
	int failures = 0;

	printf("\n=== Flow Utility Unit Tests ===\n\n");

	failures += test_reverse_ipv4();
	failures += test_reverse_ipv6();
	failures += test_double_reverse();
	failures += test_original_unchanged();
	failures += test_cmp_with_reverse();

	printf("\n=== Results: %d test(s) failed ===\n", failures);

	return failures;
}
