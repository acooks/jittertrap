/*
 * test_video_detect.c - Unit tests for RTP and MPEG-TS detection
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>

#include "video_detect.h"

static int tests_failed = 0;

#define TEST(name) printf("Test: %s... ", name)
#define PASS() printf("PASS\n")
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

/* Helper to create a valid RTP packet */
static void make_rtp_packet(uint8_t *buf, size_t buflen, uint8_t pt, uint16_t seq, uint32_t ssrc)
{
	/* Clear entire buffer first */
	memset(buf, 0, buflen);
	/* Version=2, no padding, no extension, CC=0 */
	buf[0] = 0x80;
	/* No marker, payload type */
	buf[1] = pt & 0x7F;
	/* Sequence number (big-endian) */
	buf[2] = (seq >> 8) & 0xFF;
	buf[3] = seq & 0xFF;
	/* Timestamp */
	buf[4] = 0x00; buf[5] = 0x00; buf[6] = 0x00; buf[7] = 0x00;
	/* SSRC (big-endian) */
	buf[8] = (ssrc >> 24) & 0xFF;
	buf[9] = (ssrc >> 16) & 0xFF;
	buf[10] = (ssrc >> 8) & 0xFF;
	buf[11] = ssrc & 0xFF;
}

/* Helper to create MPEG-TS packets */
static void make_mpegts_packet(uint8_t *buf, uint16_t pid, uint8_t cc)
{
	buf[0] = 0x47;  /* Sync byte */
	buf[1] = (pid >> 8) & 0x1F;  /* PID high bits */
	buf[2] = pid & 0xFF;         /* PID low bits */
	buf[3] = 0x10 | (cc & 0x0F); /* AFC=01 (payload only), CC */
	/* Rest is payload */
	memset(buf + 4, 0xFF, 188 - 4);
}

static void test_rtp_detection_valid(void)
{
	uint8_t buf[64];
	struct rtp_info info;

	TEST("RTP detection - valid packet (PT=96, dynamic video)");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);

	if (video_detect_rtp(buf, sizeof(buf), &info)) {
		if (info.payload_type == 96 && info.ssrc == 0x12345678 &&
		    info.last_seq == 1234) {
			PASS();
		} else {
			FAIL("incorrect field values");
		}
	} else {
		FAIL("detection failed");
	}
}

static void test_rtp_detection_static_pt(void)
{
	uint8_t buf[64];
	struct rtp_info info;

	TEST("RTP detection - static PT=32 (MPEG video)");

	make_rtp_packet(buf, sizeof(buf), 32, 5678, 0xABCDEF01);

	if (video_detect_rtp(buf, sizeof(buf), &info)) {
		if (info.payload_type == 32) {
			PASS();
		} else {
			FAIL("incorrect payload type");
		}
	} else {
		FAIL("detection failed");
	}
}

static void test_rtp_detection_invalid_version(void)
{
	uint8_t buf[64];
	struct rtp_info info;

	TEST("RTP detection - invalid version (v=1)");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);
	buf[0] = 0x40;  /* Version 1 instead of 2 */

	if (!video_detect_rtp(buf, sizeof(buf), &info)) {
		PASS();
	} else {
		FAIL("should not detect invalid version");
	}
}

static void test_rtp_detection_audio_pt(void)
{
	uint8_t buf[64];
	struct rtp_info info;

	TEST("RTP detection - audio PT=0 (PCMU) should not be detected as video");

	make_rtp_packet(buf, sizeof(buf), 0, 1234, 0x12345678);

	if (!video_detect_rtp(buf, sizeof(buf), &info)) {
		PASS();
	} else {
		FAIL("audio should not be detected as video");
	}
}

static void test_rtp_detection_zero_ssrc(void)
{
	uint8_t buf[64];
	struct rtp_info info;

	TEST("RTP detection - zero SSRC (suspicious, rejected)");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0);

	if (!video_detect_rtp(buf, sizeof(buf), &info)) {
		PASS();
	} else {
		FAIL("zero SSRC should be rejected");
	}
}

static void test_rtp_detection_short_packet(void)
{
	uint8_t buf[16];
	struct rtp_info info;

	TEST("RTP detection - packet too short");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);

	/* Pass only 8 bytes, not enough for RTP header */
	if (!video_detect_rtp(buf, 8, &info)) {
		PASS();
	} else {
		FAIL("short packet should fail");
	}
}

static void test_mpegts_detection_valid(void)
{
	uint8_t buf[188 * 3];
	struct mpegts_info info;

	TEST("MPEG-TS detection - valid stream (3 packets)");

	make_mpegts_packet(buf, 0x100, 0);
	make_mpegts_packet(buf + 188, 0x100, 1);
	make_mpegts_packet(buf + 376, 0x100, 2);

	if (video_detect_mpegts(buf, sizeof(buf), &info)) {
		if (info.packets_seen == 3) {
			PASS();
		} else {
			FAIL("incorrect packet count");
		}
	} else {
		FAIL("detection failed");
	}
}

static void test_mpegts_detection_bad_sync(void)
{
	uint8_t buf[188 * 2];
	struct mpegts_info info;

	TEST("MPEG-TS detection - invalid sync byte");

	make_mpegts_packet(buf, 0x100, 0);
	buf[0] = 0x00;  /* Corrupt sync byte */
	make_mpegts_packet(buf + 188, 0x100, 1);

	if (!video_detect_mpegts(buf, sizeof(buf), &info)) {
		PASS();
	} else {
		FAIL("should reject bad sync");
	}
}

static void test_mpegts_detection_short(void)
{
	uint8_t buf[100];
	struct mpegts_info info;

	TEST("MPEG-TS detection - packet too short");

	memset(buf, 0x47, sizeof(buf));  /* Fill with sync bytes */

	if (!video_detect_mpegts(buf, sizeof(buf), &info)) {
		PASS();
	} else {
		FAIL("short packet should fail");
	}
}

static void test_video_detect_rtp_over_ts(void)
{
	uint8_t buf[64];
	struct flow_video_info info;

	TEST("video_detect() - RTP detected before TS");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);

	enum video_stream_type type = video_detect(buf, sizeof(buf), &info);
	if (type == VIDEO_STREAM_RTP && info.stream_type == VIDEO_STREAM_RTP) {
		PASS();
	} else {
		FAIL("should detect RTP");
	}
}

static void test_video_detect_ts(void)
{
	uint8_t buf[188 * 2];
	struct flow_video_info info;

	TEST("video_detect() - MPEG-TS detected");

	make_mpegts_packet(buf, 0x100, 0);
	make_mpegts_packet(buf + 188, 0x100, 1);

	enum video_stream_type type = video_detect(buf, sizeof(buf), &info);
	if (type == VIDEO_STREAM_MPEG_TS && info.stream_type == VIDEO_STREAM_MPEG_TS) {
		PASS();
	} else {
		FAIL("should detect MPEG-TS");
	}
}

static void test_video_detect_none(void)
{
	uint8_t buf[64];
	struct flow_video_info info;

	TEST("video_detect() - random data not detected");

	memset(buf, 0xAA, sizeof(buf));

	enum video_stream_type type = video_detect(buf, sizeof(buf), &info);
	if (type == VIDEO_STREAM_NONE) {
		PASS();
	} else {
		FAIL("should not detect random data");
	}
}

static void test_h264_codec_detection(void)
{
	uint8_t buf[64];
	struct rtp_info info;

	TEST("H.264 codec detection from NAL unit");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);
	/* Add H.264 NAL unit header after RTP header */
	/* forbidden_zero_bit=0, nal_ref_idc=3, nal_unit_type=5 (IDR slice) */
	buf[12] = 0x65;  /* 0b01100101 */

	if (video_detect_rtp(buf, sizeof(buf), &info)) {
		if (info.codec == VIDEO_CODEC_H264) {
			PASS();
		} else {
			printf("(got codec %d) ", info.codec);
			FAIL("wrong codec detected");
		}
	} else {
		FAIL("RTP detection failed");
	}
}

static void test_h264_fua_codec_detection(void)
{
	uint8_t buf[64];
	struct rtp_info info;

	TEST("H.264 FU-A codec detection (most common IP camera format)");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);
	/*
	 * FU-A packet for H.264 (RFC 6184):
	 * - FU indicator (1 byte): F=0, NRI=3, Type=28 (FU-A)
	 * - FU header (1 byte): S=1, E=0, R=0, Type=5 (IDR slice)
	 */
	buf[12] = 0x7C;  /* FU indicator: 0b01111100 = NRI=3, Type=28 */
	buf[13] = 0x85;  /* FU header: 0b10000101 = S=1, E=0, R=0, Type=5 (IDR) */

	if (video_detect_rtp(buf, sizeof(buf), &info)) {
		if (info.codec == VIDEO_CODEC_H264) {
			PASS();
		} else {
			printf("(got codec %d) ", info.codec);
			FAIL("wrong codec detected for FU-A");
		}
	} else {
		FAIL("RTP detection failed");
	}
}

static void test_h264_stapa_codec_detection(void)
{
	uint8_t buf[64];
	struct rtp_info info;

	TEST("H.264 STAP-A codec detection (aggregation packet)");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);
	/*
	 * STAP-A packet for H.264 (RFC 6184):
	 * - NAL header: F=0, NRI=3, Type=24 (STAP-A)
	 */
	buf[12] = 0x78;  /* 0b01111000 = NRI=3, Type=24 (STAP-A) */

	if (video_detect_rtp(buf, sizeof(buf), &info)) {
		if (info.codec == VIDEO_CODEC_H264) {
			PASS();
		} else {
			printf("(got codec %d) ", info.codec);
			FAIL("wrong codec detected for STAP-A");
		}
	} else {
		FAIL("RTP detection failed");
	}
}

static void test_h265_codec_detection(void)
{
	uint8_t buf[64];
	struct rtp_info info;

	TEST("H.265 codec detection from NAL unit");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);
	/* H.265 NAL unit header (2 bytes) */
	/* forbidden_zero_bit=0, nal_unit_type=1 (non-VCL), layer_id=0, tid=1 */
	buf[12] = 0x02;  /* type=1 in bits 1-6 */
	buf[13] = 0x01;  /* tid=1 */

	if (video_detect_rtp(buf, sizeof(buf), &info)) {
		if (info.codec == VIDEO_CODEC_H265) {
			PASS();
		} else {
			printf("(got codec %d) ", info.codec);
			FAIL("wrong codec detected");
		}
	} else {
		FAIL("RTP detection failed");
	}
}

static void test_h264_sps_extraction_single_nal(void)
{
	uint8_t buf[64];
	struct rtp_info info;

	TEST("H.264 SPS profile/level extraction (single NAL)");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);
	/*
	 * H.264 SPS NAL unit:
	 * NAL header: 0x67 = forbidden=0, ref_idc=3, type=7 (SPS)
	 * SPS payload: profile_idc, constraint_flags, level_idc, ...
	 * Example: High Profile (100), Level 4.0 (40)
	 */
	buf[12] = 0x67;  /* SPS NAL header */
	buf[13] = 0x64;  /* profile_idc = 100 (High Profile) */
	buf[14] = 0x00;  /* constraint_set flags */
	buf[15] = 0x28;  /* level_idc = 40 (Level 4.0) */

	memset(&info, 0, sizeof(info));
	if (video_detect_rtp(buf, sizeof(buf), &info)) {
		if (info.codec == VIDEO_CODEC_H264 &&
		    info.profile_idc == 0x64 &&
		    info.level_idc == 0x28) {
			PASS();
		} else {
			printf("(profile=%02X level=%02X) ", info.profile_idc, info.level_idc);
			FAIL("wrong profile/level extracted");
		}
	} else {
		FAIL("RTP detection failed");
	}
}

static void test_h264_sps_extraction_stapa(void)
{
	uint8_t buf[64];
	struct rtp_info info;

	TEST("H.264 SPS profile/level extraction (STAP-A)");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);
	/*
	 * STAP-A with SPS:
	 * NAL header: 0x78 = ref_idc=3, type=24 (STAP-A)
	 * NALU size: 4 bytes
	 * SPS NAL: header + profile + constraint + level
	 */
	buf[12] = 0x78;  /* STAP-A NAL header */
	buf[13] = 0x00;  /* NALU size high byte */
	buf[14] = 0x04;  /* NALU size low byte = 4 */
	buf[15] = 0x67;  /* SPS NAL header */
	buf[16] = 0x4D;  /* profile_idc = 77 (Main Profile) */
	buf[17] = 0x40;  /* constraint_set flags */
	buf[18] = 0x1E;  /* level_idc = 30 (Level 3.0) */

	memset(&info, 0, sizeof(info));
	if (video_detect_rtp(buf, sizeof(buf), &info)) {
		if (info.codec == VIDEO_CODEC_H264 &&
		    info.profile_idc == 0x4D &&
		    info.level_idc == 0x1E) {
			PASS();
		} else {
			printf("(profile=%02X level=%02X) ", info.profile_idc, info.level_idc);
			FAIL("wrong profile/level from STAP-A");
		}
	} else {
		FAIL("RTP detection failed");
	}
}

static void test_h264_no_sps_no_params(void)
{
	uint8_t buf[64];
	struct rtp_info info;

	TEST("H.264 no SPS - profile/level should be 0");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);
	/* IDR NAL (type 5), not SPS */
	buf[12] = 0x65;  /* IDR NAL header */
	buf[13] = 0xFF;  /* IDR payload (not SPS data) */

	memset(&info, 0, sizeof(info));
	if (video_detect_rtp(buf, sizeof(buf), &info)) {
		if (info.codec == VIDEO_CODEC_H264 &&
		    info.profile_idc == 0 &&
		    info.level_idc == 0) {
			PASS();
		} else {
			printf("(profile=%02X level=%02X) ", info.profile_idc, info.level_idc);
			FAIL("profile/level should be 0 without SPS");
		}
	} else {
		FAIL("RTP detection failed");
	}
}

static void test_h264_sps_resolution_extraction(void)
{
	uint8_t buf[128];
	struct rtp_info info;

	TEST("H.264 SPS resolution extraction (1280x720 High Profile)");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);
	/*
	 * Real H.264 SPS NAL captured from IP camera stream:
	 * rtsp://admin:admin1234@10.0.5.156//stream1
	 *
	 * SDP: sprop-parameter-sets=Z2QAHqwVFOBQBbpuDAwMgAAB9AAATiAC,aO48sA==
	 *      profile-level-id=64001E
	 *
	 * Decoded from base64: Z2QAHqwVFOBQBbpuDAwMgAAB9AAATiAC
	 * Expected: 1280x720, High Profile (100), Level 3.0
	 */
	static const uint8_t sps_720p_high[] = {
		0x67,  /* NAL header: type 7 (SPS) */
		0x64,  /* profile_idc = 100 (High Profile) */
		0x00,  /* constraint_set flags */
		0x1e,  /* level_idc = 30 (Level 3.0) */
		0xac, 0x15, 0x14, 0xe0, 0x50, 0x05, 0xba, 0x6e,
		0x0c, 0x0c, 0x0c, 0x80, 0x00, 0x01, 0xf4, 0x00,
		0x00, 0x4e, 0x20, 0x02
	};
	memcpy(buf + 12, sps_720p_high, sizeof(sps_720p_high));

	memset(&info, 0, sizeof(info));
	if (video_detect_rtp(buf, 12 + sizeof(sps_720p_high), &info)) {
		/* Verify profile/level extraction */
		if (info.profile_idc != 0x64 || info.level_idc != 0x1e) {
			printf("(profile=%02X level=%02X, expected 64/1e) ",
			       info.profile_idc, info.level_idc);
			FAIL("wrong profile/level");
			return;
		}
		/* Verify resolution extraction */
		if (info.width == 1280 && info.height == 720) {
			printf("(extracted %dx%d) ", info.width, info.height);
			PASS();
		} else if (info.width > 0 && info.height > 0) {
			printf("(extracted %dx%d, expected 1280x720) ",
			       info.width, info.height);
			FAIL("wrong resolution");
		} else {
			printf("(profile/level OK, resolution=%dx%d) ",
			       info.width, info.height);
			FAIL("resolution not extracted");
		}
	} else {
		FAIL("RTP detection failed");
	}
}

static void test_h264_sps_resolution_848x480(void)
{
	uint8_t buf[128];
	struct rtp_info info;

	TEST("H.264 SPS resolution extraction (848x480 High Profile)");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);
	/*
	 * Real H.264 SPS NAL captured from IP camera stream2:
	 * rtsp://admin:admin1234@10.0.5.156//stream2
	 *
	 * SDP: sprop-parameter-sets=Z2QAHqwVFKDUPabgwMDIAAAfQAAGGoAg,aO48sA==
	 *      profile-level-id=64001E
	 *
	 * Decoded from base64: Z2QAHqwVFKDUPabgwMDIAAAfQAAGGoAg
	 * Expected: 848x480, High Profile (100), Level 3.0
	 */
	static const uint8_t sps_480p_high[] = {
		0x67,  /* NAL header: type 7 (SPS) */
		0x64,  /* profile_idc = 100 (High Profile) */
		0x00,  /* constraint_set flags */
		0x1e,  /* level_idc = 30 (Level 3.0) */
		0xac, 0x15, 0x14, 0xa0, 0xd4, 0x3d, 0xa6, 0xe0,
		0xc0, 0xc0, 0xc8, 0x00, 0x00, 0x1f, 0x40, 0x00,
		0x06, 0x1a, 0x80, 0x20
	};
	memcpy(buf + 12, sps_480p_high, sizeof(sps_480p_high));

	memset(&info, 0, sizeof(info));
	if (video_detect_rtp(buf, 12 + sizeof(sps_480p_high), &info)) {
		/* Verify profile/level extraction */
		if (info.profile_idc != 0x64 || info.level_idc != 0x1e) {
			printf("(profile=%02X level=%02X, expected 64/1e) ",
			       info.profile_idc, info.level_idc);
			FAIL("wrong profile/level");
			return;
		}
		/* Verify resolution extraction */
		if (info.width == 848 && info.height == 480) {
			printf("(extracted %dx%d) ", info.width, info.height);
			PASS();
		} else if (info.width > 0 && info.height > 0) {
			printf("(extracted %dx%d, expected 848x480) ",
			       info.width, info.height);
			FAIL("wrong resolution");
		} else {
			printf("(profile/level OK, resolution=%dx%d) ",
			       info.width, info.height);
			FAIL("resolution not extracted");
		}
	} else {
		FAIL("RTP detection failed");
	}
}

/*
 * Test: H.264 SPS resolution extraction - 1280x720 10fps High Profile
 * Uses real SPS from user's camera at 10fps setting (different VUI timing)
 * SPS base64: Z2QAHqwVFKBQBbpuDAwMgAAB9AAAJxAC
 * Expected: 1280x720, profile_idc=0x64 (High), level_idc=0x1e (3.0)
 */
static void test_h264_sps_resolution_1280x720_10fps(void)
{
	uint8_t buf[128];
	struct rtp_info info;

	TEST("H.264 SPS resolution extraction (1280x720 10fps High Profile)");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);
	/*
	 * Real H.264 SPS NAL captured from IP camera stream1 at 10fps:
	 * rtsp://admin:admin1234@10.0.5.156//stream1
	 *
	 * SDP: sprop-parameter-sets=Z2QAHqwVFKBQBbpuDAwMgAAB9AAAJxAC,aO48sA==
	 *      profile-level-id=64001E
	 *
	 * Decoded from base64: Z2QAHqwVFKBQBbpuDAwMgAAB9AAAJxAC
	 * Expected: 1280x720, High Profile (100), Level 3.0
	 * Note: Different VUI timing params compared to 25fps version
	 */
	static const uint8_t sps_720p_10fps[] = {
		0x67,  /* NAL header: type 7 (SPS) */
		0x64,  /* profile_idc = 100 (High Profile) */
		0x00,  /* constraint_set flags */
		0x1e,  /* level_idc = 30 (Level 3.0) */
		0xac, 0x15, 0x14, 0xa0, 0x50, 0x05, 0xba, 0x6e,
		0x0c, 0x0c, 0x0c, 0x80, 0x00, 0x01, 0xf4, 0x00,
		0x00, 0x27, 0x10, 0x02
	};
	memcpy(buf + 12, sps_720p_10fps, sizeof(sps_720p_10fps));

	memset(&info, 0, sizeof(info));
	if (video_detect_rtp(buf, 12 + sizeof(sps_720p_10fps), &info)) {
		/* Verify profile/level extraction */
		if (info.profile_idc != 0x64 || info.level_idc != 0x1e) {
			printf("(profile=%02X level=%02X, expected 64/1e) ",
			       info.profile_idc, info.level_idc);
			FAIL("wrong profile/level");
			return;
		}
		/* Verify resolution extraction - should be same as 25fps */
		if (info.width == 1280 && info.height == 720) {
			printf("(extracted %dx%d) ", info.width, info.height);
			PASS();
		} else if (info.width > 0 && info.height > 0) {
			printf("(extracted %dx%d, expected 1280x720) ",
			       info.width, info.height);
			FAIL("wrong resolution");
		} else {
			printf("(profile/level OK, resolution=%dx%d) ",
			       info.width, info.height);
			FAIL("resolution not extracted");
		}
	} else {
		FAIL("RTP detection failed");
	}
}

/*
 * Test: H.264 SPS resolution extraction (2048x1280 High Profile Level 5.0)
 * Real SPS from TP-Link camera stream1
 * profile-level-id=640032, sprop=Z2QAMqwVFOAgAKGm4MDAyAAAH0AABhqAIA==
 * ffprobe confirms: 2048x1280, High Profile, Level 5.0
 */
static void test_h264_sps_resolution_2048x1280(void)
{
	uint8_t buf[128];
	struct rtp_info info;

	TEST("H.264 SPS resolution extraction (2048x1280 High Profile Level 5.0)");

	/* Create a minimal RTP packet with H.264 payload type 96 */
	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);

	/* Real SPS from camera: Z2QAMqwVFOAgAKGm4MDAyAAAH0AABhqAIA== (25 bytes)
	 * Decoded hex: 67 64 00 32 ac 15 14 e0 20 00 a1 a6 e0 c0 c0 c8 00 00 1f 40 00 06 1a 80 20
	 * profile_idc=0x64 (High), level_idc=0x32 (Level 5.0)
	 * Expected resolution: 2048x1280
	 */
	static const uint8_t sps_2048x1280[] = {
		0x67, 0x64, 0x00, 0x32, 0xac, 0x15, 0x14, 0xe0,
		0x20, 0x00, 0xa1, 0xa6, 0xe0, 0xc0, 0xc0, 0xc8,
		0x00, 0x00, 0x1f, 0x40, 0x00, 0x06, 0x1a, 0x80,
		0x20
	};

	/* Copy SPS after RTP header */
	memcpy(buf + 12, sps_2048x1280, sizeof(sps_2048x1280));

	memset(&info, 0, sizeof(info));

	if (video_detect_rtp(buf, 12 + sizeof(sps_2048x1280), &info)) {
		/* Print extracted values for verification */
		printf("(extracted %ux%u) ",
			info.width, info.height);
		/* Verify resolution, profile and level */
		if (info.width != 2048 || info.height != 1280) {
			FAIL("resolution mismatch");
		} else if (info.profile_idc != 0x64) {
			FAIL("profile_idc mismatch");
		} else if (info.level_idc != 0x32) {
			FAIL("level_idc mismatch");
		} else {
			PASS();
		}
	} else {
		FAIL("RTP detection failed");
	}
}

/*
 * Test: H.264 SPS resolution extraction (1920x1080 High Profile Level 4.0)
 * Real SPS from TP-Link camera stream1
 * profile-level-id=640028, sprop=Z2QAKKwVFOB4Aiflm4MDAyAAAH0AABhqAIA=
 * ffprobe confirms: 1920x1080, High Profile, Level 4.0
 */
static void test_h264_sps_resolution_1920x1080(void)
{
	uint8_t buf[128];
	struct rtp_info info;

	TEST("H.264 SPS resolution extraction (1920x1080 High Profile Level 4.0)");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);

	/* Real SPS from camera: Z2QAKKwVFOB4Aiflm4MDAyAAAH0AABhqAIA= (26 bytes)
	 * Decoded hex: 67 64 00 28 ac 15 14 e0 78 02 27 e5 9b 83 03 03 20 00 00 7d 00 00 18 6a 00 80
	 * profile_idc=0x64 (High), level_idc=0x28 (Level 4.0)
	 * Expected resolution: 1920x1080
	 */
	static const uint8_t sps_1920x1080[] = {
		0x67, 0x64, 0x00, 0x28, 0xac, 0x15, 0x14, 0xe0,
		0x78, 0x02, 0x27, 0xe5, 0x9b, 0x83, 0x03, 0x03,
		0x20, 0x00, 0x00, 0x7d, 0x00, 0x00, 0x18, 0x6a,
		0x00, 0x80
	};

	memcpy(buf + 12, sps_1920x1080, sizeof(sps_1920x1080));
	memset(&info, 0, sizeof(info));

	if (video_detect_rtp(buf, 12 + sizeof(sps_1920x1080), &info)) {
		printf("(extracted %ux%u) ", info.width, info.height);
		if (info.width != 1920 || info.height != 1080) {
			FAIL("resolution mismatch");
		} else if (info.profile_idc != 0x64) {
			FAIL("profile_idc mismatch");
		} else if (info.level_idc != 0x28) {
			FAIL("level_idc mismatch");
		} else {
			PASS();
		}
	} else {
		FAIL("RTP detection failed");
	}
}

/*
 * Test: H.265 SPS profile/tier/level extraction
 * Synthetic H.265 SPS with Main profile, Main tier, Level 4.0
 */
static void test_h265_sps_extraction(void)
{
	uint8_t buf[128];
	struct rtp_info info;

	TEST("H.265 SPS profile/tier/level extraction (Main Profile Main@L4.0)");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);

	/*
	 * H.265 SPS NAL unit (type 33 = 0x21):
	 * - NAL header: 2 bytes (0x42 0x01 for SPS type 33)
	 * - sps_video_parameter_set_id: 4 bits = 0
	 * - sps_max_sub_layers_minus1: 3 bits = 0
	 * - sps_temporal_id_nesting_flag: 1 bit = 1
	 * - profile_tier_level:
	 *   - general_profile_space: 2 bits = 0
	 *   - general_tier_flag: 1 bit = 0 (Main tier)
	 *   - general_profile_idc: 5 bits = 1 (Main profile)
	 *   - general_profile_compatibility_flags: 32 bits = 0x60000000
	 *   - general_progressive_source_flag: 1 bit = 1
	 *   - general_interlaced_source_flag: 1 bit = 0
	 *   - general_non_packed_constraint_flag: 1 bit = 1
	 *   - general_frame_only_constraint_flag: 1 bit = 1
	 *   - (44 reserved zero bits)
	 *   - general_level_idc: 8 bits = 120 (Level 4.0 = 120)
	 *
	 * Byte encoding for profile_space/tier/profile_idc:
	 *   0x01 = 00(space) + 0(tier=Main) + 00001(profile=1=Main) = 0000.0001
	 */
	static const uint8_t h265_sps[] = {
		0x42, 0x01,  /* NAL header: type = 33 (SPS), layer_id=0, tid=1 */
		0x01,        /* sps_video_parameter_set_id=0, max_sub_layers=0, temporal_nesting=1 */
		0x01,        /* profile_space=0, tier=0 (Main), profile_idc=1 (Main) */
		0x60, 0x00, 0x00, 0x00,  /* profile_compatibility_flags */
		0xB0, 0x00, 0x00, 0x00, 0x00, 0x00,  /* constraint flags (48 bits) */
		0x78         /* level_idc = 120 (Level 4.0) */
	};
	memcpy(buf + 12, h265_sps, sizeof(h265_sps));

	memset(&info, 0, sizeof(info));
	if (video_detect_rtp(buf, 12 + sizeof(h265_sps), &info)) {
		printf("(profile=%02X level=%02X) ", info.profile_idc, info.level_idc);
		/* profile_idc should be 1 (Main profile), tier bit 7 = 0 (Main tier) */
		if (info.codec != VIDEO_CODEC_H265) {
			FAIL("wrong codec detected");
		} else if (info.profile_idc != 1) {
			FAIL("profile should be 1 (Main)");
		} else if (info.level_idc != 120) {
			FAIL("level should be 120 (L4.0)");
		} else {
			PASS();
		}
	} else {
		FAIL("RTP detection failed");
	}
}

/*
 * Test: H.265 SPS profile/tier/level extraction - High tier
 */
static void test_h265_sps_extraction_high_tier(void)
{
	uint8_t buf[128];
	struct rtp_info info;

	TEST("H.265 SPS profile/tier/level extraction (Main 10 Profile High@L5.1)");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);

	/*
	 * H.265 SPS with Main 10 profile, High tier, Level 5.1
	 * - profile_idc = 2 (Main 10)
	 * - tier_flag = 1 (High tier)
	 * - level_idc = 153 (Level 5.1 = 5.1 * 30)
	 *
	 * Byte encoding for profile_space/tier/profile_idc:
	 *   0x22 = 00(space) + 1(tier=High) + 00010(profile=2=Main10) = 0010.0010
	 */
	static const uint8_t h265_sps_high[] = {
		0x42, 0x01,  /* NAL header: type = 33 (SPS) */
		0x01,        /* sps_video_parameter_set_id=0, max_sub_layers=0, temporal_nesting=1 */
		0x22,        /* profile_space=0, tier=1 (High), profile_idc=2 (Main 10) */
		0x40, 0x00, 0x00, 0x00,  /* profile_compatibility_flags */
		0xB0, 0x00, 0x00, 0x00, 0x00, 0x00,  /* constraint flags */
		0x99         /* level_idc = 153 (Level 5.1) */
	};
	memcpy(buf + 12, h265_sps_high, sizeof(h265_sps_high));

	memset(&info, 0, sizeof(info));
	if (video_detect_rtp(buf, 12 + sizeof(h265_sps_high), &info)) {
		printf("(profile=%02X level=%02X) ", info.profile_idc, info.level_idc);
		/* profile should have bit 7 set for High tier, bits 0-4 = 2 (Main 10) */
		/* Expected: 0x82 (128 + 2) */
		if (info.codec != VIDEO_CODEC_H265) {
			FAIL("wrong codec detected");
		} else if ((info.profile_idc & 0x1F) != 2) {
			FAIL("profile should be 2 (Main 10)");
		} else if ((info.profile_idc & 0x80) == 0) {
			FAIL("High tier flag should be set");
		} else if (info.level_idc != 153) {
			FAIL("level should be 153 (L5.1)");
		} else {
			PASS();
		}
	} else {
		FAIL("RTP detection failed");
	}
}

/*
 * Test: H.265 SPS resolution extraction - 1920x1080 Main Profile
 * Test case with pic_width_in_luma_samples and pic_height_in_luma_samples
 */
static void test_h265_sps_resolution_extraction(void)
{
	uint8_t buf[128];
	struct rtp_info info;

	TEST("H.265 SPS resolution extraction (1920x1080 Main Profile)");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);

	/*
	 * H.265 SPS with 1920x1080 resolution, Main profile, Level 4.0
	 *
	 * Structure (per ITU-T H.265):
	 * - NAL unit header: 2 bytes (type=33=SPS, layer_id=0, tid=1)
	 * - sps_video_parameter_set_id: 4 bits = 0
	 * - sps_max_sub_layers_minus1: 3 bits = 0 (1 sub-layer)
	 * - sps_temporal_id_nesting_flag: 1 bit = 1
	 * - profile_tier_level(0):
	 *   - general_profile_space: 2 bits = 0
	 *   - general_tier_flag: 1 bit = 0 (Main tier)
	 *   - general_profile_idc: 5 bits = 1 (Main profile)
	 *   - general_profile_compatibility_flags: 32 bits
	 *   - constraint flags: 48 bits
	 *   - general_level_idc: 8 bits = 120 (Level 4.0)
	 * - sps_seq_parameter_set_id: ue = 0 (1 bit = 1)
	 * - chroma_format_idc: ue = 1 (4:2:0, 3 bits = 010)
	 * - pic_width_in_luma_samples: ue = 1920 (22 bits)
	 * - pic_height_in_luma_samples: ue = 1080 (20 bits)
	 * - conformance_window_flag: 1 bit = 0 (no cropping)
	 *
	 * Encoding:
	 * After level_idc (0x78), we need:
	 * - sps_seq_parameter_set_id: ue(0) = 1 (1 bit)
	 * - chroma_format_idc: ue(1) = 010 (3 bits)
	 * - pic_width: ue(1920) = 0000000001 1110000001 (22 bits: 10 leading zeros, 11 bits value)
	 * - pic_height: ue(1080) = 000000001 10000111001 (20 bits: 9 leading zeros, 10 bits value)
	 * - conformance_window_flag: 0 (1 bit)
	 *
	 * Bitstream after level_idc:
	 * 1 010 0000000001 1110000001 000000001 10000111001 0
	 * = 1010 0000 0000 1111 0000 0010 0000 0001 1000 0111 0010
	 * = 0xA0 0x0F 0x02 0x01 0x87 0x20 (approximately - needs exact encoding)
	 *
	 * Actually let's use a simpler approach with manually calculated bytes:
	 * sps_seq_parameter_set_id=0: 1
	 * chroma_format_idc=1: 010
	 * pic_width_in_luma_samples=1920: 1920 needs ceil(log2(1920+1))=11 bits
	 *   ue encoding: (10 leading zeros) + 1 + (10 bits of 1919) = 0000000000 1 11 01111111
	 *   = 21 bits for ue(1920)
	 * pic_height_in_luma_samples=1080: 1080 needs ceil(log2(1081))=11 bits
	 *   ue encoding: (10 leading zeros) + 1 + (10 bits of 1079) = 0000000000 1 10 00110111
	 *   = 21 bits for ue(1080)
	 *
	 * Combined: 1 010 [21 bits] [21 bits] 0 = 47 bits total after level_idc
	 * That's 6 bytes needed
	 *
	 * Let me compute exactly:
	 * ue(0) = 1
	 * ue(1) = 010
	 * ue(1920): 1920 = 0x780, 1921 in binary = 11110000001 (11 bits)
	 *   Leading zeros = 10, then 11110000001
	 *   Total: 0000000000 11110000001 = 21 bits
	 * ue(1080): 1080 = 0x438, 1081 in binary = 10000111001 (11 bits)
	 *   Leading zeros = 10, then 10000111001
	 *   Total: 0000000000 10000111001 = 21 bits
	 *
	 * Bitstream: 1 010 0000000000 11110000001 0000000000 10000111001 0
	 *          : 1010 0000 0000 0011 1100 0000 1000 0000 0001 0000 1110 0100
	 *          = 0xA0 0x03 0xC0 0x80 0x10 0xE4
	 */
	static const uint8_t h265_sps_1080p[] = {
		0x42, 0x01,  /* NAL header: type = 33 (SPS), layer_id=0, tid=1 */
		0x01,        /* sps_video_parameter_set_id=0, max_sub_layers=0, temporal_nesting=1 */
		0x01,        /* profile_space=0, tier=0 (Main), profile_idc=1 (Main) */
		0x60, 0x00, 0x00, 0x00,  /* profile_compatibility_flags */
		0xB0, 0x00, 0x00, 0x00, 0x00, 0x00,  /* constraint flags (48 bits) */
		0x78,        /* level_idc = 120 (Level 4.0) */
		/* Now the SPS parameters proper (47 bits = 6 bytes):
		 * ue(0)=1, ue(1)=010, ue(1920)=21 bits, ue(1080)=21 bits, conf=0 */
		0xA0,        /* 1010 0000 - sps_id=0, chroma=1, start of width zeros */
		0x03,        /* 0000 0011 - continuing width zeros, start of 1921 */
		0xC0,        /* 1100 0000 - continuing 1921 value */
		0x80,        /* 1000 0000 - end of width, start of height zeros */
		0x10,        /* 0001 0000 - continuing height zeros, start of 1081 */
		0xE4,        /* 1110 0100 - end of 1081, conformance=0 */
	};
	memcpy(buf + 12, h265_sps_1080p, sizeof(h265_sps_1080p));

	memset(&info, 0, sizeof(info));
	if (video_detect_rtp(buf, 12 + sizeof(h265_sps_1080p), &info)) {
		/* First verify codec and profile/level */
		if (info.codec != VIDEO_CODEC_H265) {
			FAIL("wrong codec detected");
			return;
		}
		if (info.profile_idc != 1 || info.level_idc != 120) {
			printf("(profile=%02X level=%02X, expected 01/78) ",
			       info.profile_idc, info.level_idc);
			FAIL("wrong profile/level");
			return;
		}
		/* Check resolution extraction */
		if (info.width == 1920 && info.height == 1080) {
			printf("(extracted %dx%d) ", info.width, info.height);
			PASS();
		} else {
			printf("(extracted %dx%d, expected 1920x1080) ",
			       info.width, info.height);
			FAIL("wrong resolution");
		}
	} else {
		FAIL("RTP detection failed");
	}
}

/*
 * Test: H.265 SPS with emulation prevention bytes (real camera data)
 * This tests the removal of 0x000003 sequences before parsing.
 * Data captured from: rtsp://10.0.5.156:554//stream1 (2880x1620 HEVC Main L5.0)
 */
static void test_h265_sps_emulation_prevention(void)
{
	uint8_t buf[128];
	struct rtp_info info;

	TEST("H.265 SPS with emulation prevention bytes (2880x1620 Main@L5.0)");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x12345678);

	/*
	 * Real H.265 SPS from IP camera with emulation prevention bytes.
	 * sprop-sps=QgEEAWAAAAMAAAMAAAMAAAMAlgAAoAFoIAZh8+Kve9O6Jrv5uDAwMCACky4AM3+YAQ==
	 *
	 * This SPS contains multiple 0x000003 sequences that must be removed:
	 * Raw:     420104016000000300000300000300000300960000a00168200661f3e2af7bd3ba26bbf9b830303020...
	 * Cleaned: 4201040160000000000000000000960000a00168200661f3e2af7bd3ba26bbf9b830303020...
	 *
	 * Expected: Profile 1 (Main), Main Tier, Level 150 (5.0), 2880x1620
	 */
	static const uint8_t h265_sps_real[] = {
		0x42, 0x01,  /* NAL header: type = 33 (SPS) */
		0x04,        /* vps_id=0, max_sub_layers_minus1=2, temporal_nesting=0 */
		0x01,        /* profile_space=0, tier=0 (Main), profile_idc=1 (Main) */
		0x60, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00,  /* compat + constraints with EPB */
		0x00, 0x03, 0x00, 0x00, 0x03, 0x00,  /* more constraints with EPB */
		0x96,        /* level_idc = 150 (Level 5.0) */
		0x00, 0x00, 0xa0, 0x01, 0x68, 0x20, 0x06, 0x61,  /* rest of SPS */
		0xf3, 0xe2, 0xaf, 0x7b, 0xd3, 0xba, 0x26, 0xbb,
		0xf9, 0xb8, 0x30, 0x30, 0x30, 0x20, 0x02, 0x93,
		0x2e, 0x00, 0x33, 0x7f, 0x98, 0x01
	};
	memcpy(buf + 12, h265_sps_real, sizeof(h265_sps_real));

	memset(&info, 0, sizeof(info));
	if (video_detect_rtp(buf, 12 + sizeof(h265_sps_real), &info)) {
		if (info.codec != VIDEO_CODEC_H265) {
			FAIL("wrong codec detected");
			return;
		}
		/* Check profile (should be 1, no tier flag in high bit) */
		if (info.profile_idc != 1) {
			printf("(profile=%02X, expected 01) ", info.profile_idc);
			FAIL("wrong profile");
			return;
		}
		/* Check level (should be 150 = Level 5.0) */
		if (info.level_idc != 150) {
			printf("(level=%u, expected 150) ", info.level_idc);
			FAIL("wrong level");
			return;
		}
		/* Check resolution */
		if (info.width == 2880 && info.height == 1620) {
			printf("(extracted %dx%d, profile=%u, level=%.1f) ",
			       info.width, info.height, info.profile_idc, info.level_idc/30.0);
			PASS();
		} else {
			printf("(extracted %dx%d, expected 2880x1620) ",
			       info.width, info.height);
			FAIL("wrong resolution");
		}
	} else {
		FAIL("RTP detection failed");
	}
}

/* Audio detection tests */
static void test_audio_detection_pcmu(void)
{
	uint8_t buf[64];
	struct rtp_info info;

	TEST("Audio detection - PCMU (PT=0)");

	make_rtp_packet(buf, sizeof(buf), 0, 1000, 0x11111111);

	if (audio_detect_rtp(buf, sizeof(buf), &info)) {
		if (info.audio_codec == AUDIO_CODEC_PCMU &&
		    info.sample_rate_khz == 8 &&
		    info.ssrc == 0x11111111) {
			PASS();
		} else {
			FAIL("incorrect audio fields");
		}
	} else {
		FAIL("detection failed");
	}
}

static void test_audio_detection_pcma(void)
{
	uint8_t buf[64];
	struct rtp_info info;

	TEST("Audio detection - PCMA (PT=8)");

	make_rtp_packet(buf, sizeof(buf), 8, 2000, 0x22222222);

	if (audio_detect_rtp(buf, sizeof(buf), &info)) {
		if (info.audio_codec == AUDIO_CODEC_PCMA &&
		    info.sample_rate_khz == 8) {
			PASS();
		} else {
			FAIL("incorrect audio fields");
		}
	} else {
		FAIL("detection failed");
	}
}

static void test_audio_detection_g729(void)
{
	uint8_t buf[64];
	struct rtp_info info;

	TEST("Audio detection - G.729 (PT=18)");

	make_rtp_packet(buf, sizeof(buf), 18, 3000, 0x33333333);

	if (audio_detect_rtp(buf, sizeof(buf), &info)) {
		if (info.audio_codec == AUDIO_CODEC_G729 &&
		    info.sample_rate_khz == 8) {
			PASS();
		} else {
			FAIL("incorrect audio fields");
		}
	} else {
		FAIL("detection failed");
	}
}

static void test_audio_detection_video_pt_rejected(void)
{
	uint8_t buf[64];
	struct rtp_info info;

	TEST("Audio detection - video PT=96 should not be detected as audio");

	make_rtp_packet(buf, sizeof(buf), 96, 4000, 0x44444444);

	if (!audio_detect_rtp(buf, sizeof(buf), &info)) {
		PASS();
	} else {
		FAIL("video PT incorrectly detected as audio");
	}
}

static void test_audio_detection_invalid_rtp(void)
{
	uint8_t buf[64];
	struct rtp_info info;

	TEST("Audio detection - invalid RTP version");

	make_rtp_packet(buf, sizeof(buf), 0, 5000, 0x55555555);
	buf[0] = 0x40;  /* Version 1 instead of 2 */

	if (!audio_detect_rtp(buf, sizeof(buf), &info)) {
		PASS();
	} else {
		FAIL("invalid RTP incorrectly detected");
	}
}

/*
 * Test: H.265 FU (Fragmentation Unit) codec detection
 * Uses real packet bytes captured from IP camera H.265 stream
 * Camera sends 0x62 0x03 which is H.265 FU (type 49), tid=3
 */
static void test_h265_fu_codec_detection(void)
{
	uint8_t buf[64];
	struct rtp_info info;

	TEST("H.265 FU codec detection (real camera packet)");

	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x6d9f04a5);
	/*
	 * H.265 FU packet from camera:
	 * Byte 0: 0x62 - F=0, Type=(0x62>>1)&0x3F=49 (FU), LayerId_high=0
	 * Byte 1: 0x03 - LayerId_low=0, TID=3
	 * Byte 2: 0x81 - S=1, E=0, FuType=1 (VCL NAL)
	 */
	buf[12] = 0x62;  /* H.265 NAL header byte 0: type 49 (FU) */
	buf[13] = 0x03;  /* H.265 NAL header byte 1: tid=3, layer_id=0 */
	buf[14] = 0x81;  /* FU header: S=1, E=0, FuType=1 */

	if (video_detect_rtp(buf, sizeof(buf), &info)) {
		if (info.codec == VIDEO_CODEC_H265) {
			PASS();
		} else {
			printf("(got codec %d, expected %d) ", info.codec, VIDEO_CODEC_H265);
			FAIL("wrong codec - expected H.265");
		}
	} else {
		FAIL("RTP detection failed");
	}
}

/*
 * Test that H.265 FU packets (VCL data, no SPS) result in zero resolution.
 * This matches the camera behavior where SPS is in RTSP SDP, not RTP.
 */
static void test_h265_fu_no_resolution(void)
{
	uint8_t buf[64];
	struct rtp_info info;

	TEST("H.265 FU packet should have zero resolution (no SPS)");

	memset(&info, 0xFF, sizeof(info));  /* Fill with non-zero to detect changes */
	make_rtp_packet(buf, sizeof(buf), 96, 1234, 0x6d9f04a5);
	/*
	 * H.265 FU packet with VCL data (not SPS):
	 * Byte 0: 0x62 - Type=49 (FU)
	 * Byte 1: 0x03 - TID=3, LayerId=0
	 * Byte 2: 0x81 - S=1, E=0, FuType=1 (TRAIL_R, not SPS type 33)
	 */
	buf[12] = 0x62;
	buf[13] = 0x03;
	buf[14] = 0x81;  /* FuType=1 (TRAIL_R), not 33 (SPS) */

	if (video_detect_rtp(buf, sizeof(buf), &info)) {
		if (info.codec == VIDEO_CODEC_H265 &&
		    info.width == 0 && info.height == 0) {
			PASS();
		} else {
			printf("(codec=%d, width=%d, height=%d) ",
			       info.codec, info.width, info.height);
			FAIL("expected H.265 with 0x0 resolution");
		}
	} else {
		FAIL("RTP detection failed");
	}
}

int main(void)
{
	printf("\n=== Video Detection Unit Tests ===\n\n");

	/* RTP detection tests */
	test_rtp_detection_valid();
	test_rtp_detection_static_pt();
	test_rtp_detection_invalid_version();
	test_rtp_detection_audio_pt();
	test_rtp_detection_zero_ssrc();
	test_rtp_detection_short_packet();

	/* MPEG-TS detection tests */
	test_mpegts_detection_valid();
	test_mpegts_detection_bad_sync();
	test_mpegts_detection_short();

	/* Combined detection tests */
	test_video_detect_rtp_over_ts();
	test_video_detect_ts();
	test_video_detect_none();

	/* Codec detection tests */
	test_h264_codec_detection();
	test_h264_fua_codec_detection();
	test_h264_stapa_codec_detection();
	test_h265_codec_detection();
	test_h265_fu_codec_detection();

	/* SPS extraction tests */
	test_h264_sps_extraction_single_nal();
	test_h264_sps_extraction_stapa();
	test_h264_no_sps_no_params();
	test_h264_sps_resolution_extraction();
	test_h264_sps_resolution_848x480();
	test_h264_sps_resolution_1280x720_10fps();
	test_h264_sps_resolution_2048x1280();
	test_h264_sps_resolution_1920x1080();

	/* H.265 SPS extraction tests */
	test_h265_sps_extraction();
	test_h265_sps_extraction_high_tier();
	test_h265_sps_resolution_extraction();
	test_h265_sps_emulation_prevention();
	test_h265_fu_no_resolution();

	/* Audio detection tests */
	test_audio_detection_pcmu();
	test_audio_detection_pcma();
	test_audio_detection_g729();
	test_audio_detection_video_pt_rejected();
	test_audio_detection_invalid_rtp();

	printf("\n=== Results: %d test(s) failed ===\n\n", tests_failed);

	return tests_failed;
}
