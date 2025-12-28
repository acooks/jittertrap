#ifndef JT_MSG_TOPTALK_H
#define JT_MSG_TOPTALK_H

int jt_toptalk_packer(void *data, char **out);
int jt_toptalk_unpacker(json_t *root, void **data);
int jt_toptalk_printer(void *data, char *out, int len);
int jt_toptalk_free(void *data);
const char *jt_toptalk_test_msg_get(void);

/* MAX_FLOWS should be 2x the number of displayed top N flows. */
#define MAX_FLOWS 40
#define ADDR_LEN 50
#define PROTO_LEN 6
#define TCLASS_LEN 5

/* TCP connection states (matches tcp_rtt.h enum tcp_conn_state) */
#define TCP_CONN_STATE_UNKNOWN   0
#define TCP_CONN_STATE_SYN_SEEN  1
#define TCP_CONN_STATE_ACTIVE    2
#define TCP_CONN_STATE_FIN_WAIT  3
#define TCP_CONN_STATE_CLOSED    4

/* Video stream types (matches flow.h enum video_stream_type) */
#define JT_VIDEO_TYPE_NONE    0
#define JT_VIDEO_TYPE_RTP     1
#define JT_VIDEO_TYPE_MPEG_TS 2

/* Video codecs (matches flow.h enum video_codec) */
#define JT_VIDEO_CODEC_UNKNOWN 0
#define JT_VIDEO_CODEC_H264    1
#define JT_VIDEO_CODEC_H265    2
#define JT_VIDEO_CODEC_VP8     3
#define JT_VIDEO_CODEC_VP9     4
#define JT_VIDEO_CODEC_AV1     5

/* Codec parameter source (matches flow.h enum codec_param_source) */
#define JT_CODEC_SRC_UNKNOWN 0
#define JT_CODEC_SRC_INBAND  1
#define JT_CODEC_SRC_SDP     2

/* Audio codecs (matches flow.h enum audio_codec) */
#define JT_AUDIO_CODEC_UNKNOWN 0
#define JT_AUDIO_CODEC_PCMU    1   /* G.711 μ-law (PT 0) */
#define JT_AUDIO_CODEC_PCMA    2   /* G.711 A-law (PT 8) */
#define JT_AUDIO_CODEC_G729    3   /* G.729 (PT 18) */
#define JT_AUDIO_CODEC_OPUS    4   /* Opus (dynamic PT) */
#define JT_AUDIO_CODEC_AAC     5   /* AAC (dynamic PT) */

/* Audio stream type indicator */
#define JT_AUDIO_TYPE_NONE 0
#define JT_AUDIO_TYPE_RTP  1

/* TCP health status values (matches flow.h) */
#define JT_TCP_HEALTH_UNKNOWN  0   /* Not enough data yet */
#define JT_TCP_HEALTH_GOOD     1   /* No issues detected */
#define JT_TCP_HEALTH_WARNING  2   /* Minor issues or elevated metrics */
#define JT_TCP_HEALTH_PROBLEM  3   /* Significant issues detected */

/* TCP health issue flags (matches flow.h) */
#define JT_TCP_HEALTH_FLAG_HIGH_TAIL_LATENCY   0x01  /* p99/p50 > 5x */
#define JT_TCP_HEALTH_FLAG_ELEVATED_LOSS       0x02  /* Retransmit > 0.5% */
#define JT_TCP_HEALTH_FLAG_HIGH_LOSS           0x04  /* Retransmit > 2% */
#define JT_TCP_HEALTH_FLAG_WINDOW_STARVATION   0x08  /* Zero window events */
#define JT_TCP_HEALTH_FLAG_RTO_STALLS          0x10  /* Gaps > 500ms */

struct jt_msg_toptalk
{
	struct timespec timestamp;
	uint64_t interval_ns;
	uint32_t tflows;
	int64_t tbytes;
	int64_t tpackets;
	struct {
		int64_t bytes;
		int64_t packets;
		int64_t rtt_us;          /* RTT in microseconds, -1 if not available */
		int32_t tcp_state;       /* TCP connection state, -1 if not TCP */
		int32_t saw_syn;         /* 1 if SYN was observed, 0 otherwise */
		/* Window/Congestion tracking fields */
		int64_t rwnd_bytes;      /* Advertised window in bytes, -1 if unknown */
		int32_t window_scale;    /* Window scale factor, -1 if unknown */
		uint32_t zero_window_cnt; /* Zero-window events */
		uint32_t dup_ack_cnt;    /* Duplicate ACK bursts (3+) */
		uint32_t retransmit_cnt; /* Detected retransmissions */
		uint32_t ece_cnt;        /* ECE flag count */
		uint64_t recent_events;  /* Bitmask of recent congestion events */
		/* TCP health indicator fields */
		uint32_t health_rtt_hist[14]; /* RTT histogram buckets (log scale) */
		uint32_t health_rtt_samples;  /* Total number of RTT samples */
		uint8_t health_status;        /* JT_TCP_HEALTH_* status */
		uint8_t health_flags;         /* JT_TCP_HEALTH_FLAG_* bitmask */
		/* Inter-packet gap (IPG) histogram - for all flows */
		uint32_t ipg_hist[12];        /* IPG histogram buckets (log scale) */
		uint32_t ipg_samples;         /* Total IPG samples */
		int64_t ipg_mean_us;          /* Mean IPG in microseconds */
		/* Video stream tracking fields */
		uint8_t video_type;      /* VIDEO_TYPE_NONE/RTP/MPEG_TS */
		uint8_t video_codec;     /* VIDEO_CODEC_* enum */
		int64_t video_jitter_us; /* RTP jitter in microseconds */
		uint32_t video_jitter_hist[12]; /* Jitter histogram buckets (log scale) */
		uint32_t video_seq_loss; /* RTP sequence loss count */
		uint32_t video_cc_errors; /* MPEG-TS continuity counter errors */
		uint32_t video_ssrc;     /* RTP SSRC identifier */
		/* Extended video telemetry */
		uint8_t video_codec_source;  /* JT_CODEC_SRC_* */
		uint16_t video_width;        /* Video width in pixels */
		uint16_t video_height;       /* Video height in pixels */
		uint8_t video_profile;       /* H.264/H.265 profile_idc */
		uint8_t video_level;         /* H.264/H.265 level_idc */
		uint16_t video_fps_x100;     /* FPS * 100 (2500 = 25.00 fps) */
		uint32_t video_bitrate_kbps; /* Current bitrate estimate */
		uint16_t video_gop_frames;   /* Frames between keyframes */
		uint32_t video_keyframes;    /* Total keyframes seen */
		uint32_t video_frames;       /* Total frames seen */
		/* Audio stream tracking fields */
		uint8_t audio_type;          /* JT_AUDIO_TYPE_NONE/RTP */
		uint8_t audio_codec;         /* JT_AUDIO_CODEC_* enum */
		uint8_t audio_sample_rate;   /* Sample rate in kHz (8, 16, 48) */
		int64_t audio_jitter_us;     /* RTP jitter in microseconds */
		uint32_t audio_seq_loss;     /* RTP sequence loss count */
		uint32_t audio_ssrc;         /* RTP SSRC identifier */
		uint32_t audio_bitrate_kbps; /* Audio stream bitrate in kbps */
		/* Packet size histogram fields (20 buckets) */
		uint32_t frame_size_hist[20];
		uint32_t frame_size_samples;
		uint32_t frame_size_mean;    /* Mean frame size in bytes */
		uint32_t frame_size_variance; /* Variance in bytes^2 */
		uint32_t frame_size_min;
		uint32_t frame_size_max;
		/* Packets per second histogram fields (12 buckets) */
		uint32_t pps_hist[12];
		uint32_t pps_samples;
		uint32_t pps_mean;           /* Mean PPS */
		uint32_t pps_variance;       /* PPS variance */
		uint16_t sport;
		uint16_t dport;
		char src[ADDR_LEN];
		char dst[ADDR_LEN];
		char proto[PROTO_LEN];
		char tclass[TCLASS_LEN];
	} flows[MAX_FLOWS];
};

#endif
