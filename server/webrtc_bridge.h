/*
 * webrtc_bridge.h - WebRTC bridge for in-browser video playback
 *
 * Converts RTP streams from the tap to WebRTC for browser playback.
 * Uses libdatachannel for WebRTC stack (DTLS, SRTP, ICE, SDP).
 */

#ifndef WEBRTC_BRIDGE_H
#define WEBRTC_BRIDGE_H

#include <stdint.h>
#include <stddef.h>

/* Forward declaration */
struct flow;

/* Error codes for webrtc_bridge_handle_offer */
#define WEBRTC_ERR_NO_SLOTS       -2  /* All viewer slots are in use */
#define WEBRTC_ERR_NOT_INIT       -3  /* Bridge not initialized */
#define WEBRTC_ERR_BAD_PARAMS     -4  /* Invalid parameters */
#define WEBRTC_ERR_PC_FAILED      -5  /* PeerConnection creation failed */
#define WEBRTC_ERR_SDP_FAILED     -6  /* SDP negotiation failed */
#define WEBRTC_ERR_CODEC_UNSUP    -7  /* Requested codec not supported by browser */

/*
 * Initialize the WebRTC bridge subsystem.
 * Must be called before any other webrtc_bridge functions.
 * Returns 0 on success, -1 on error.
 */
int webrtc_bridge_init(void);

/*
 * Cleanup the WebRTC bridge subsystem.
 * Should be called on server shutdown.
 */
void webrtc_bridge_cleanup(void);

/*
 * Handle a WebRTC offer from the browser.
 *
 * @param sdp_offer    SDP offer from browser
 * @param fkey         Flow key identifying the stream to view
 * @param ssrc         RTP SSRC to forward
 * @param codec        Codec type (RTC_CODEC_H264, etc)
 * @param sdp_answer   Buffer to store generated SDP answer
 * @param answer_len   Size of sdp_answer buffer
 *
 * Returns viewer_id on success (>= 0), -1 on error.
 */
int webrtc_bridge_handle_offer(const char *sdp_offer,
                               const char *fkey,
                               uint32_t ssrc,
                               int codec,
                               char *sdp_answer,
                               size_t answer_len);

/*
 * Add an ICE candidate from the browser.
 *
 * @param viewer_id    Viewer ID from webrtc_bridge_handle_offer
 * @param candidate    ICE candidate string
 * @param mid          Media ID
 *
 * Returns 0 on success, -1 on error.
 */
int webrtc_bridge_add_ice_candidate(int viewer_id,
                                    const char *candidate,
                                    const char *mid);

/*
 * Forward an RTP packet to any active WebRTC viewers watching this flow.
 * Called from tt_thread for each RTP packet.
 *
 * @param rtp_packet   Raw RTP packet data
 * @param len          Length of RTP packet
 * @param f            Flow the packet belongs to
 * @param ssrc         RTP SSRC from packet header
 */
void webrtc_bridge_forward_rtp(const uint8_t *rtp_packet,
                               size_t len,
                               const struct flow *f,
                               uint32_t ssrc);

/*
 * Stop and close a viewer session.
 *
 * @param viewer_id    Viewer ID to close
 */
void webrtc_bridge_close_viewer(int viewer_id);

/*
 * Check if there are any active viewers for a given flow/ssrc.
 * Used to avoid processing packets when no one is watching.
 *
 * @param fkey    Flow key
 * @param ssrc    RTP SSRC
 *
 * Returns 1 if there are active viewers, 0 otherwise.
 */
int webrtc_bridge_has_viewers(const char *fkey, uint32_t ssrc);

/*
 * Get statistics for a viewer session.
 *
 * @param viewer_id              Viewer ID
 * @param packets_sent           Output: packets sent to viewer
 * @param bytes_sent             Output: bytes sent to viewer
 * @param waiting_for_keyframe   Output: 1 if waiting for IDR, 0 otherwise
 *
 * Returns 0 on success, -1 if viewer not found.
 */
int webrtc_bridge_get_stats(int viewer_id,
                            uint64_t *packets_sent,
                            uint64_t *bytes_sent,
                            int *waiting_for_keyframe);

/*
 * Check for and close timed-out viewer sessions.
 * Should be called periodically (e.g., every few seconds).
 * Viewers are closed if no packets have been forwarded for 30 seconds.
 */
void webrtc_bridge_check_timeouts(void);

#endif /* WEBRTC_BRIDGE_H */
