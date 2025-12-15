#ifndef VIDEO_DETECT_H
#define VIDEO_DETECT_H

#include <stdint.h>
#include <stddef.h>
#include "flow.h"

/*
 * RTP Header (RFC 3550)
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |V=2|P|X|  CC   |M|     PT      |       sequence number         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                           timestamp                           |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                             SSRC                              |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
struct hdr_rtp {
	uint8_t vpxcc;      /* V=2 (2 bits), P (1), X (1), CC (4) */
	uint8_t mpt;        /* M (1 bit), PT (7 bits) */
	uint16_t seq;       /* Sequence number */
	uint32_t timestamp; /* RTP timestamp */
	uint32_t ssrc;      /* Synchronization source */
} __attribute__((__packed__));

#define RTP_VERSION(rtp) (((rtp)->vpxcc >> 6) & 0x03)
#define RTP_PADDING(rtp) (((rtp)->vpxcc >> 5) & 0x01)
#define RTP_EXTENSION(rtp) (((rtp)->vpxcc >> 4) & 0x01)
#define RTP_CSRC_COUNT(rtp) ((rtp)->vpxcc & 0x0F)
#define RTP_MARKER(rtp) (((rtp)->mpt >> 7) & 0x01)
#define RTP_PAYLOAD_TYPE(rtp) ((rtp)->mpt & 0x7F)

/* Minimum RTP header size (without CSRC list) */
#define RTP_HDR_MIN_SIZE 12

/*
 * MPEG-TS Packet Header
 *
 * Each MPEG-TS packet is 188 bytes with a 4-byte header:
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |   sync=0x47   |TEI|PUSI|TP| PID (13 bits) |TSC|AFC|    CC     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
#define MPEGTS_SYNC_BYTE 0x47
#define MPEGTS_PACKET_SIZE 188

struct hdr_mpegts {
	uint8_t sync;       /* Sync byte (0x47) */
	uint8_t pid_hi;     /* TEI(1), PUSI(1), TP(1), PID high 5 bits */
	uint8_t pid_lo;     /* PID low 8 bits */
	uint8_t flags_cc;   /* TSC(2), AFC(2), CC(4) */
} __attribute__((__packed__));

#define MPEGTS_PID(ts) ((((ts)->pid_hi & 0x1F) << 8) | (ts)->pid_lo)
#define MPEGTS_CC(ts) ((ts)->flags_cc & 0x0F)
#define MPEGTS_AFC(ts) (((ts)->flags_cc >> 4) & 0x03)
#define MPEGTS_PUSI(ts) (((ts)->pid_hi >> 6) & 0x01)

/* Well-known MPEG-TS PIDs */
#define MPEGTS_PID_PAT 0x0000   /* Program Association Table */
#define MPEGTS_PID_CAT 0x0001   /* Conditional Access Table */
#define MPEGTS_PID_NULL 0x1FFF /* Null packets */

/* NAL unit types for H.264/H.265 codec detection */
#define H264_NAL_TYPE_MASK 0x1F
#define H264_NAL_SPS 7
#define H264_NAL_PPS 8

#define H265_NAL_TYPE_MASK 0x3F
#define H265_NAL_VPS 32
#define H265_NAL_SPS 33
#define H265_NAL_PPS 34

/*
 * Detect if a UDP payload is an RTP stream.
 *
 * @param payload     Pointer to UDP payload data
 * @param payload_len Length of UDP payload
 * @param rtp_info    Output: RTP header information if detected
 *
 * @return 1 if RTP detected, 0 otherwise
 */
int video_detect_rtp(const uint8_t *payload, size_t payload_len,
                     struct rtp_info *rtp_info);

/*
 * Detect if a UDP payload is an MPEG-TS stream.
 *
 * @param payload     Pointer to UDP payload data
 * @param payload_len Length of UDP payload
 * @param ts_info     Output: MPEG-TS information if detected
 *
 * @return 1 if MPEG-TS detected, 0 otherwise
 */
int video_detect_mpegts(const uint8_t *payload, size_t payload_len,
                        struct mpegts_info *ts_info);

/*
 * Detect video stream type from UDP payload.
 * Tries RTP first, then MPEG-TS.
 *
 * @param payload     Pointer to UDP payload data
 * @param payload_len Length of UDP payload
 * @param video_info  Output: Video stream information
 *
 * @return enum video_stream_type (VIDEO_STREAM_NONE if not detected)
 */
enum video_stream_type video_detect(const uint8_t *payload, size_t payload_len,
                                    struct flow_video_info *video_info);

/*
 * Detect video codec from RTP payload based on payload type and NAL units.
 *
 * @param payload      Pointer to RTP payload (after RTP header)
 * @param payload_len  Length of payload
 * @param payload_type RTP payload type
 *
 * @return enum video_codec
 */
enum video_codec video_detect_rtp_codec(const uint8_t *payload,
                                        size_t payload_len,
                                        uint8_t payload_type);

/*
 * Detect video codec from MPEG-TS stream by analyzing PES headers.
 *
 * @param payload     Pointer to TS packet payload
 * @param payload_len Length of payload
 *
 * @return enum video_codec
 */
enum video_codec video_detect_ts_codec(const uint8_t *payload,
                                       size_t payload_len);

/*
 * Check if RTP payload contains a keyframe (IDR for H.264/H.265).
 *
 * @param rtp_payload  Pointer to RTP payload (after RTP header)
 * @param len          Length of payload
 * @param codec        Video codec type
 *
 * @return 1 if keyframe detected, 0 otherwise
 */
int video_detect_is_keyframe(const uint8_t *rtp_payload, size_t len,
                             enum video_codec codec);

/*
 * Get RTP timestamp from packet header.
 *
 * @param payload      Pointer to UDP payload (RTP packet)
 * @param payload_len  Length of payload
 *
 * @return RTP timestamp in network byte order, or 0 on failure
 */
uint32_t video_detect_get_rtp_timestamp(const uint8_t *payload, size_t payload_len);

/*
 * Detect if a UDP payload is an RTP audio stream.
 *
 * @param payload     Pointer to UDP payload data
 * @param payload_len Length of UDP payload
 * @param rtp_info    Output: RTP header information if detected
 *
 * @return 1 if RTP audio detected, 0 otherwise
 */
int audio_detect_rtp(const uint8_t *payload, size_t payload_len,
                     struct rtp_info *rtp_info);

/*
 * Detect audio codec from RTP payload type.
 *
 * @param payload_type RTP payload type
 *
 * @return enum audio_codec
 */
enum audio_codec audio_detect_codec(uint8_t payload_type);

/*
 * Get RTP clock rate in Hz for an audio codec.
 * Used for RFC 3550 jitter calculation.
 *
 * @param codec Audio codec type
 *
 * @return Clock rate in Hz (e.g., 8000 for G.711, 48000 for Opus)
 */
uint32_t audio_clock_rate_hz(enum audio_codec codec);

#endif /* VIDEO_DETECT_H */
