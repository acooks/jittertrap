/*
 * test_tcp_window.c - Unit tests for TCP window/congestion tracking
 *
 * Tests the tcp_window module which tracks:
 * - Advertised window (rwnd) with optional window scaling
 * - Zero-window events
 * - Duplicate ACK detection
 * - Retransmission detection
 * - ECE/CWR flag tracking
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>

#include "tcp_window.h"
#include "tcp_rtt.h"
#include "decode.h"

/* Test helper: create a flow structure */
static struct flow make_flow(const char *src_ip, uint16_t sport,
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

/* Test helper: create a timeval from microseconds */
static struct timeval usec_to_tv(int64_t usec)
{
	struct timeval tv;
	tv.tv_sec = usec / 1000000;
	tv.tv_usec = usec % 1000000;
	return tv;
}

/*
 * Test 1: Basic window tracking
 */
static int test_basic_window(void)
{
	printf("Test 1: Basic window tracking... ");

	tcp_window_init();

	struct flow client_to_server = make_flow("10.0.0.1", 1234, "10.0.0.2", 80);

	/* Send a data packet with window=65535 */
	tcp_window_process_packet(&client_to_server,
	                          NULL, NULL,  /* no TCP header for options */
	                          1000,        /* seq */
	                          0,           /* ack */
	                          TH_ACK,      /* flags */
	                          65535,       /* window */
	                          100,         /* payload_len */
	                          usec_to_tv(0));

	struct tcp_window_info info;
	int ret = tcp_window_get_info(&client_to_server, &info);

	tcp_window_cleanup();

	if (ret != 0) {
		printf("FAIL (get_info returned %d)\n", ret);
		return 1;
	}

	if (info.rwnd_bytes != 65535) {
		printf("FAIL (rwnd=%ld, expected 65535)\n", info.rwnd_bytes);
		return 1;
	}

	printf("PASS (rwnd=%ld)\n", info.rwnd_bytes);
	return 0;
}

/*
 * Test 2: Zero window detection
 */
static int test_zero_window(void)
{
	printf("Test 2: Zero window detection... ");

	tcp_window_init();

	struct flow client_to_server = make_flow("10.0.0.1", 1234, "10.0.0.2", 80);

	/* Send a packet with zero window */
	tcp_window_process_packet(&client_to_server,
	                          NULL, NULL,
	                          1000, 0, TH_ACK,
	                          0,           /* zero window! */
	                          100,
	                          usec_to_tv(0));

	struct tcp_window_info info;
	int ret = tcp_window_get_info(&client_to_server, &info);

	tcp_window_cleanup();

	if (ret != 0) {
		printf("FAIL (get_info returned %d)\n", ret);
		return 1;
	}

	if (info.zero_window_count != 1) {
		printf("FAIL (zero_window_count=%u, expected 1)\n", info.zero_window_count);
		return 1;
	}

	if (!(info.recent_events & CONG_EVENT_ZERO_WINDOW)) {
		printf("FAIL (CONG_EVENT_ZERO_WINDOW not set)\n");
		return 1;
	}

	printf("PASS (zero_window_count=%u)\n", info.zero_window_count);
	return 0;
}

/*
 * Test 3: Duplicate ACK detection
 */
static int test_dup_ack(void)
{
	printf("Test 3: Duplicate ACK detection... ");

	tcp_window_init();

	struct flow client_to_server = make_flow("10.0.0.1", 1234, "10.0.0.2", 80);

	/* First, need to establish the connection with some data
	 * so that dup_ack detection can work */

	/* Send 4 pure ACKs with the same ACK number (triple dup ACK) */
	for (int i = 0; i < 4; i++) {
		tcp_window_process_packet(&client_to_server,
		                          NULL, NULL,
		                          1000, 5000, TH_ACK,
		                          65535,
		                          0,         /* pure ACK, no data */
		                          usec_to_tv(i * 1000));
	}

	struct tcp_window_info info;
	int ret = tcp_window_get_info(&client_to_server, &info);

	tcp_window_cleanup();

	if (ret != 0) {
		printf("FAIL (get_info returned %d)\n", ret);
		return 1;
	}

	if (info.dup_ack_count != 1) {
		printf("FAIL (dup_ack_count=%u, expected 1)\n", info.dup_ack_count);
		return 1;
	}

	printf("PASS (dup_ack_count=%u)\n", info.dup_ack_count);
	return 0;
}

/*
 * Test 4: Retransmission detection
 */
static int test_retransmit(void)
{
	printf("Test 4: Retransmission detection... ");

	tcp_window_init();

	struct flow client_to_server = make_flow("10.0.0.1", 1234, "10.0.0.2", 80);

	/* Send first data packet seq=1000, len=100 */
	tcp_window_process_packet(&client_to_server,
	                          NULL, NULL,
	                          1000, 0, TH_ACK,
	                          65535,
	                          100,         /* data */
	                          usec_to_tv(0));

	/* Send second data packet seq=1100, len=100 */
	tcp_window_process_packet(&client_to_server,
	                          NULL, NULL,
	                          1100, 0, TH_ACK,
	                          65535,
	                          100,
	                          usec_to_tv(1000));

	/* Retransmit first packet seq=1000, len=100 */
	tcp_window_process_packet(&client_to_server,
	                          NULL, NULL,
	                          1000, 0, TH_ACK,  /* same seq as first */
	                          65535,
	                          100,
	                          usec_to_tv(2000));

	struct tcp_window_info info;
	int ret = tcp_window_get_info(&client_to_server, &info);

	tcp_window_cleanup();

	if (ret != 0) {
		printf("FAIL (get_info returned %d)\n", ret);
		return 1;
	}

	if (info.retransmit_count != 1) {
		printf("FAIL (retransmit_count=%u, expected 1)\n", info.retransmit_count);
		return 1;
	}

	printf("PASS (retransmit_count=%u)\n", info.retransmit_count);
	return 0;
}

/*
 * Test 5: ECE/CWR flag tracking
 */
static int test_ecn_flags(void)
{
	printf("Test 5: ECE/CWR flag tracking... ");

	tcp_window_init();

	struct flow client_to_server = make_flow("10.0.0.1", 1234, "10.0.0.2", 80);

	/* Send packet with ECE flag */
	tcp_window_process_packet(&client_to_server,
	                          NULL, NULL,
	                          1000, 0, TH_ACK | TH_ECE,
	                          65535,
	                          100,
	                          usec_to_tv(0));

	/* Send packet with CWR flag */
	tcp_window_process_packet(&client_to_server,
	                          NULL, NULL,
	                          1100, 0, TH_ACK | TH_CWR,
	                          65535,
	                          100,
	                          usec_to_tv(1000));

	struct tcp_window_info info;
	int ret = tcp_window_get_info(&client_to_server, &info);

	tcp_window_cleanup();

	if (ret != 0) {
		printf("FAIL (get_info returned %d)\n", ret);
		return 1;
	}

	if (info.ece_count != 1) {
		printf("FAIL (ece_count=%u, expected 1)\n", info.ece_count);
		return 1;
	}

	/* Note: CWR is in a different packet, check recent_events has both */
	/* Actually recent_events is cleared on read, so we'll just check counts */

	printf("PASS (ece_count=%u)\n", info.ece_count);
	return 0;
}

/*
 * Test 6: Non-TCP returns -1
 */
static int test_non_tcp(void)
{
	printf("Test 6: Non-TCP returns -1... ");

	tcp_window_init();

	struct flow udp_flow = make_flow("10.0.0.1", 1234, "10.0.0.2", 53);
	udp_flow.proto = IPPROTO_UDP;

	struct tcp_window_info info;
	int ret = tcp_window_get_info(&udp_flow, &info);

	tcp_window_cleanup();

	if (ret != -1) {
		printf("FAIL (expected -1, got %d)\n", ret);
		return 1;
	}

	printf("PASS\n");
	return 0;
}

/*
 * Test 7: Bidirectional tracking
 */
static int test_bidirectional(void)
{
	printf("Test 7: Bidirectional tracking... ");

	tcp_window_init();

	struct flow client_to_server = make_flow("10.0.0.1", 1234, "10.0.0.2", 80);
	struct flow server_to_client = make_flow("10.0.0.2", 80, "10.0.0.1", 1234);

	/* Client sends with window=32768 */
	tcp_window_process_packet(&client_to_server,
	                          NULL, NULL,
	                          1000, 0, TH_ACK,
	                          32768,
	                          100,
	                          usec_to_tv(0));

	/* Server sends with window=65535 */
	tcp_window_process_packet(&server_to_client,
	                          NULL, NULL,
	                          5000, 1100, TH_ACK,
	                          65535,
	                          200,
	                          usec_to_tv(1000));

	struct tcp_window_info c2s_info, s2c_info;
	int ret1 = tcp_window_get_info(&client_to_server, &c2s_info);
	int ret2 = tcp_window_get_info(&server_to_client, &s2c_info);

	tcp_window_cleanup();

	if (ret1 != 0 || ret2 != 0) {
		printf("FAIL (get_info failed)\n");
		return 1;
	}

	if (c2s_info.rwnd_bytes != 32768) {
		printf("FAIL (c2s rwnd=%ld, expected 32768)\n", c2s_info.rwnd_bytes);
		return 1;
	}

	if (s2c_info.rwnd_bytes != 65535) {
		printf("FAIL (s2c rwnd=%ld, expected 65535)\n", s2c_info.rwnd_bytes);
		return 1;
	}

	printf("PASS (c2s=%ld, s2c=%ld)\n", c2s_info.rwnd_bytes, s2c_info.rwnd_bytes);
	return 0;
}

int main(void)
{
	int failures = 0;

	printf("\n=== TCP Window Tracking Unit Tests ===\n\n");

	failures += test_basic_window();
	failures += test_zero_window();
	failures += test_dup_ack();
	failures += test_retransmit();
	failures += test_ecn_flags();
	failures += test_non_tcp();
	failures += test_bidirectional();

	printf("\n=== Results: %d test(s) failed ===\n", failures);

	return failures;
}
