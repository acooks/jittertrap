#ifndef VIDEO_METRICS_H
#define VIDEO_METRICS_H

#include <stdint.h>
#include <sys/time.h>
#include "flow.h"

/*
 * Video stream metrics tracking.
 *
 * For RTP streams:
 * - Jitter: RFC 3550 interarrival jitter calculation
 * - Loss: Sequence number discontinuities
 *
 * For MPEG-TS streams:
 * - Continuity counter errors per PID
 */

/*
 * Process an RTP packet and update metrics for the flow.
 *
 * @param flow       Flow key (5-tuple)
 * @param rtp_info   RTP header info from video_detect_rtp()
 * @param timestamp  Packet arrival time
 *
 * @return 0 on success, -1 on error
 */
int video_metrics_rtp_process(const struct flow *flow,
                              const struct rtp_info *rtp_info,
                              struct timeval timestamp);

/*
 * Process an MPEG-TS packet and update metrics for the flow.
 *
 * @param flow       Flow key (5-tuple)
 * @param ts_info    MPEG-TS info from video_detect_mpegts()
 * @param payload    Raw TS packet data
 * @param payload_len Length of payload
 *
 * @return 0 on success, -1 on error
 */
int video_metrics_mpegts_process(const struct flow *flow,
                                 struct mpegts_info *ts_info,
                                 const uint8_t *payload,
                                 size_t payload_len);

/*
 * Get current video metrics for a flow (returns first matching SSRC).
 *
 * @param flow       Flow key (5-tuple)
 * @param video_info Output: Video metrics (stream_type must be set)
 *
 * @return 0 on success, -1 if flow not found or not a video stream
 */
int video_metrics_get(const struct flow *flow, struct flow_video_info *video_info);

/*
 * Get current video metrics for a specific flow+SSRC combination.
 *
 * @param flow       Flow key (5-tuple)
 * @param ssrc       RTP SSRC to identify specific stream
 * @param video_info Output: Video metrics
 *
 * @return 0 on success, -1 if flow+SSRC not found
 */
int video_metrics_get_by_ssrc(const struct flow *flow, uint32_t ssrc,
                              struct flow_video_info *video_info);

/*
 * Get count of RTP streams for a flow (different SSRCs).
 *
 * @param flow       Flow key (5-tuple)
 *
 * @return Number of RTP streams with matching flow, 0 if none
 */
int video_metrics_get_stream_count(const struct flow *flow);

/*
 * Get video metrics for the Nth RTP stream matching a flow.
 * Use with video_metrics_get_stream_count() to iterate all streams.
 *
 * @param flow       Flow key (5-tuple)
 * @param index      Stream index (0-based)
 * @param video_info Output: Video metrics
 *
 * @return 0 on success, -1 if index out of range or flow not found
 */
int video_metrics_get_by_index(const struct flow *flow, int index,
                               struct flow_video_info *video_info);

/*
 * Update codec parameters for an RTP flow (from SPS parsing or SDP).
 *
 * @param flow       Flow key (5-tuple)
 * @param ssrc       RTP SSRC to identify specific stream
 * @param source     CODEC_SRC_INBAND or CODEC_SRC_SDP
 * @param width      Video width in pixels
 * @param height     Video height in pixels
 * @param profile    H.264/H.265 profile_idc
 * @param level      H.264/H.265 level_idc
 *
 * @return 0 on success, -1 if flow not found
 */
int video_metrics_update_codec_params(const struct flow *flow,
                                      uint32_t ssrc,
                                      uint8_t source,
                                      uint16_t width, uint16_t height,
                                      uint8_t profile, uint8_t level);

/*
 * Update frame/keyframe count for an RTP flow.
 * Called when a complete frame (access unit) is detected.
 *
 * @param flow       Flow key (5-tuple)
 * @param ssrc       RTP SSRC to identify specific stream
 * @param is_keyframe 1 if this is a keyframe (IDR), 0 otherwise
 * @param rtp_ts     RTP timestamp of the frame
 * @param frame_bytes Size of the frame in bytes
 *
 * @return 0 on success, -1 if flow not found
 */
int video_metrics_update_frame(const struct flow *flow,
                               uint32_t ssrc,
                               int is_keyframe,
                               uint32_t rtp_ts,
                               size_t frame_bytes);

/*
 * Initialize video metrics tracking.
 */
void video_metrics_init(void);

/*
 * Cleanup video metrics tracking (free hash tables).
 */
void video_metrics_cleanup(void);

/*
 * Expire old video flow entries based on deadline.
 *
 * @param deadline    Current time reference
 * @param max_age     Maximum age for entries
 */
void video_metrics_expire_old(struct timeval deadline, struct timeval max_age);

#endif /* VIDEO_METRICS_H */
