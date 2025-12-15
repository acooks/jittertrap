#ifndef FLOW_H
#define FLOW_H

#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <netinet/in.h>

/* Video stream type detection */
enum video_stream_type {
	VIDEO_STREAM_NONE = 0,
	VIDEO_STREAM_RTP = 1,
	VIDEO_STREAM_MPEG_TS = 2
};

/* Video codec identification */
enum video_codec {
	VIDEO_CODEC_UNKNOWN = 0,
	VIDEO_CODEC_H264 = 1,
	VIDEO_CODEC_H265 = 2,
	VIDEO_CODEC_VP8 = 3,
	VIDEO_CODEC_VP9 = 4,
	VIDEO_CODEC_AV1 = 5
};

/* Codec source identification */
enum codec_source {
	CODEC_SRC_UNKNOWN = 0,
	CODEC_SRC_INBAND = 1,       /* Detected from RTP payload (SPS/NAL) */
	CODEC_SRC_SDP = 2           /* Obtained from RTSP/SDP signaling */
};

/* Audio codec identification (RFC 3551 static payload types) */
enum audio_codec {
	AUDIO_CODEC_UNKNOWN = 0,
	AUDIO_CODEC_PCMU = 1,       /* G.711 μ-law (PT 0) */
	AUDIO_CODEC_PCMA = 2,       /* G.711 A-law (PT 8) */
	AUDIO_CODEC_G729 = 3,       /* G.729 (PT 18) */
	AUDIO_CODEC_OPUS = 4,       /* Opus (dynamic PT) */
	AUDIO_CODEC_AAC = 5         /* AAC (dynamic PT) */
};

/* RTP stream information - populated during packet processing
 * Struct members ordered for optimal 64-bit alignment:
 * - 64-bit fields first, then 32-bit, then 16-bit, then 8-bit
 */
/* Jitter histogram bucket count - must match video_metrics.c */
#define JITTER_HIST_BUCKETS 12

struct rtp_info {
	/* 64-bit aligned fields (8 bytes each) */
	int64_t jitter_us;          /* RFC 3550 jitter estimate in microseconds */
	uint64_t first_ts;          /* First RTP timestamp seen */
	uint64_t last_ts;           /* Last RTP timestamp seen */

	/* 32-bit aligned fields (4 bytes each) */
	uint32_t ssrc;              /* Synchronization source identifier */
	uint32_t rtp_timestamp;     /* Current packet's RTP timestamp */
	uint32_t seq_loss;          /* Count of sequence discontinuities */
	uint32_t packets_seen;      /* Total RTP packets observed */
	uint32_t frame_count;       /* Total video frames detected */
	uint32_t keyframe_count;    /* I-frame/IDR count */
	uint32_t gop_frames;        /* Frames since last keyframe (GOP size) */
	uint32_t bitrate_kbps;      /* Current bitrate estimate */

	/* Jitter histogram (log scale buckets):
	 * 0: <10us, 1: 10-50us, 2: 50-100us, 3: 100-500us,
	 * 4: 0.5-1ms, 5: 1-2ms, 6: 2-5ms, 7: 5-10ms,
	 * 8: 10-20ms, 9: 20-50ms, 10: 50-100ms, 11: >100ms
	 */
	uint32_t jitter_hist[JITTER_HIST_BUCKETS];

	/* 16-bit aligned fields (2 bytes each) */
	uint16_t last_seq;          /* Last sequence number seen */
	uint16_t width;             /* Video width from SPS */
	uint16_t height;            /* Video height from SPS */
	uint16_t fps_x100;          /* FPS * 100 (e.g., 2997 = 29.97fps) */
	uint16_t payload_len;       /* RTP payload length (excluding header) */

	/* 8-bit fields (1 byte each, grouped for alignment) */
	uint8_t payload_type;       /* RTP payload type (0-127) */
	uint8_t codec;              /* Detected video codec (enum video_codec) */
	uint8_t codec_source;       /* enum codec_source */
	uint8_t profile_idc;        /* H.264/H.265 profile */
	uint8_t level_idc;          /* H.264/H.265 level */

	/* Audio-specific fields */
	uint8_t audio_codec;        /* Detected audio codec (enum audio_codec) */
	uint8_t sample_rate_khz;    /* Audio sample rate in kHz (8, 16, 32, 48) */
	uint8_t channels;           /* Audio channels (1=mono, 2=stereo) */
};

/* MPEG-TS stream information - output only, state tracked in video_metrics.c */
struct mpegts_info {
	uint32_t cc_errors;         /* Continuity counter error count */
	uint16_t video_pid;         /* Primary video PID */
	uint8_t codec;              /* Detected video codec (enum video_codec) */
	uint32_t packets_seen;      /* Total TS packets observed */
};

/* Combined video stream info for flow records */
struct flow_video_info {
	uint8_t stream_type;        /* enum video_stream_type */
	union {
		struct rtp_info rtp;
		struct mpegts_info mpegts;
	};
};

struct flow {
	uint16_t ethertype;
	union {
		struct {
			struct in_addr src_ip;
			struct in_addr dst_ip;
		};
		struct {
			struct in6_addr src_ip6;
			struct in6_addr dst_ip6;
		};
	};
	uint16_t sport;
	uint16_t dport;
	uint16_t proto;
	uint8_t tclass;
};

/*
 * Compare two flows for equality, ignoring padding bytes.
 * Returns 0 if flows are equal, non-zero otherwise.
 * This avoids issues with memcmp comparing uninitialized padding.
 */
static inline int flow_cmp(const struct flow *a, const struct flow *b)
{
	if (a->ethertype != b->ethertype)
		return 1;
	if (a->sport != b->sport)
		return 1;
	if (a->dport != b->dport)
		return 1;
	if (a->proto != b->proto)
		return 1;
	if (a->tclass != b->tclass)
		return 1;

	/* Compare IP addresses based on ethertype */
	if (a->ethertype == 0x0800) {
		/* IPv4: compare only the 4-byte addresses */
		if (a->src_ip.s_addr != b->src_ip.s_addr)
			return 1;
		if (a->dst_ip.s_addr != b->dst_ip.s_addr)
			return 1;
	} else {
		/* IPv6: compare full 16-byte addresses */
		if (memcmp(&a->src_ip6, &b->src_ip6, sizeof(struct in6_addr)) != 0)
			return 1;
		if (memcmp(&a->dst_ip6, &b->dst_ip6, sizeof(struct in6_addr)) != 0)
			return 1;
	}

	return 0;  /* Equal */
}

/* Cached TCP RTT info - populated by tt_get_top5() for thread-safe access */
struct flow_rtt_info {
	int64_t rtt_us;           /* RTT in microseconds, -1 if unknown */
	int32_t tcp_state;        /* TCP connection state, -1 if unknown */
	int32_t saw_syn;          /* 1 if SYN was observed, 0 otherwise */
};

/* Cached TCP window/congestion info - populated by tt_get_top5() */
struct flow_window_info {
	int64_t rwnd_bytes;       /* Receive window in bytes, -1 if unknown */
	int32_t window_scale;     /* Window scale factor, -1 if unknown */
	uint32_t zero_window_cnt; /* Zero window events */
	uint32_t dup_ack_cnt;     /* Duplicate ACK events */
	uint32_t retransmit_cnt;  /* Retransmission events */
	uint32_t ece_cnt;         /* ECE flag count */
	uint8_t recent_events;    /* Bitmask of recent congestion events */
};

/* Inter-packet gap (IPG) histogram bucket count */
#define IPG_HIST_BUCKETS 12

/* Cached IPG info - populated by tt_get_top5() for all flows */
struct flow_ipg_info {
	/* IPG histogram (log scale buckets):
	 * 0: <10us, 1: 10-50us, 2: 50-100us, 3: 100-500us,
	 * 4: 0.5-1ms, 5: 1-2ms, 6: 2-5ms, 7: 5-10ms,
	 * 8: 10-20ms, 9: 20-50ms, 10: 50-100ms, 11: >100ms
	 */
	uint32_t ipg_hist[IPG_HIST_BUCKETS];
	uint32_t ipg_samples;         /* Total IPG samples (packets - 1) */
	int64_t ipg_mean_us;          /* Mean IPG in microseconds */
};

/* Packet size histogram bucket count */
#define PKT_SIZE_HIST_BUCKETS 20

/* Packets per second histogram bucket count */
#define PPS_HIST_BUCKETS 12

/* Cached packet size info - populated by tt_get_top5() for all flows */
struct flow_pkt_size_info {
	/* Frame size histogram (20 buckets):
	 * Designed to capture VoIP, MPEG-TS video, and tunnel MTU ceilings
	 *
	 * Small packets (VoIP focus):
	 *   0: <64B (undersized)
	 *   1: 64-100B (minimum ACKs)
	 *   2: 100-160B (small, G.729 VoIP)
	 *   3: 160-220B (VoIP G.711 20ms ~200B)
	 *   4: 220-300B (VoIP G.711 30ms ~280B)
	 *
	 * Medium packets (MPEG-TS focus):
	 *   5: 300-400B (MPEG-TS 2x = 376B)
	 *   6: 400-576B (MPEG-TS 3x = 564B)
	 *   7: 576-760B (MPEG-TS 4x = 752B)
	 *   8: 760-950B (MPEG-TS 5x = 940B)
	 *   9: 950-1140B (MPEG-TS 6x = 1128B)
	 *  10: 1140-1320B (MPEG-TS 7x = 1316B)
	 *
	 * Near MTU (tunnel ceiling focus):
	 *  11: 1320-1400B (pre-tunnel ceiling)
	 *  12: 1400-1430B (WireGuard ~1420B)
	 *  13: 1430-1460B (VXLAN ~1450B)
	 *  14: 1460-1480B (GRE ~1472B)
	 *  15: 1480-1492B (MPLS ~1488B)
	 *
	 * Full MTU:
	 *  16: 1492-1500B (near MTU)
	 *  17: 1500-1518B (standard MTU frame)
	 *
	 * Oversized:
	 *  18: 1518-2000B (VLAN/small jumbo)
	 *  19: >=2000B (jumbo frames)
	 */
	uint32_t frame_hist[PKT_SIZE_HIST_BUCKETS];
	uint32_t frame_samples;
	uint64_t frame_sum;           /* For mean calculation */
	uint64_t frame_sum_sq;        /* For variance: sum of squares */
	uint32_t frame_min;
	uint32_t frame_max;

	/* Payload size histogram (same buckets as frame) */
	uint32_t payload_hist[PKT_SIZE_HIST_BUCKETS];
	uint32_t payload_samples;
	uint64_t payload_sum;
	uint64_t payload_sum_sq;
	uint32_t payload_min;
	uint32_t payload_max;
};

/* Cached packets per second info - populated by tt_get_top5() */
struct flow_pps_info {
	/* PPS histogram (log scale buckets):
	 * 0: <10, 1: 10-50, 2: 50-100, 3: 100-500,
	 * 4: 500-1K, 5: 1K-2K, 6: 2K-5K, 7: 5K-10K,
	 * 8: 10K-20K, 9: 20K-50K, 10: 50K-100K, 11: >100K
	 */
	uint32_t pps_hist[PPS_HIST_BUCKETS];
	uint32_t pps_samples;         /* Number of intervals measured */
	uint64_t pps_sum;             /* For mean calculation */
	uint64_t pps_sum_sq;          /* For variance */
};

/* TCP health status values */
#define TCP_HEALTH_UNKNOWN  0   /* Not enough data yet */
#define TCP_HEALTH_GOOD     1   /* No issues detected */
#define TCP_HEALTH_WARNING  2   /* Minor issues or elevated metrics */
#define TCP_HEALTH_PROBLEM  3   /* Significant issues detected */

/* TCP health issue flags */
#define TCP_HEALTH_FLAG_HIGH_TAIL_LATENCY   0x01  /* p99/p50 > 5x */
#define TCP_HEALTH_FLAG_ELEVATED_LOSS       0x02  /* Retransmit > 0.5% */
#define TCP_HEALTH_FLAG_HIGH_LOSS           0x04  /* Retransmit > 2% */
#define TCP_HEALTH_FLAG_WINDOW_STARVATION   0x08  /* Zero window events */
#define TCP_HEALTH_FLAG_RTO_STALLS          0x10  /* Gaps > 500ms */

/* Cached TCP health info - populated by tt_get_top5() */
struct flow_health_info {
	/* RTT histogram (14 log-scale buckets):
	 * 0: 0-99us, 1: 100-199us, 2: 200-499us, 3: 500-999us,
	 * 4: 1-2ms, 5: 2-5ms, 6: 5-10ms, 7: 10-20ms, 8: 20-50ms,
	 * 9: 50-100ms, 10: 100-200ms, 11: 200-500ms, 12: 500ms-1s, 13: >1s
	 */
	uint32_t rtt_hist[14];
	uint32_t rtt_samples;

	/* Overall health */
	uint8_t health_status;    /* TCP_HEALTH_* enum */
	uint8_t health_flags;     /* Bitmask of TCP_HEALTH_FLAG_* */
};

struct flow_record {
	struct flow flow;
	int64_t bytes;
	int64_t packets;
	/* Cached TCP info - populated by writer thread for thread-safe reader access */
	struct flow_rtt_info rtt;
	struct flow_window_info window;
	struct flow_health_info health;
	/* Inter-packet gap info - populated for all flows */
	struct flow_ipg_info ipg;
	/* Packet size info - populated for all flows */
	struct flow_pkt_size_info pkt_size;
	/* Packets per second info - populated for all flows */
	struct flow_pps_info pps;
	/* Video stream info - populated for RTP/MPEG-TS UDP flows */
	struct flow_video_info video;
};

struct flow_pkt {
	struct flow_record flow_rec;
	struct timeval timestamp;
};

#endif
