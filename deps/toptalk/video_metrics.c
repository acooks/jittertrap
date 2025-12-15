/*
 * video_metrics.c - Video stream quality metrics tracking
 *
 * Implements RFC 3550 jitter calculation for RTP and
 * continuity counter error tracking for MPEG-TS.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "uthash.h"
#include "flow.h"
#include "video_detect.h"
#include "video_metrics.h"
#include "timeywimey.h"

/*
 * Composite key for RTP stream tracking: flow + SSRC
 * This allows tracking multiple RTP streams (e.g., main and sub streams)
 * that share the same 5-tuple but have different SSRCs.
 */
struct rtp_stream_key {
	struct flow flow;                /* 5-tuple flow key */
	uint32_t ssrc;                   /* RTP SSRC */
};

/*
 * RTP stream tracking entry
 */
/* Jitter histogram bucket count - 12 buckets from <10us to >100ms */
#define JITTER_HIST_BUCKETS 12

struct rtp_stream_entry {
	struct rtp_stream_key key;       /* Composite key: flow + SSRC */
	uint16_t last_seq;               /* Last sequence number */
	uint32_t last_timestamp;         /* Last RTP timestamp */
	uint32_t first_timestamp;        /* First RTP timestamp seen */
	struct timeval last_arrival;     /* Last packet arrival time */
	struct timeval first_arrival;    /* First packet arrival time */
	int64_t jitter;                  /* Jitter estimate (scaled by 16) */
	uint32_t packets_received;       /* Total packets received */
	uint32_t packets_lost;           /* Estimated lost packets */
	uint32_t seq_discontinuities;    /* Sequence discontinuities */
	uint8_t codec;                   /* Detected codec */
	uint8_t audio_codec;             /* Detected audio codec */
	uint8_t payload_type;            /* RTP payload type */
	uint8_t is_audio_stream;         /* True if audio codec detected */
	int initialized;                 /* Have we seen first packet? */
	uint32_t clock_rate_hz;          /* RTP clock rate: 90000 for video, 8000/48000 for audio */

	/* Extended video telemetry */
	uint8_t codec_source;            /* CODEC_SRC_* */
	uint16_t width;                  /* Video width (from SPS/SDP) */
	uint16_t height;                 /* Video height (from SPS/SDP) */
	uint8_t profile_idc;             /* H.264/H.265 profile */
	uint8_t level_idc;               /* H.264/H.265 level */
	uint32_t frame_count;            /* Total frames seen */
	uint32_t keyframe_count;         /* Total keyframes seen */
	uint32_t last_keyframe_num;      /* Frame number of last keyframe */
	uint32_t prev_frame_ts;          /* Previous frame's RTP timestamp */
	int prev_frame_ts_valid;         /* Have we seen a frame timestamp? */
	uint16_t gop_frames;             /* Last measured GOP size */
	uint64_t bytes_received;         /* Total bytes for bitrate calc */
	struct timeval bitrate_window_start; /* Start of bitrate window */
	uint64_t bitrate_window_bytes;   /* Bytes in current window */

	/* 1-second windowed metrics */
	struct timeval window_start;     /* Start of current 1-second window */
	uint32_t window_frames;          /* Frames in current window */
	uint64_t window_bytes;           /* Bytes in current window */
	int64_t window_jitter_sum;       /* Sum of jitter samples in window (scaled) */
	uint32_t window_jitter_count;    /* Number of jitter samples in window */

	/* Jitter histogram (log scale buckets):
	 * 0: <10us, 1: 10-50us, 2: 50-100us, 3: 100-500us,
	 * 4: 0.5-1ms, 5: 1-2ms, 6: 2-5ms, 7: 5-10ms,
	 * 8: 10-20ms, 9: 20-50ms, 10: 50-100ms, 11: >100ms
	 */
	uint32_t jitter_hist[JITTER_HIST_BUCKETS];

	/* Last calculated windowed values */
	uint16_t fps_x100;               /* Windowed FPS * 100 */
	uint32_t bitrate_kbps;           /* Windowed bitrate in kbps */
	int64_t jitter_us;               /* Windowed average jitter in microseconds */

	UT_hash_handle hh;
};

/*
 * MPEG-TS stream tracking entry
 */
struct mpegts_stream_entry {
	struct flow flow;                /* Flow key */
	uint8_t cc[8192];                /* Per-PID continuity counters */
	uint8_t cc_valid[8192];          /* Per-PID: have we seen this PID? */
	uint32_t cc_errors;              /* Total CC errors */
	uint32_t packets_received;       /* Total TS packets received */
	uint16_t video_pid;              /* Primary video PID (0 if unknown) */
	uint8_t codec;                   /* Detected codec */
	struct timeval last_arrival;     /* Last packet arrival time */
	UT_hash_handle hh;
};

/* Hash tables for video stream tracking */
static struct rtp_stream_entry *rtp_streams = NULL;
static struct mpegts_stream_entry *mpegts_streams = NULL;

/* Forward declarations */
static void populate_rtp_video_info(struct rtp_stream_entry *rtp,
                                    struct flow_video_info *video_info);

void video_metrics_init(void)
{
	rtp_streams = NULL;
	mpegts_streams = NULL;
}

void video_metrics_cleanup(void)
{
	struct rtp_stream_entry *rtp, *rtp_tmp;
	struct mpegts_stream_entry *ts, *ts_tmp;

	HASH_ITER(hh, rtp_streams, rtp, rtp_tmp) {
		HASH_DEL(rtp_streams, rtp);
		free(rtp);
	}

	HASH_ITER(hh, mpegts_streams, ts, ts_tmp) {
		HASH_DEL(mpegts_streams, ts);
		free(ts);
	}
}

/* Convert jitter in microseconds to histogram bucket index.
 * Log-scale buckets for better resolution at low jitter:
 * 0: <10us, 1: 10-50us, 2: 50-100us, 3: 100-500us,
 * 4: 0.5-1ms, 5: 1-2ms, 6: 2-5ms, 7: 5-10ms,
 * 8: 10-20ms, 9: 20-50ms, 10: 50-100ms, 11: >100ms
 */
static int jitter_to_bucket(int64_t jitter_us)
{
	if (jitter_us < 10) return 0;
	if (jitter_us < 50) return 1;
	if (jitter_us < 100) return 2;
	if (jitter_us < 500) return 3;
	if (jitter_us < 1000) return 4;
	if (jitter_us < 2000) return 5;
	if (jitter_us < 5000) return 6;
	if (jitter_us < 10000) return 7;
	if (jitter_us < 20000) return 8;
	if (jitter_us < 50000) return 9;
	if (jitter_us < 100000) return 10;
	return 11; /* > 100ms */
}

/*
 * RFC 3550 Interarrival Jitter Calculation
 *
 * The jitter is calculated as:
 *   J(i) = J(i-1) + (|D(i-1,i)| - J(i-1)) / 16
 *
 * Where D(i-1,i) is the difference in packet spacing at the receiver
 * compared to the sender:
 *   D(i-1,i) = (Ri - Ri-1) - (Si - Si-1)
 *
 * Ri = arrival time of packet i
 * Si = RTP timestamp of packet i
 *
 * We store jitter scaled by 16 to avoid floating point.
 *
 * clock_rate_hz: RTP clock rate (90000 for video, 8000/48000 for audio)
 */
static void update_rtp_jitter(struct rtp_stream_entry *entry,
                              uint32_t timestamp,
                              struct timeval arrival,
                              uint32_t clock_rate_hz)
{
	if (!entry->initialized) {
		entry->last_timestamp = timestamp;
		entry->last_arrival = arrival;
		entry->initialized = 1;
		return;
	}

	/* Calculate arrival time delta in RTP timestamp units.
	 * Convert from microseconds to RTP timestamp units using actual clock rate.
	 * Formula: arrival_delta_ts = arrival_delta_us * clock_rate_hz / 1000000
	 */
	int64_t arrival_delta_us = (arrival.tv_sec - entry->last_arrival.tv_sec) * 1000000 +
	                           (arrival.tv_usec - entry->last_arrival.tv_usec);
	int64_t arrival_delta_ts = (arrival_delta_us * clock_rate_hz) / 1000000;

	/* RTP timestamp delta (handles wraparound) */
	int64_t ts_delta = (int32_t)(timestamp - entry->last_timestamp);

	/* D = transit time difference */
	int64_t d = arrival_delta_ts - ts_delta;
	if (d < 0) d = -d;  /* Absolute value */

	/* J(i) = J(i-1) + (|D| - J(i-1)) / 16 */
	/* Stored scaled by 16, so: J_scaled = J_scaled + |D| - J_scaled/16 */
	entry->jitter = entry->jitter + d - (entry->jitter >> 4);

	/* Convert jitter to microseconds for windowed average and histogram */
	int64_t d_us = (d * 1000000) / clock_rate_hz;

	/* Accumulate jitter samples for windowed average */
	entry->window_jitter_sum += d_us;
	entry->window_jitter_count++;

	/* Update jitter histogram */
	int bucket = jitter_to_bucket(d_us);
	entry->jitter_hist[bucket]++;

	entry->last_timestamp = timestamp;
	entry->last_arrival = arrival;
}

/*
 * Check for sequence number discontinuity
 * Returns number of packets lost (0 if none)
 */
static uint32_t check_seq_continuity(struct rtp_stream_entry *entry, uint16_t seq)
{
	if (!entry->initialized) {
		return 0;
	}

	/* Expected sequence number (with wraparound) */
	uint16_t expected = entry->last_seq + 1;

	if (seq == expected) {
		return 0;  /* No discontinuity */
	}

	/* Calculate gap (handle wraparound) */
	int32_t gap = (int16_t)(seq - expected);

	if (gap > 0 && gap < 1000) {
		/* Likely packet loss */
		return gap;
	} else if (gap < 0 && gap > -100) {
		/* Late/reordered packet - not loss */
		return 0;
	}

	/* Large gap - could be reset or severe loss, count as 1 discontinuity */
	return 1;
}

int video_metrics_rtp_process(const struct flow *flow,
                              const struct rtp_info *rtp_info,
                              struct timeval timestamp)
{
	if (!flow || !rtp_info)
		return -1;

	struct rtp_stream_entry *entry;

	/* Build composite key: flow + SSRC
	 * Zero-initialize to ensure padding bytes are zeroed for memcmp */
	struct rtp_stream_key lookup_key = { 0 };
	memcpy(&lookup_key.flow, flow, sizeof(struct flow));
	lookup_key.ssrc = rtp_info->ssrc;

	/* Find or create entry for this flow+SSRC combination */
	HASH_FIND(hh, rtp_streams, &lookup_key, sizeof(struct rtp_stream_key), entry);

	if (!entry) {
		entry = calloc(1, sizeof(struct rtp_stream_entry));
		if (!entry)
			return -1;

		memcpy(&entry->key, &lookup_key, sizeof(struct rtp_stream_key));
		entry->payload_type = rtp_info->payload_type;
		entry->codec = rtp_info->codec;
		entry->audio_codec = rtp_info->audio_codec;
		entry->initialized = 0;

		/* Set clock rate based on stream type for correct jitter calculation */
		if (rtp_info->audio_codec != AUDIO_CODEC_UNKNOWN) {
			entry->clock_rate_hz = audio_clock_rate_hz(rtp_info->audio_codec);
			entry->is_audio_stream = 1;
		} else {
			entry->clock_rate_hz = 90000;  /* Standard video clock rate */
			entry->is_audio_stream = 0;
		}

		HASH_ADD(hh, rtp_streams, key, sizeof(struct rtp_stream_key), entry);

	}

	/* No longer need SSRC change detection - each SSRC gets its own entry */

	/* Update codec only if we don't have one yet.
	 * Once a codec is detected, lock it in - different NAL types within
	 * the same stream can be ambiguous (e.g., some H.265 packets may
	 * look like H.264 depending on NAL content), causing flickering. */
	if (entry->codec == VIDEO_CODEC_UNKNOWN &&
	    rtp_info->codec != VIDEO_CODEC_UNKNOWN) {
		entry->codec = rtp_info->codec;
	}

	/* Update audio codec if detected, and set audio stream flags */
	if (entry->audio_codec == 0 && rtp_info->audio_codec != 0) {
		entry->audio_codec = rtp_info->audio_codec;
		/* Also update clock rate and audio stream flag if not already set */
		if (!entry->is_audio_stream) {
			entry->clock_rate_hz = audio_clock_rate_hz(rtp_info->audio_codec);
			entry->is_audio_stream = 1;
		}
	}

	/* Copy codec params if available (from SPS parsing in video_detect) */
	if (rtp_info->profile_idc != 0 && entry->profile_idc == 0) {
		entry->profile_idc = rtp_info->profile_idc;
		entry->level_idc = rtp_info->level_idc;
		entry->codec_source = rtp_info->codec_source;
	}

	/* Copy resolution if available */
	if (rtp_info->width != 0 && entry->width == 0) {
		entry->width = rtp_info->width;
		entry->height = rtp_info->height;
	}

	/* Check sequence continuity */
	uint32_t lost = check_seq_continuity(entry, rtp_info->last_seq);
	if (lost > 0) {
		entry->packets_lost += lost;
		entry->seq_discontinuities++;
	}

	/* Update jitter estimate using actual RTP timestamp and clock rate */
	update_rtp_jitter(entry, rtp_info->rtp_timestamp, timestamp, entry->clock_rate_hz);

	entry->last_seq = rtp_info->last_seq;
	entry->packets_received++;
	entry->last_arrival = timestamp;

	/* Track first arrival time for this stream */
	if (entry->packets_received == 1) {
		entry->first_arrival = timestamp;
		entry->bitrate_window_start = timestamp;
		entry->window_start = timestamp;
	}

	/* For audio streams, track bytes and calculate windowed metrics here.
	 * Video streams do this in video_metrics_update_frame() instead. */
	if (entry->is_audio_stream && rtp_info->payload_len > 0) {
		entry->bytes_received += rtp_info->payload_len;
		entry->window_bytes += rtp_info->payload_len;
		entry->bitrate_window_bytes += rtp_info->payload_len;

		/* Calculate windowed metrics every second */
		int64_t window_us = (timestamp.tv_sec - entry->window_start.tv_sec) * 1000000 +
		                    (timestamp.tv_usec - entry->window_start.tv_usec);

		if (window_us >= 1000000) {
			/* Bitrate: bytes * 8 * 1000000 / window_us / 1000 = bytes * 8000 / window_us (kbps) */
			if (window_us > 0) {
				entry->bitrate_kbps = (uint32_t)((entry->window_bytes * 8000) / window_us);
			}

			/* Jitter: average of samples in window (already in microseconds) */
			if (entry->window_jitter_count > 0) {
				entry->jitter_us = entry->window_jitter_sum / entry->window_jitter_count;
			}

			/* Reset window accumulators */
			entry->window_start = timestamp;
			entry->window_bytes = 0;
			entry->window_jitter_sum = 0;
			entry->window_jitter_count = 0;
			entry->bitrate_window_bytes = 0;
		}
	}

	return 0;
}

/*
 * MPEG-TS Continuity Counter check
 * CC is a 4-bit counter (0-15) that increments with each packet per PID
 */
static int check_mpegts_cc(struct mpegts_stream_entry *entry,
                           uint16_t pid, uint8_t cc)
{
	if (pid >= 8192)
		return 0;  /* Invalid PID */

	if (!entry->cc_valid[pid]) {
		/* First packet for this PID */
		entry->cc[pid] = cc;
		entry->cc_valid[pid] = 1;
		return 0;
	}

	uint8_t expected = (entry->cc[pid] + 1) & 0x0F;
	entry->cc[pid] = cc;

	if (cc != expected) {
		/* Discontinuity detected */
		return 1;
	}

	return 0;
}

int video_metrics_mpegts_process(const struct flow *flow,
                                 struct mpegts_info *ts_info,
                                 const uint8_t *payload,
                                 size_t payload_len)
{
	if (!flow || !ts_info || !payload)
		return -1;

	struct mpegts_stream_entry *entry;

	/* Find or create entry for this flow */
	HASH_FIND(hh, mpegts_streams, flow, sizeof(struct flow), entry);

	if (!entry) {
		entry = calloc(1, sizeof(struct mpegts_stream_entry));
		if (!entry)
			return -1;

		memcpy(&entry->flow, flow, sizeof(struct flow));
		entry->video_pid = ts_info->video_pid;
		entry->codec = ts_info->codec;

		HASH_ADD(hh, mpegts_streams, flow, sizeof(struct flow), entry);
	}

	/* Update video PID and codec if detected */
	if (ts_info->video_pid != 0) {
		entry->video_pid = ts_info->video_pid;
	}
	if (ts_info->codec != VIDEO_CODEC_UNKNOWN) {
		entry->codec = ts_info->codec;
	}

	/* Process each TS packet in the payload */
	int ts_packets = payload_len / 188;
	for (int i = 0; i < ts_packets; i++) {
		const uint8_t *pkt = payload + i * 188;

		if (pkt[0] != 0x47) {
			continue;  /* Invalid sync byte */
		}

		/* Extract PID and CC */
		uint16_t pid = ((pkt[1] & 0x1F) << 8) | pkt[2];
		uint8_t afc = (pkt[3] >> 4) & 0x03;
		uint8_t cc = pkt[3] & 0x0F;

		/* Skip null packets and packets without payload */
		if (pid == 0x1FFF || afc == 0 || afc == 2) {
			continue;
		}

		/* Check continuity counter */
		if (check_mpegts_cc(entry, pid, cc)) {
			entry->cc_errors++;
		}

		entry->packets_received++;
	}

	/* Update ts_info with accumulated metrics */
	ts_info->cc_errors = entry->cc_errors;
	ts_info->packets_seen = entry->packets_received;

	struct timeval now;
	gettimeofday(&now, NULL);
	entry->last_arrival = now;

	return 0;
}

int video_metrics_get(const struct flow *flow, struct flow_video_info *video_info)
{
	if (!flow || !video_info)
		return -1;

	/* Debug: show lookup request */
	char lookup_src[INET6_ADDRSTRLEN], lookup_dst[INET6_ADDRSTRLEN];
	if (flow->ethertype == 0x0800) {
		inet_ntop(AF_INET, &flow->src_ip, lookup_src, sizeof(lookup_src));
		inet_ntop(AF_INET, &flow->dst_ip, lookup_dst, sizeof(lookup_dst));
	} else {
		inet_ntop(AF_INET6, &flow->src_ip6, lookup_src, sizeof(lookup_src));
		inet_ntop(AF_INET6, &flow->dst_ip6, lookup_dst, sizeof(lookup_dst));
	}

	/* Try RTP first - find the entry with the highest bitrate for this flow.
	 * Entries are keyed by flow+SSRC, so multiple SSRCs may exist on same
	 * 5-tuple (RTP multiplexing). We prefer the stream with higher bitrate
	 * since that's typically the "main" stream vs "sub" stream.
	 *
	 * Note: Use flow_cmp() instead of memcmp() to avoid comparing padding
	 * bytes in struct flow, which may differ due to uninitialized memory. */
	struct rtp_stream_entry *rtp = NULL, *best = NULL, *tmp;
	int match_count = 0;
	HASH_ITER(hh, rtp_streams, rtp, tmp) {
		if (flow_cmp(&rtp->key.flow, flow) == 0) {
			match_count++;
			if (!best || rtp->bitrate_window_bytes > best->bitrate_window_bytes) {
				best = rtp;
			}
		}
	}
	rtp = best;

	if (rtp) {  /* best is only set when flow matches */
		populate_rtp_video_info(rtp, video_info);
		return 0;
	}

	/* Try MPEG-TS */
	struct mpegts_stream_entry *ts;
	HASH_FIND(hh, mpegts_streams, flow, sizeof(struct flow), ts);

	if (ts) {
		video_info->stream_type = VIDEO_STREAM_MPEG_TS;
		video_info->mpegts.cc_errors = ts->cc_errors;
		video_info->mpegts.video_pid = ts->video_pid;
		video_info->mpegts.codec = ts->codec;
		video_info->mpegts.packets_seen = ts->packets_received;
		return 0;
	}

	return -1;  /* Flow not found */
}

/*
 * Helper function to populate video_info from an RTP stream entry
 */
static void populate_rtp_video_info(struct rtp_stream_entry *rtp,
                                    struct flow_video_info *video_info)
{
	video_info->stream_type = VIDEO_STREAM_RTP;
	video_info->rtp.payload_type = rtp->payload_type;
	video_info->rtp.last_seq = rtp->last_seq;
	video_info->rtp.ssrc = rtp->key.ssrc;
	video_info->rtp.seq_loss = rtp->packets_lost;
	video_info->rtp.codec = rtp->codec;
	video_info->rtp.audio_codec = rtp->audio_codec;
	video_info->rtp.packets_seen = rtp->packets_received;

	/* Extended telemetry fields */
	video_info->rtp.codec_source = rtp->codec_source;
	video_info->rtp.width = rtp->width;
	video_info->rtp.height = rtp->height;
	video_info->rtp.profile_idc = rtp->profile_idc;
	video_info->rtp.level_idc = rtp->level_idc;
	video_info->rtp.frame_count = rtp->frame_count;
	video_info->rtp.keyframe_count = rtp->keyframe_count;
	video_info->rtp.gop_frames = rtp->gop_frames;

	/* Use windowed metrics (1 second average) for responsive display */
	video_info->rtp.fps_x100 = rtp->fps_x100;
	video_info->rtp.bitrate_kbps = rtp->bitrate_kbps;
	video_info->rtp.jitter_us = rtp->jitter_us;

	/* Copy jitter histogram */
	memcpy(video_info->rtp.jitter_hist, rtp->jitter_hist,
	       sizeof(video_info->rtp.jitter_hist));

	/* Copy first/last timestamps for client to calculate duration */
	video_info->rtp.first_ts = rtp->first_timestamp;
	video_info->rtp.last_ts = rtp->last_timestamp;
}

int video_metrics_get_by_ssrc(const struct flow *flow, uint32_t ssrc,
                              struct flow_video_info *video_info)
{
	if (!flow || !video_info)
		return -1;

	/* Build composite key: flow + SSRC
	 * Zero-initialize to ensure padding bytes are zeroed for memcmp */
	struct rtp_stream_key lookup_key = { 0 };
	memcpy(&lookup_key.flow, flow, sizeof(struct flow));
	lookup_key.ssrc = ssrc;

	struct rtp_stream_entry *rtp;
	HASH_FIND(hh, rtp_streams, &lookup_key, sizeof(struct rtp_stream_key), rtp);

	if (rtp) {
		populate_rtp_video_info(rtp, video_info);
		return 0;
	}

	return -1;  /* Flow+SSRC not found */
}

int video_metrics_get_stream_count(const struct flow *flow)
{
	if (!flow)
		return 0;

	int count = 0;
	struct rtp_stream_entry *rtp, *tmp;
	HASH_ITER(hh, rtp_streams, rtp, tmp) {
		if (flow_cmp(&rtp->key.flow, flow) == 0) {
			count++;
		}
	}

	return count;
}

int video_metrics_get_by_index(const struct flow *flow, int index,
                               struct flow_video_info *video_info)
{
	if (!flow || !video_info || index < 0)
		return -1;

	int count = 0;
	struct rtp_stream_entry *rtp, *tmp;
	HASH_ITER(hh, rtp_streams, rtp, tmp) {
		if (flow_cmp(&rtp->key.flow, flow) == 0) {
			if (count == index) {
				populate_rtp_video_info(rtp, video_info);
				return 0;
			}
			count++;
		}
	}

	return -1;  /* Index out of range */
}

int video_metrics_update_codec_params(const struct flow *flow,
                                      uint32_t ssrc,
                                      uint8_t source,
                                      uint16_t width, uint16_t height,
                                      uint8_t profile, uint8_t level)
{
	if (!flow)
		return -1;

	/* Build composite key: flow + SSRC
	 * Zero-initialize to ensure padding bytes are zeroed for memcmp */
	struct rtp_stream_key lookup_key = { 0 };
	memcpy(&lookup_key.flow, flow, sizeof(struct flow));
	lookup_key.ssrc = ssrc;

	struct rtp_stream_entry *rtp;
	HASH_FIND(hh, rtp_streams, &lookup_key, sizeof(struct rtp_stream_key), rtp);

	if (!rtp)
		return -1;

	rtp->codec_source = source;
	rtp->width = width;
	rtp->height = height;
	rtp->profile_idc = profile;
	rtp->level_idc = level;

	return 0;
}

int video_metrics_update_frame(const struct flow *flow,
                               uint32_t ssrc,
                               int is_keyframe,
                               uint32_t rtp_ts,
                               size_t frame_bytes)
{
	if (!flow)
		return -1;

	/* Build composite key: flow + SSRC
	 * Zero-initialize to clear padding bytes in struct flow - memcmp in
	 * HASH_FIND compares all bytes including padding.
	 */
	struct rtp_stream_key lookup_key = { 0 };
	memcpy(&lookup_key.flow, flow, sizeof(struct flow));
	lookup_key.ssrc = ssrc;

	struct rtp_stream_entry *rtp;
	HASH_FIND(hh, rtp_streams, &lookup_key, sizeof(struct rtp_stream_key), rtp);

	if (!rtp)
		return -1;

	/* Detect new frame by RTP timestamp change.
	 * In RTP video, all packets for the same frame share the same timestamp.
	 * Only increment frame_count when timestamp changes (new frame starts).
	 */
	int is_new_frame = 0;
	if (!rtp->prev_frame_ts_valid) {
		/* First packet - this is the first frame */
		is_new_frame = 1;
		rtp->prev_frame_ts_valid = 1;
		rtp->first_timestamp = rtp_ts;
	} else if (rtp_ts != rtp->prev_frame_ts) {
		/* Timestamp changed - new frame */
		is_new_frame = 1;
	}
	rtp->prev_frame_ts = rtp_ts;
	rtp->last_timestamp = rtp_ts;

	if (is_new_frame) {
		rtp->frame_count++;

		/* Track keyframes and GOP size */
		if (is_keyframe) {
			if (rtp->keyframe_count > 0) {
				/* Calculate GOP size from frames since last keyframe */
				rtp->gop_frames = rtp->frame_count - rtp->last_keyframe_num;
			}
			rtp->keyframe_count++;
			rtp->last_keyframe_num = rtp->frame_count;
		}

		/* Accumulate frame in current window */
		rtp->window_frames++;
	}

	/* Track bytes for bitrate calculation */
	rtp->bytes_received += frame_bytes;
	rtp->window_bytes += frame_bytes;

	/* Unified 1-second window for FPS, Bitrate, and Jitter */
	struct timeval now;
	gettimeofday(&now, NULL);

	/* Initialize window on first call */
	if (rtp->window_start.tv_sec == 0 && rtp->window_start.tv_usec == 0) {
		rtp->window_start = now;
		rtp->bitrate_window_start = now;  /* Keep for backward compat */
	}

	int64_t window_us = (now.tv_sec - rtp->window_start.tv_sec) * 1000000 +
	                    (now.tv_usec - rtp->window_start.tv_usec);

	if (window_us >= 1000000) {
		/* Window complete - calculate all metrics and reset */

		/* FPS: frames * 1000000 / window_us, scaled by 100 */
		if (window_us > 0) {
			rtp->fps_x100 = (uint16_t)((uint64_t)rtp->window_frames * 100000000 / window_us);
		}

		/* Bitrate: bytes * 8 * 1000000 / window_us / 1000 = bytes * 8000 / window_us (kbps) */
		if (window_us > 0) {
			rtp->bitrate_kbps = (uint32_t)((rtp->window_bytes * 8000) / window_us);
		}

		/* Jitter: average of samples in window (already in microseconds) */
		if (rtp->window_jitter_count > 0) {
			rtp->jitter_us = rtp->window_jitter_sum / rtp->window_jitter_count;
		}

		/* Reset window accumulators */
		rtp->window_start = now;
		rtp->window_frames = 0;
		rtp->window_bytes = 0;
		rtp->window_jitter_sum = 0;
		rtp->window_jitter_count = 0;

		/* Update legacy bitrate window for backward compat */
		rtp->bitrate_window_start = now;
		rtp->bitrate_window_bytes = 0;
	}

	/* Also track in legacy bitrate window for backward compat */
	rtp->bitrate_window_bytes += frame_bytes;

	return 0;
}

void video_metrics_expire_old(struct timeval deadline, struct timeval max_age)
{
	struct timeval cutoff = tv_absdiff(deadline, max_age);

	/* Expire old RTP entries */
	struct rtp_stream_entry *rtp, *rtp_tmp;
	HASH_ITER(hh, rtp_streams, rtp, rtp_tmp) {
		if (tv_cmp(rtp->last_arrival, cutoff) < 0) {
			HASH_DEL(rtp_streams, rtp);
			free(rtp);
		}
	}

	/* Expire old MPEG-TS entries */
	struct mpegts_stream_entry *ts, *ts_tmp;
	HASH_ITER(hh, mpegts_streams, ts, ts_tmp) {
		if (tv_cmp(ts->last_arrival, cutoff) < 0) {
			HASH_DEL(mpegts_streams, ts);
			free(ts);
		}
	}
}
