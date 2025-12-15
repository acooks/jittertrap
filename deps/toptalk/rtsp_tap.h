/*
 * rtsp_tap.h - RTSP passive tap for extracting SDP/codec info
 *
 * Passively observes RTSP signaling to:
 * 1. Extract codec parameters from SDP (DESCRIBE response)
 * 2. Learn port mappings from SETUP/Transport headers
 * 3. Link RTSP control sessions to RTP media flows
 * 4. Track session state (PLAYING, PAUSED, TEARDOWN)
 */

#ifndef RTSP_TAP_H
#define RTSP_TAP_H

#include <stdint.h>
#include <netinet/in.h>
#include "flow.h"

/* Maximum number of tracked RTSP sessions */
#define RTSP_MAX_SESSIONS 64

/* Maximum media streams per RTSP session (video, audio, etc.) */
#define RTSP_MAX_MEDIA 4

/* Buffer size for accumulating RTSP messages */
#define RTSP_MSG_BUFFER_SIZE 4096

/* RTSP session states */
enum rtsp_state {
	RTSP_STATE_INIT = 0,
	RTSP_STATE_DESCRIBED = 1,   /* DESCRIBE response received with SDP */
	RTSP_STATE_SETUP = 2,       /* SETUP complete, ports negotiated */
	RTSP_STATE_PLAYING = 3,     /* PLAY sent, streaming active */
	RTSP_STATE_PAUSED = 4,      /* PAUSE sent */
	RTSP_STATE_TEARDOWN = 5     /* TEARDOWN sent, session ending */
};

/* Media stream info extracted from SDP and SETUP */
struct rtsp_media {
	uint8_t valid;              /* 1 if this slot is in use */
	uint8_t payload_type;       /* RTP payload type from SDP m= line */
	uint8_t codec;              /* enum video_codec */
	char codec_name[32];        /* e.g., "H264", "H265" */
	uint32_t clock_rate;        /* e.g., 90000 for video */

	/* From SDP a=fmtp line */
	uint8_t profile_idc;        /* H.264 profile */
	uint8_t level_idc;          /* H.264 level */
	char sprop_params[256];     /* sprop-parameter-sets (base64 SPS/PPS) */

	/* From SETUP Transport header */
	uint16_t client_rtp_port;   /* Client's RTP port */
	uint16_t client_rtcp_port;  /* Client's RTCP port */
	uint16_t server_rtp_port;   /* Server's RTP port */
	uint16_t server_rtcp_port;  /* Server's RTCP port */
	uint32_t ssrc;              /* SSRC if specified in Transport */

	/* Linked RTP flow (if found) */
	struct flow rtp_flow;       /* Matched RTP flow 5-tuple */
	uint8_t rtp_flow_linked;    /* 1 if rtp_flow is valid */
};

/* RTSP session tracking */
struct rtsp_session {
	uint8_t active;             /* 1 if this slot is in use */

	/* Control connection (TCP flow) */
	struct flow control_flow;   /* RTSP TCP connection 5-tuple */
	char session_id[64];        /* RTSP Session header value */
	char url[256];              /* RTSP URL from DESCRIBE */

	/* Session state */
	enum rtsp_state state;
	uint64_t last_activity_ns;  /* Timestamp of last RTSP message */

	/* Media streams */
	struct rtsp_media media[RTSP_MAX_MEDIA];
	int media_count;

	/* Message accumulation buffer (for multi-packet messages) */
	char msg_buffer[RTSP_MSG_BUFFER_SIZE];
	size_t msg_buffer_len;
	int expecting_body;         /* 1 if waiting for Content-Length body */
	size_t expected_body_len;
};

/* Global RTSP tap state */
struct rtsp_tap_state {
	struct rtsp_session sessions[RTSP_MAX_SESSIONS];
	int session_count;
};

/*
 * Initialize RTSP tap state.
 */
void rtsp_tap_init(struct rtsp_tap_state *state);

/*
 * Process a TCP packet that may contain RTSP data.
 * Called from packet processing path for TCP port 554 traffic.
 *
 * @param state     RTSP tap state
 * @param flow      TCP flow 5-tuple
 * @param payload   TCP payload data
 * @param len       Payload length
 * @param timestamp Packet timestamp in nanoseconds
 * @return 1 if RTSP message was processed, 0 otherwise
 */
int rtsp_tap_process_packet(struct rtsp_tap_state *state,
                            const struct flow *flow,
                            const uint8_t *payload, size_t len,
                            uint64_t timestamp);

/*
 * Try to link an RTP flow to an RTSP session.
 * Called when a new RTP stream is detected to see if we have
 * SDP info for it from a prior RTSP DESCRIBE.
 *
 * @param state     RTSP tap state
 * @param rtp_flow  RTP flow 5-tuple (UDP)
 * @param ssrc      RTP SSRC from packet
 * @return Pointer to rtsp_media if matched, NULL otherwise
 */
const struct rtsp_media *rtsp_tap_find_media_for_rtp(
    struct rtsp_tap_state *state,
    const struct flow *rtp_flow,
    uint32_t ssrc);

/*
 * Get RTSP session state for a linked RTP flow.
 *
 * @param state     RTSP tap state
 * @param rtp_flow  RTP flow to look up
 * @return enum rtsp_state, or RTSP_STATE_INIT if not found
 */
enum rtsp_state rtsp_tap_get_session_state(
    struct rtsp_tap_state *state,
    const struct flow *rtp_flow);

/*
 * Clean up expired RTSP sessions.
 * Call periodically to remove stale sessions.
 *
 * @param state     RTSP tap state
 * @param now_ns    Current timestamp in nanoseconds
 * @param timeout_ns Session timeout in nanoseconds (e.g., 60 seconds)
 */
void rtsp_tap_cleanup(struct rtsp_tap_state *state,
                      uint64_t now_ns, uint64_t timeout_ns);

#endif /* RTSP_TAP_H */
