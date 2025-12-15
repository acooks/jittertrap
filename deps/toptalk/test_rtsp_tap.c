/*
 * test_rtsp_tap.c - Unit tests for RTSP passive tap
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include "rtsp_tap.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
	printf("  Testing %s... ", #name); \
	fflush(stdout); \
	tests_run++; \
	if (test_##name()) { \
		printf("PASS\n"); \
		tests_passed++; \
	} else { \
		printf("FAIL\n"); \
	} \
} while(0)

/* Sample RTSP DESCRIBE response with SDP */
static const char *describe_response =
	"RTSP/1.0 200 OK\r\n"
	"CSeq: 2\r\n"
	"Content-Type: application/sdp\r\n"
	"Content-Length: 300\r\n"
	"\r\n"
	"v=0\r\n"
	"o=- 0 0 IN IP4 192.168.1.100\r\n"
	"s=IP Camera\r\n"
	"c=IN IP4 0.0.0.0\r\n"
	"t=0 0\r\n"
	"m=video 0 RTP/AVP 96\r\n"
	"a=rtpmap:96 H264/90000\r\n"
	"a=fmtp:96 packetization-mode=1; profile-level-id=640028; sprop-parameter-sets=Z2QAKKzZQLQ9uCgB,aO48gA==\r\n"
	"a=control:track1\r\n";

/* Sample RTSP SETUP response with Transport header */
static const char *setup_response =
	"RTSP/1.0 200 OK\r\n"
	"CSeq: 3\r\n"
	"Session: 12345678;timeout=60\r\n"
	"Transport: RTP/AVP;unicast;client_port=5000-5001;server_port=6000-6001;ssrc=AABBCCDD\r\n"
	"\r\n";

/* Sample RTSP PLAY request */
static const char *play_request =
	"PLAY rtsp://192.168.1.100/stream1 RTSP/1.0\r\n"
	"CSeq: 4\r\n"
	"Session: 12345678\r\n"
	"\r\n";

/* Sample RTSP TEARDOWN request */
static const char *teardown_request =
	"TEARDOWN rtsp://192.168.1.100/stream1 RTSP/1.0\r\n"
	"CSeq: 5\r\n"
	"Session: 12345678\r\n"
	"\r\n";

/* Sample DESCRIBE request */
static const char *describe_request =
	"DESCRIBE rtsp://192.168.1.100/stream1 RTSP/1.0\r\n"
	"CSeq: 2\r\n"
	"Accept: application/sdp\r\n"
	"\r\n";

/* Create a test flow */
static void make_test_flow(struct flow *f, const char *src_ip, uint16_t sport,
                           const char *dst_ip, uint16_t dport)
{
	memset(f, 0, sizeof(*f));
	f->ethertype = 0x0800; /* IPv4 */
	f->proto = 6; /* TCP */
	inet_pton(AF_INET, src_ip, &f->src_ip);
	inet_pton(AF_INET, dst_ip, &f->dst_ip);
	f->sport = sport;
	f->dport = dport;
}

/* Test basic initialization */
static int test_init(void)
{
	struct rtsp_tap_state state;
	rtsp_tap_init(&state);

	if (state.session_count != 0)
		return 0;

	for (int i = 0; i < RTSP_MAX_SESSIONS; i++) {
		if (state.sessions[i].active)
			return 0;
	}

	return 1;
}

/* Test SDP parsing from DESCRIBE response */
static int test_sdp_parsing(void)
{
	struct rtsp_tap_state state;
	struct flow f;

	rtsp_tap_init(&state);
	make_test_flow(&f, "192.168.1.50", 50000, "192.168.1.100", 554);

	int ret = rtsp_tap_process_packet(&state, &f,
		(const uint8_t *)describe_response,
		strlen(describe_response), 1000000000ULL);

	if (!ret)
		return 0;

	if (state.session_count != 1)
		return 0;

	struct rtsp_session *s = &state.sessions[0];
	if (!s->active)
		return 0;

	if (s->state != RTSP_STATE_DESCRIBED)
		return 0;

	if (s->media_count != 1)
		return 0;

	struct rtsp_media *m = &s->media[0];
	if (!m->valid)
		return 0;

	/* Check parsed SDP values */
	if (m->payload_type != 96)
		return 0;

	if (m->codec != VIDEO_CODEC_H264)
		return 0;

	if (strcmp(m->codec_name, "H264") != 0)
		return 0;

	if (m->clock_rate != 90000)
		return 0;

	/* Check profile-level-id parsing: 640028 */
	/* 0x64 = 100 = High profile */
	/* 0x00 = constraint flags */
	/* 0x28 = 40 = Level 4.0 */
	if (m->profile_idc != 0x64)
		return 0;

	if (m->level_idc != 0x28)
		return 0;

	return 1;
}

/* Test Transport header parsing from SETUP response */
static int test_transport_parsing(void)
{
	struct rtsp_tap_state state;
	struct flow f;

	rtsp_tap_init(&state);
	make_test_flow(&f, "192.168.1.50", 50000, "192.168.1.100", 554);

	/* First send DESCRIBE to create session and media */
	rtsp_tap_process_packet(&state, &f,
		(const uint8_t *)describe_response,
		strlen(describe_response), 1000000000ULL);

	/* Then send SETUP */
	int ret = rtsp_tap_process_packet(&state, &f,
		(const uint8_t *)setup_response,
		strlen(setup_response), 2000000000ULL);

	if (!ret)
		return 0;

	struct rtsp_session *s = &state.sessions[0];
	if (s->state != RTSP_STATE_SETUP)
		return 0;

	/* Check Session ID */
	if (strncmp(s->session_id, "12345678", 8) != 0)
		return 0;

	/* Check Transport header parsing */
	struct rtsp_media *m = &s->media[0];
	if (m->client_rtp_port != 5000)
		return 0;
	if (m->client_rtcp_port != 5001)
		return 0;
	if (m->server_rtp_port != 6000)
		return 0;
	if (m->server_rtcp_port != 6001)
		return 0;

	/* Check SSRC parsing (hex: AABBCCDD = 2864434397) */
	if (m->ssrc != 0xAABBCCDD)
		return 0;

	return 1;
}

/* Test session state transitions */
static int test_state_transitions(void)
{
	struct rtsp_tap_state state;
	struct flow f;

	rtsp_tap_init(&state);
	make_test_flow(&f, "192.168.1.50", 50000, "192.168.1.100", 554);

	/* DESCRIBE -> DESCRIBED */
	rtsp_tap_process_packet(&state, &f,
		(const uint8_t *)describe_response,
		strlen(describe_response), 1000000000ULL);

	if (state.sessions[0].state != RTSP_STATE_DESCRIBED)
		return 0;

	/* SETUP -> SETUP */
	rtsp_tap_process_packet(&state, &f,
		(const uint8_t *)setup_response,
		strlen(setup_response), 2000000000ULL);

	if (state.sessions[0].state != RTSP_STATE_SETUP)
		return 0;

	/* PLAY -> PLAYING */
	rtsp_tap_process_packet(&state, &f,
		(const uint8_t *)play_request,
		strlen(play_request), 3000000000ULL);

	if (state.sessions[0].state != RTSP_STATE_PLAYING)
		return 0;

	/* TEARDOWN -> TEARDOWN */
	rtsp_tap_process_packet(&state, &f,
		(const uint8_t *)teardown_request,
		strlen(teardown_request), 4000000000ULL);

	if (state.sessions[0].state != RTSP_STATE_TEARDOWN)
		return 0;

	return 1;
}

/* Test URL extraction from DESCRIBE request */
static int test_url_extraction(void)
{
	struct rtsp_tap_state state;
	struct flow f;

	rtsp_tap_init(&state);
	make_test_flow(&f, "192.168.1.50", 50000, "192.168.1.100", 554);

	/* Send DESCRIBE request */
	rtsp_tap_process_packet(&state, &f,
		(const uint8_t *)describe_request,
		strlen(describe_request), 1000000000ULL);

	struct rtsp_session *s = &state.sessions[0];

	/* Check URL was extracted */
	if (strstr(s->url, "rtsp://192.168.1.100/stream1") == NULL)
		return 0;

	return 1;
}

/* Test RTP flow matching */
static int test_rtp_flow_matching(void)
{
	struct rtsp_tap_state state;
	struct flow rtsp_flow, rtp_flow;

	rtsp_tap_init(&state);

	/* Client at 192.168.1.50, camera at 192.168.1.100 */
	make_test_flow(&rtsp_flow, "192.168.1.50", 50000, "192.168.1.100", 554);

	/* Process DESCRIBE and SETUP */
	rtsp_tap_process_packet(&state, &rtsp_flow,
		(const uint8_t *)describe_response,
		strlen(describe_response), 1000000000ULL);
	rtsp_tap_process_packet(&state, &rtsp_flow,
		(const uint8_t *)setup_response,
		strlen(setup_response), 2000000000ULL);

	/* Create RTP flow: camera -> client */
	make_test_flow(&rtp_flow, "192.168.1.100", 6000, "192.168.1.50", 5000);
	rtp_flow.proto = 17; /* UDP */

	/* Try to find media for RTP flow by SSRC */
	const struct rtsp_media *media = rtsp_tap_find_media_for_rtp(
		&state, &rtp_flow, 0xAABBCCDD);

	if (!media)
		return 0;

	if (media->codec != VIDEO_CODEC_H264)
		return 0;

	if (media->ssrc != 0xAABBCCDD)
		return 0;

	return 1;
}

/* Test session cleanup */
static int test_cleanup(void)
{
	struct rtsp_tap_state state;
	struct flow f;

	rtsp_tap_init(&state);
	make_test_flow(&f, "192.168.1.50", 50000, "192.168.1.100", 554);

	/* Create a session */
	rtsp_tap_process_packet(&state, &f,
		(const uint8_t *)describe_response,
		strlen(describe_response), 1000000000ULL);

	if (state.session_count != 1)
		return 0;

	/* Cleanup with timeout - session should survive */
	rtsp_tap_cleanup(&state, 2000000000ULL, 60000000000ULL);
	if (state.session_count != 1)
		return 0;

	/* Cleanup after timeout - session should be removed */
	rtsp_tap_cleanup(&state, 70000000000ULL, 60000000000ULL);
	if (state.session_count != 0)
		return 0;

	return 1;
}

/* Test cleanup of TEARDOWN session */
static int test_teardown_cleanup(void)
{
	struct rtsp_tap_state state;
	struct flow f;

	rtsp_tap_init(&state);
	make_test_flow(&f, "192.168.1.50", 50000, "192.168.1.100", 554);

	/* Create session and send TEARDOWN */
	rtsp_tap_process_packet(&state, &f,
		(const uint8_t *)describe_response,
		strlen(describe_response), 1000000000ULL);
	rtsp_tap_process_packet(&state, &f,
		(const uint8_t *)teardown_request,
		strlen(teardown_request), 2000000000ULL);

	if (state.sessions[0].state != RTSP_STATE_TEARDOWN)
		return 0;

	/* Cleanup should remove TEARDOWN sessions immediately */
	rtsp_tap_cleanup(&state, 3000000000ULL, 60000000000ULL);

	if (state.session_count != 0)
		return 0;

	return 1;
}

/* Test non-RTSP data is ignored */
static int test_non_rtsp_ignored(void)
{
	struct rtsp_tap_state state;
	struct flow f;

	rtsp_tap_init(&state);
	make_test_flow(&f, "192.168.1.50", 50000, "192.168.1.100", 554);

	/* Send random data */
	const char *garbage = "GET /index.html HTTP/1.1\r\nHost: example.com\r\n\r\n";
	int ret = rtsp_tap_process_packet(&state, &f,
		(const uint8_t *)garbage, strlen(garbage), 1000000000ULL);

	if (ret != 0)
		return 0;

	if (state.session_count != 0)
		return 0;

	return 1;
}

/* Test multiple concurrent sessions */
static int test_multiple_sessions(void)
{
	struct rtsp_tap_state state;
	struct flow f1, f2;

	rtsp_tap_init(&state);

	/* Two different clients */
	make_test_flow(&f1, "192.168.1.50", 50000, "192.168.1.100", 554);
	make_test_flow(&f2, "192.168.1.51", 50001, "192.168.1.100", 554);

	/* Create two sessions */
	rtsp_tap_process_packet(&state, &f1,
		(const uint8_t *)describe_response,
		strlen(describe_response), 1000000000ULL);
	rtsp_tap_process_packet(&state, &f2,
		(const uint8_t *)describe_response,
		strlen(describe_response), 1000000000ULL);

	if (state.session_count != 2)
		return 0;

	/* Update first session */
	rtsp_tap_process_packet(&state, &f1,
		(const uint8_t *)play_request,
		strlen(play_request), 2000000000ULL);

	/* Verify states are independent */
	struct rtsp_session *s1 = &state.sessions[0];
	struct rtsp_session *s2 = &state.sessions[1];

	if (s1->state != RTSP_STATE_PLAYING)
		return 0;
	if (s2->state != RTSP_STATE_DESCRIBED)
		return 0;

	return 1;
}

/* Test sprop-parameter-sets extraction */
static int test_sprop_extraction(void)
{
	struct rtsp_tap_state state;
	struct flow f;

	rtsp_tap_init(&state);
	make_test_flow(&f, "192.168.1.50", 50000, "192.168.1.100", 554);

	rtsp_tap_process_packet(&state, &f,
		(const uint8_t *)describe_response,
		strlen(describe_response), 1000000000ULL);

	struct rtsp_media *m = &state.sessions[0].media[0];

	/* Check sprop-parameter-sets was extracted */
	if (strlen(m->sprop_params) == 0)
		return 0;

	/* Should contain base64 SPS/PPS */
	if (strstr(m->sprop_params, "Z2QAKKzZQLQ9uCgB") == NULL)
		return 0;

	return 1;
}

int main(void)
{
	printf("RTSP Tap Unit Tests\n");
	printf("==================\n");

	TEST(init);
	TEST(sdp_parsing);
	TEST(transport_parsing);
	TEST(state_transitions);
	TEST(url_extraction);
	TEST(rtp_flow_matching);
	TEST(cleanup);
	TEST(teardown_cleanup);
	TEST(non_rtsp_ignored);
	TEST(multiple_sessions);
	TEST(sprop_extraction);

	printf("\nResults: %d/%d tests passed\n", tests_passed, tests_run);

	return (tests_passed == tests_run) ? 0 : 1;
}
