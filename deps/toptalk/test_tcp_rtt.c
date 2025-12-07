/*
 * test_tcp_rtt.c - Unit tests for TCP RTT tracking
 *
 * This test verifies the RTT calculation logic by simulating TCP packet
 * sequences with controlled timing. It doesn't require network access or
 * root privileges.
 *
 * How it works:
 * =============
 * TCP RTT measurement works by tracking the time between sending data and
 * receiving the corresponding ACK:
 *
 *   Client (10.0.0.1:1234) ----[DATA seq=1000, len=100]----> Server (10.0.0.2:80)
 *                              timestamp T1
 *
 *   Client <----[ACK ack=1100]---- Server
 *                timestamp T2
 *
 *   RTT = T2 - T1
 *
 * The test simulates this by calling tcp_rtt_process_packet() directly with
 * synthetic flow/packet data and controlled timestamps.
 *
 * Test cases:
 * -----------
 * 1. Basic RTT: Send data, receive ACK, verify RTT matches time delta
 * 2. EWMA smoothing: Multiple samples converge toward true RTT
 * 3. Bidirectional: Both directions of a connection track RTT independently
 * 4. Sequence wraparound: Handles uint32 overflow correctly
 * 5. Multiple outstanding: Tracks multiple unacked segments
 * 6. Cumulative ACK: Single ACK covering multiple segments
 * 7. No data packets: Pure ACKs don't create RTT samples
 *
 * Run with: ./test_tcp_rtt
 * Exit code 0 = all tests passed
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>

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

/* Test helper: check RTT is within tolerance (for future use) */
__attribute__((unused))
static int rtt_within(int64_t actual, int64_t expected, int64_t tolerance)
{
	int64_t diff = actual - expected;
	if (diff < 0) diff = -diff;
	return diff <= tolerance;
}

/*
 * Test 1: Basic RTT measurement
 *
 * Simulate: Client sends 100 bytes, server ACKs after 50ms
 * Expected: RTT = 50000 us
 */
static int test_basic_rtt(void)
{
	printf("Test 1: Basic RTT measurement... ");

	tcp_rtt_init();

	struct flow client_to_server = make_flow("10.0.0.1", 1234, "10.0.0.2", 80);
	struct flow server_to_client = make_flow("10.0.0.2", 80, "10.0.0.1", 1234);

	/* T=0ms: Client sends data (seq=1000, len=100) */
	tcp_rtt_process_packet(&client_to_server,
	                       1000,      /* seq */
	                       0,         /* ack (not relevant for data) */
	                       TH_ACK,    /* flags */
	                       100,       /* payload_len */
	                       usec_to_tv(0));

	/* T=50ms: Server sends ACK (ack=1100) */
	tcp_rtt_process_packet(&server_to_client,
	                       5000,      /* seq (server's seq, irrelevant) */
	                       1100,      /* ack = client's seq + len */
	                       TH_ACK,    /* flags */
	                       0,         /* no payload, just ACK */
	                       usec_to_tv(50000));

	/* Check RTT */
	int64_t rtt = tcp_rtt_get_ewma(&client_to_server);

	tcp_rtt_cleanup();

	if (rtt != 50000) {
		printf("FAIL (expected 50000us, got %ldus)\n", rtt);
		return 1;
	}

	printf("PASS (RTT=%ldus)\n", rtt);
	return 0;
}

/*
 * Test 2: EWMA smoothing
 *
 * Send multiple packets with varying RTT, verify EWMA converges
 * EWMA formula: rtt = rtt - rtt/8 + sample/8
 */
static int test_ewma_smoothing(void)
{
	printf("Test 2: EWMA smoothing... ");

	tcp_rtt_init();

	struct flow c2s = make_flow("10.0.0.1", 1234, "10.0.0.2", 80);
	struct flow s2c = make_flow("10.0.0.2", 80, "10.0.0.1", 1234);

	int64_t base_time = 0;
	uint32_t seq = 1000;

	/* First sample: 100ms RTT - becomes initial EWMA */
	tcp_rtt_process_packet(&c2s, seq, 0, TH_ACK, 100, usec_to_tv(base_time));
	base_time += 100000; /* 100ms later */
	tcp_rtt_process_packet(&s2c, 5000, seq + 100, TH_ACK, 0, usec_to_tv(base_time));
	seq += 100;

	int64_t rtt1 = tcp_rtt_get_ewma(&c2s);
	if (rtt1 != 100000) {
		printf("FAIL (first sample: expected 100000us, got %ldus)\n", rtt1);
		tcp_rtt_cleanup();
		return 1;
	}

	/* Second sample: 20ms RTT
	 * EWMA = 100000 - 100000/8 + 20000/8 = 100000 - 12500 + 2500 = 90000 */
	base_time += 1000; /* small gap */
	tcp_rtt_process_packet(&c2s, seq, 0, TH_ACK, 100, usec_to_tv(base_time));
	base_time += 20000; /* 20ms later */
	tcp_rtt_process_packet(&s2c, 5000, seq + 100, TH_ACK, 0, usec_to_tv(base_time));
	seq += 100;

	int64_t rtt2 = tcp_rtt_get_ewma(&c2s);
	/* EWMA = 100000 - 12500 + 2500 = 90000 */
	if (rtt2 != 90000) {
		printf("FAIL (second EWMA: expected 90000us, got %ldus)\n", rtt2);
		tcp_rtt_cleanup();
		return 1;
	}

	tcp_rtt_cleanup();
	printf("PASS (EWMA converges: %ld -> %ld)\n", rtt1, rtt2);
	return 0;
}

/*
 * Test 3: Bidirectional RTT tracking
 *
 * Both sides send data, each direction has independent RTT
 */
static int test_bidirectional(void)
{
	printf("Test 3: Bidirectional RTT... ");

	tcp_rtt_init();

	struct flow c2s = make_flow("10.0.0.1", 1234, "10.0.0.2", 80);
	struct flow s2c = make_flow("10.0.0.2", 80, "10.0.0.1", 1234);

	/* Client -> Server: 30ms RTT */
	tcp_rtt_process_packet(&c2s, 1000, 0, TH_ACK, 100, usec_to_tv(0));
	tcp_rtt_process_packet(&s2c, 5000, 1100, TH_ACK, 0, usec_to_tv(30000));

	/* Server -> Client: 40ms RTT */
	tcp_rtt_process_packet(&s2c, 5000, 1100, TH_ACK, 200, usec_to_tv(35000));
	tcp_rtt_process_packet(&c2s, 1100, 5200, TH_ACK, 0, usec_to_tv(75000));

	/* Both flows should return the same RTT for their respective directions */
	int64_t rtt_c2s = tcp_rtt_get_ewma(&c2s);
	int64_t rtt_s2c = tcp_rtt_get_ewma(&s2c);

	tcp_rtt_cleanup();

	/* c2s flow should show client->server RTT (30ms) */
	/* s2c flow should show server->client RTT (40ms) */
	if (rtt_c2s != 30000) {
		printf("FAIL (c2s: expected 30000us, got %ldus)\n", rtt_c2s);
		return 1;
	}
	if (rtt_s2c != 40000) {
		printf("FAIL (s2c: expected 40000us, got %ldus)\n", rtt_s2c);
		return 1;
	}

	printf("PASS (c2s=%ldus, s2c=%ldus)\n", rtt_c2s, rtt_s2c);
	return 0;
}

/*
 * Test 4: Sequence number wraparound
 *
 * TCP sequence numbers are 32-bit and wrap around
 */
static int test_seq_wraparound(void)
{
	printf("Test 4: Sequence wraparound... ");

	tcp_rtt_init();

	struct flow c2s = make_flow("10.0.0.1", 1234, "10.0.0.2", 80);
	struct flow s2c = make_flow("10.0.0.2", 80, "10.0.0.1", 1234);

	/* Send data near wraparound point */
	uint32_t seq = 0xFFFFFFF0; /* 16 bytes before wrap */
	tcp_rtt_process_packet(&c2s, seq, 0, TH_ACK, 100, usec_to_tv(0));

	/* ACK wraps around: 0xFFFFFFF0 + 100 = 0x54 (wrapped) */
	uint32_t expected_ack = seq + 100; /* Will wrap to 0x54 */
	tcp_rtt_process_packet(&s2c, 5000, expected_ack, TH_ACK, 0, usec_to_tv(25000));

	int64_t rtt = tcp_rtt_get_ewma(&c2s);

	tcp_rtt_cleanup();

	if (rtt != 25000) {
		printf("FAIL (expected 25000us, got %ldus)\n", rtt);
		return 1;
	}

	printf("PASS (RTT=%ldus with seq wrap 0x%x -> 0x%x)\n",
	       rtt, seq, expected_ack);
	return 0;
}

/*
 * Test 5: Multiple outstanding segments
 *
 * Multiple data packets sent before ACKs arrive
 */
static int test_multiple_outstanding(void)
{
	printf("Test 5: Multiple outstanding segments... ");

	tcp_rtt_init();

	struct flow c2s = make_flow("10.0.0.1", 1234, "10.0.0.2", 80);
	struct flow s2c = make_flow("10.0.0.2", 80, "10.0.0.1", 1234);

	/* Send 3 segments at T=0, T=5ms, T=10ms */
	tcp_rtt_process_packet(&c2s, 1000, 0, TH_ACK, 100, usec_to_tv(0));
	tcp_rtt_process_packet(&c2s, 1100, 0, TH_ACK, 100, usec_to_tv(5000));
	tcp_rtt_process_packet(&c2s, 1200, 0, TH_ACK, 100, usec_to_tv(10000));

	/* ACK first segment at T=50ms (RTT = 50ms) */
	tcp_rtt_process_packet(&s2c, 5000, 1100, TH_ACK, 0, usec_to_tv(50000));

	int64_t rtt1 = tcp_rtt_get_ewma(&c2s);
	if (rtt1 != 50000) {
		printf("FAIL (first ACK: expected 50000us, got %ldus)\n", rtt1);
		tcp_rtt_cleanup();
		return 1;
	}

	/* ACK second segment at T=52ms (RTT = 52-5 = 47ms) */
	tcp_rtt_process_packet(&s2c, 5000, 1200, TH_ACK, 0, usec_to_tv(52000));

	int64_t rtt2 = tcp_rtt_get_ewma(&c2s);
	/* EWMA = 50000 - 50000/8 + 47000/8 = 50000 - 6250 + 5875 = 49625 */
	if (rtt2 != 49625) {
		printf("FAIL (second ACK: expected 49625us, got %ldus)\n", rtt2);
		tcp_rtt_cleanup();
		return 1;
	}

	tcp_rtt_cleanup();
	printf("PASS (RTT after 2 ACKs: %ldus)\n", rtt2);
	return 0;
}

/*
 * Test 6: Cumulative ACK
 *
 * Single ACK acknowledges multiple segments.
 * The RTT is calculated from the LAST segment covered by the ACK
 * (most recently sent), not the first. This gives the most accurate
 * RTT measurement because the ACK timing is closest to that segment.
 */
static int test_cumulative_ack(void)
{
	printf("Test 6: Cumulative ACK... ");

	tcp_rtt_init();

	struct flow c2s = make_flow("10.0.0.1", 1234, "10.0.0.2", 80);
	struct flow s2c = make_flow("10.0.0.2", 80, "10.0.0.1", 1234);

	/* Send 3 segments at T=0, T=1ms, T=2ms */
	tcp_rtt_process_packet(&c2s, 1000, 0, TH_ACK, 100, usec_to_tv(0));
	tcp_rtt_process_packet(&c2s, 1100, 0, TH_ACK, 100, usec_to_tv(1000));
	tcp_rtt_process_packet(&c2s, 1200, 0, TH_ACK, 100, usec_to_tv(2000));

	/* Single ACK covers all 3 at T=60ms
	 * RTT is calculated from the LAST matched segment (T=2ms)
	 * because that gives the most accurate RTT estimate.
	 */
	tcp_rtt_process_packet(&s2c, 5000, 1300, TH_ACK, 0, usec_to_tv(60000));

	int64_t rtt = tcp_rtt_get_ewma(&c2s);

	tcp_rtt_cleanup();

	/* Matches seq 1200 sent at T=2ms, so RTT = 60ms - 2ms = 58ms */
	if (rtt != 58000) {
		printf("FAIL (expected 58000us, got %ldus)\n", rtt);
		return 1;
	}

	printf("PASS (cumulative ACK RTT=%ldus from last segment)\n", rtt);
	return 0;
}

/*
 * Test 7: No RTT for pure ACKs
 *
 * Packets without payload shouldn't create RTT entries
 */
static int test_no_data_no_rtt(void)
{
	printf("Test 7: No RTT without data... ");

	tcp_rtt_init();

	struct flow c2s = make_flow("10.0.0.1", 1234, "10.0.0.2", 80);
	struct flow s2c = make_flow("10.0.0.2", 80, "10.0.0.1", 1234);

	/* Send pure ACK (no payload) */
	tcp_rtt_process_packet(&c2s, 1000, 5000, TH_ACK, 0, usec_to_tv(0));

	/* Server ACKs (but there was no data to ACK) */
	tcp_rtt_process_packet(&s2c, 5000, 1000, TH_ACK, 0, usec_to_tv(50000));

	int64_t rtt = tcp_rtt_get_ewma(&c2s);

	tcp_rtt_cleanup();

	if (rtt != -1) {
		printf("FAIL (expected -1, got %ldus)\n", rtt);
		return 1;
	}

	printf("PASS (no RTT for pure ACKs)\n");
	return 0;
}

/*
 * Test 8: Non-TCP flows return -1
 */
static int test_non_tcp(void)
{
	printf("Test 8: Non-TCP returns -1... ");

	tcp_rtt_init();

	struct flow udp_flow = make_flow("10.0.0.1", 1234, "10.0.0.2", 80);
	udp_flow.proto = IPPROTO_UDP;

	int64_t rtt = tcp_rtt_get_ewma(&udp_flow);

	tcp_rtt_cleanup();

	if (rtt != -1) {
		printf("FAIL (expected -1 for UDP, got %ld)\n", rtt);
		return 1;
	}

	printf("PASS\n");
	return 0;
}

/*
 * Test 9: Flow expiration
 *
 * Old flows should be cleaned up
 */
static int test_expiration(void)
{
	printf("Test 9: Flow expiration... ");

	tcp_rtt_init();

	struct flow c2s = make_flow("10.0.0.1", 1234, "10.0.0.2", 80);
	struct flow s2c = make_flow("10.0.0.2", 80, "10.0.0.1", 1234);

	/* Create RTT entry at T=0 */
	tcp_rtt_process_packet(&c2s, 1000, 0, TH_ACK, 100, usec_to_tv(0));
	tcp_rtt_process_packet(&s2c, 5000, 1100, TH_ACK, 0, usec_to_tv(30000));

	int64_t rtt1 = tcp_rtt_get_ewma(&c2s);
	if (rtt1 != 30000) {
		printf("FAIL (initial RTT: expected 30000, got %ld)\n", rtt1);
		tcp_rtt_cleanup();
		return 1;
	}

	/* Expire entries older than T=1s with 500ms window */
	struct timeval deadline = usec_to_tv(1000000);  /* T=1s */
	struct timeval window = usec_to_tv(500000);     /* 500ms window */
	tcp_rtt_expire_old(deadline, window);

	/* Flow should be expired (last activity was at T=30ms) */
	int64_t rtt2 = tcp_rtt_get_ewma(&c2s);

	tcp_rtt_cleanup();

	if (rtt2 != -1) {
		printf("FAIL (after expiry: expected -1, got %ld)\n", rtt2);
		return 1;
	}

	printf("PASS (flow expired correctly)\n");
	return 0;
}

int main(void)
{
	int failures = 0;

	printf("\n=== TCP RTT Tracking Unit Tests ===\n\n");

	failures += test_basic_rtt();
	failures += test_ewma_smoothing();
	failures += test_bidirectional();
	failures += test_seq_wraparound();
	failures += test_multiple_outstanding();
	failures += test_cumulative_ack();
	failures += test_no_data_no_rtt();
	failures += test_non_tcp();
	failures += test_expiration();

	printf("\n=== Results: %d test(s) failed ===\n\n",failures);

	return failures;
}
