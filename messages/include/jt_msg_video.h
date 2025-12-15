#ifndef JT_MSG_VIDEO_H
#define JT_MSG_VIDEO_H

#include <stdint.h>

/*
 * WebRTC video playback message structures
 */

/* JT_MSG_VIDEO_ERROR_V1 - Server sends error notification */
struct jt_msg_video_error {
	char code[32];       /* Error code */
	char message[256];   /* Human-readable error message */
};

/* JT_MSG_WEBRTC_OFFER_V1 - Client sends WebRTC SDP offer */
struct jt_msg_webrtc_offer {
	char fkey[256];          /* Flow key identifying the stream */
	uint32_t ssrc;           /* RTP SSRC */
	int codec;               /* rtcCodec enum value */
	char sdp[16384];         /* SDP offer from browser (Chromium sends large offers) */
};

/* JT_MSG_WEBRTC_ANSWER_V1 - Server sends WebRTC SDP answer */
struct jt_msg_webrtc_answer {
	int viewer_id;           /* Viewer session ID */
	char sdp[8192];          /* SDP answer */
};

/* JT_MSG_WEBRTC_ICE_V1 - ICE candidate exchange (bidirectional) */
struct jt_msg_webrtc_ice {
	int viewer_id;           /* Viewer session ID */
	char candidate[512];     /* ICE candidate string */
	char mid[32];            /* Media ID */
};

/* JT_MSG_WEBRTC_STOP_V1 - Client requests to stop WebRTC session */
struct jt_msg_webrtc_stop {
	int viewer_id;           /* Viewer session ID to stop */
};

/* JT_MSG_WEBRTC_STATUS_V1 - Server sends WebRTC session status */
struct jt_msg_webrtc_status {
	int viewer_id;           /* Viewer session ID */
	uint8_t active;          /* 1 if session is active */
	uint8_t waiting_for_keyframe; /* 1 if waiting for IDR */
	uint64_t packets_sent;   /* Total packets sent */
	uint64_t bytes_sent;     /* Total bytes sent */
};

/* Message handler functions */
int jt_video_error_packer(void *data, char **out);
int jt_video_error_unpacker(json_t *root, void **data);
int jt_video_error_printer(void *data, char *out, int len);
int jt_video_error_free(void *data);
const char *jt_video_error_test_msg_get(void);

int jt_webrtc_offer_packer(void *data, char **out);
int jt_webrtc_offer_unpacker(json_t *root, void **data);
int jt_webrtc_offer_printer(void *data, char *out, int len);
int jt_webrtc_offer_free(void *data);
const char *jt_webrtc_offer_test_msg_get(void);

int jt_webrtc_answer_packer(void *data, char **out);
int jt_webrtc_answer_unpacker(json_t *root, void **data);
int jt_webrtc_answer_printer(void *data, char *out, int len);
int jt_webrtc_answer_free(void *data);
const char *jt_webrtc_answer_test_msg_get(void);

int jt_webrtc_ice_packer(void *data, char **out);
int jt_webrtc_ice_unpacker(json_t *root, void **data);
int jt_webrtc_ice_printer(void *data, char *out, int len);
int jt_webrtc_ice_free(void *data);
const char *jt_webrtc_ice_test_msg_get(void);

int jt_webrtc_stop_packer(void *data, char **out);
int jt_webrtc_stop_unpacker(json_t *root, void **data);
int jt_webrtc_stop_printer(void *data, char *out, int len);
int jt_webrtc_stop_free(void *data);
const char *jt_webrtc_stop_test_msg_get(void);

int jt_webrtc_status_packer(void *data, char **out);
int jt_webrtc_status_unpacker(json_t *root, void **data);
int jt_webrtc_status_printer(void *data, char *out, int len);
int jt_webrtc_status_free(void *data);
const char *jt_webrtc_status_test_msg_get(void);

#endif /* JT_MSG_VIDEO_H */
