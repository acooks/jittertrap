/*
 * webrtc_bridge.c - WebRTC bridge for in-browser video playback
 *
 * Converts RTP streams from the tap to WebRTC for browser playback.
 * Uses libdatachannel for WebRTC stack (DTLS, SRTP, ICE, SDP).
 */

#include "webrtc_bridge.h"

#ifdef ENABLE_WEBRTC_PLAYBACK

#include <rtc/rtc.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <syslog.h>
#include <ctype.h>

#include "flow.h"

#define MAX_VIEWERS 8
#define FKEY_MAX_LEN 256
#define MAX_NAL_SIZE 65536

/*
 * H.264 RTP Depacketization
 *
 * H.264 can be packetized in RTP in several ways (RFC 6184):
 * - Single NAL Unit Mode: One NAL per RTP packet
 * - Non-interleaved Mode: FU-A fragmentation + STAP-A aggregation
 *
 * NAL unit types (first byte & 0x1F):
 * - 1-23: Single NAL unit
 * - 24 (STAP-A): Aggregation packet
 * - 28 (FU-A): Fragmentation unit
 */

/* NAL unit reassembly buffer per viewer */
struct nal_reassembly {
	uint8_t buffer[MAX_NAL_SIZE];
	size_t len;
	int in_progress;
	uint8_t nal_header;
};

/* SPS/PPS storage - needed for mid-stream joins */
#define MAX_SPS_SIZE 256
#define MAX_PPS_SIZE 256
struct h264_params {
	uint8_t sps[MAX_SPS_SIZE];
	size_t sps_len;
	uint8_t pps[MAX_PPS_SIZE];
	size_t pps_len;
	int have_sps;
	int have_pps;
};

/*
 * Send NAL unit with 4-byte big-endian length prefix.
 * libdatachannel's H264 packetizer expects this format by default.
 */
static int send_nal_with_length(int track, const uint8_t *nal, size_t nal_len,
                                 uint32_t timestamp)
{
	if (nal_len == 0 || nal_len > MAX_NAL_SIZE - 4)
		return -1;

	/* Set the RTP timestamp before sending */
	rtcSetTrackRtpTimestamp(track, timestamp);

	uint8_t buf[MAX_NAL_SIZE + 4];
	/* 4-byte big-endian length prefix */
	buf[0] = (nal_len >> 24) & 0xFF;
	buf[1] = (nal_len >> 16) & 0xFF;
	buf[2] = (nal_len >> 8) & 0xFF;
	buf[3] = nal_len & 0xFF;
	memcpy(buf + 4, nal, nal_len);

	return rtcSendMessage(track, (const char *)buf, (int)(nal_len + 4));
}

/*
 * Store SPS or PPS for later use.
 */
static void store_parameter_set(struct h264_params *h264, const uint8_t *nal, size_t len)
{
	uint8_t nal_type = nal[0] & 0x1F;

	if (nal_type == 7 && len <= MAX_SPS_SIZE) {
		/* SPS */
		memcpy(h264->sps, nal, len);
		h264->sps_len = len;
		h264->have_sps = 1;
	} else if (nal_type == 8 && len <= MAX_PPS_SIZE) {
		/* PPS */
		memcpy(h264->pps, nal, len);
		h264->pps_len = len;
		h264->have_pps = 1;
	}
}

/*
 * Send SPS+PPS+IDR as a single access unit.
 * For mid-stream joins, the decoder needs SPS/PPS before it can decode.
 */
static int send_idr_with_params(int track, struct h264_params *h264,
                                 const uint8_t *idr, size_t idr_len,
                                 uint32_t timestamp)
{
	if (!h264->have_sps || !h264->have_pps) {
		return -1;
	}

	/* Set the RTP timestamp before sending */
	rtcSetTrackRtpTimestamp(track, timestamp);

	/* Build access unit: length+SPS + length+PPS + length+IDR */
	size_t total = 4 + h264->sps_len + 4 + h264->pps_len + 4 + idr_len;
	uint8_t *buf = malloc(total);
	if (!buf) return -1;

	size_t pos = 0;

	/* SPS with length prefix */
	buf[pos++] = (h264->sps_len >> 24) & 0xFF;
	buf[pos++] = (h264->sps_len >> 16) & 0xFF;
	buf[pos++] = (h264->sps_len >> 8) & 0xFF;
	buf[pos++] = h264->sps_len & 0xFF;
	memcpy(buf + pos, h264->sps, h264->sps_len);
	pos += h264->sps_len;

	/* PPS with length prefix */
	buf[pos++] = (h264->pps_len >> 24) & 0xFF;
	buf[pos++] = (h264->pps_len >> 16) & 0xFF;
	buf[pos++] = (h264->pps_len >> 8) & 0xFF;
	buf[pos++] = h264->pps_len & 0xFF;
	memcpy(buf + pos, h264->pps, h264->pps_len);
	pos += h264->pps_len;

	/* IDR with length prefix */
	buf[pos++] = (idr_len >> 24) & 0xFF;
	buf[pos++] = (idr_len >> 16) & 0xFF;
	buf[pos++] = (idr_len >> 8) & 0xFF;
	buf[pos++] = idr_len & 0xFF;
	memcpy(buf + pos, idr, idr_len);
	pos += idr_len;

	int ret = rtcSendMessage(track, (const char *)buf, (int)total);
	free(buf);
	return ret;
}

/* Viewer timeout: close viewers that haven't received packets for this long */
#define VIEWER_TIMEOUT_SEC 30

/*
 * SDP Parsing for H.264 payload type extraction
 *
 * Chrome and Firefox offer different H.264 payload types.
 * We need to parse the SDP to find a compatible one.
 *
 * Example SDP lines:
 *   a=rtpmap:109 H264/90000
 *   a=fmtp:109 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f
 */

struct h264_sdp_info {
	int payload_type;
	int packetization_mode;
	char profile_level_id[7];  /* 6 hex chars + null */
};

#define MAX_H264_PROFILES 8

/*
 * Parse SDP offer to find H.264 payload types and their profiles.
 * Returns number of H.264 profiles found.
 */
static int parse_sdp_h264_profiles(const char *sdp,
                                   struct h264_sdp_info *profiles,
                                   int max_profiles)
{
	int count = 0;
	const char *line = sdp;

	/* First pass: find all H264 rtpmap entries */
	while (line && count < max_profiles) {
		const char *next = strchr(line, '\n');

		/* Look for "a=rtpmap:NNN H264/90000" */
		if (strncmp(line, "a=rtpmap:", 9) == 0) {
			int pt;
			char codec[32] = {0};
			if (sscanf(line + 9, "%d %31[^/]/90000", &pt, codec) == 2) {
				if (strcmp(codec, "H264") == 0) {
					profiles[count].payload_type = pt;
					profiles[count].packetization_mode = 1;  /* default */
					profiles[count].profile_level_id[0] = '\0';
					syslog(LOG_DEBUG, "webrtc_bridge: found H264 PT %d", pt);
					count++;
				}
			}
		}

		line = next ? next + 1 : NULL;
	}

	/* Second pass: find fmtp lines to get profile-level-id */
	for (int i = 0; i < count; i++) {
		char fmtp_prefix[32];
		snprintf(fmtp_prefix, sizeof(fmtp_prefix),
		         "a=fmtp:%d ", profiles[i].payload_type);
		size_t prefix_len = strlen(fmtp_prefix);

		line = sdp;
		while (line) {
			const char *next = strchr(line, '\n');

			if (strncmp(line, fmtp_prefix, prefix_len) == 0) {
				const char *params = line + prefix_len;

				/* Extract profile-level-id */
				const char *plid = strstr(params, "profile-level-id=");
				if (plid) {
					plid += 17;  /* skip "profile-level-id=" */
					int j;
					for (j = 0; j < 6 && isxdigit((unsigned char)plid[j]); j++) {
						profiles[i].profile_level_id[j] = plid[j];
					}
					profiles[i].profile_level_id[j] = '\0';
				}

				/* Extract packetization-mode */
				const char *pm = strstr(params, "packetization-mode=");
				if (pm) {
					profiles[i].packetization_mode = atoi(pm + 19);
				}

				break;
			}

			line = next ? next + 1 : NULL;
		}
	}

	return count;
}

/*
 * Select best H.264 profile from parsed SDP.
 * Prefers Constrained Baseline (42e01f) with packetization-mode=1.
 * Returns payload type, or -1 if no compatible profile found.
 */
static int select_h264_profile(struct h264_sdp_info *profiles, int count,
                               char *profile_str, size_t profile_str_len)
{
	int best_idx = -1;
	int best_score = -1;

	for (int i = 0; i < count; i++) {
		int score = 0;

		/* Must have packetization-mode=1 for fragmentation support */
		if (profiles[i].packetization_mode != 1)
			continue;

		/* Score based on profile compatibility */
		if (profiles[i].profile_level_id[0] != '\0') {
			/* Constrained Baseline (42e0xx) - most compatible */
			if (strncasecmp(profiles[i].profile_level_id, "42e0", 4) == 0)
				score = 100;
			/* Baseline (42xx) */
			else if (strncasecmp(profiles[i].profile_level_id, "42", 2) == 0)
				score = 80;
			/* Constrained High (640c) - good for quality */
			else if (strncasecmp(profiles[i].profile_level_id, "640c", 4) == 0)
				score = 60;
			/* Main (4d) */
			else if (strncasecmp(profiles[i].profile_level_id, "4d", 2) == 0)
				score = 40;
			/* High (64) */
			else if (strncasecmp(profiles[i].profile_level_id, "64", 2) == 0)
				score = 30;
			/* Any other profile */
			else
				score = 10;
		} else {
			/* No profile specified, assume baseline */
			score = 5;
		}

		if (score > best_score) {
			best_score = score;
			best_idx = i;
		}
	}

	if (best_idx < 0)
		return -1;

	/* Build profile string for libdatachannel */
	if (profiles[best_idx].profile_level_id[0] != '\0') {
		snprintf(profile_str, profile_str_len,
		         "profile-level-id=%s;packetization-mode=1",
		         profiles[best_idx].profile_level_id);
	} else {
		snprintf(profile_str, profile_str_len,
		         "packetization-mode=1");
	}

	syslog(LOG_INFO, "webrtc_bridge: selected H.264 PT %d (%s)",
	       profiles[best_idx].payload_type, profile_str);

	return profiles[best_idx].payload_type;
}

/* Viewer state */
struct webrtc_viewer {
	int in_use;
	int pc;                      /* PeerConnection handle */
	int track;                   /* Video track handle */
	char fkey[FKEY_MAX_LEN];     /* Flow key being viewed */
	uint32_t ssrc;               /* RTP SSRC */
	int codec;                   /* rtcCodec enum value */
	uint64_t packets_sent;
	uint64_t bytes_sent;
	uint64_t nals_sent;
	volatile int ready;          /* Track is open and ready */
	char *local_sdp;             /* Generated local SDP (answer) */
	pthread_cond_t sdp_cond;     /* Signal when SDP is ready */
	int sdp_ready;               /* Flag for SDP availability */
	struct nal_reassembly nal;   /* NAL reassembly buffer */
	struct h264_params h264;     /* SPS/PPS for mid-stream joins */
	int waiting_for_keyframe;    /* 1 = skip non-IDR until we see IDR */
	uint32_t current_timestamp;  /* RTP timestamp for current frame */
	uint32_t nal_timestamp;      /* Timestamp when NAL reassembly started */
	time_t last_packet_time;     /* Last time a packet was forwarded */
};

static struct webrtc_viewer viewers[MAX_VIEWERS];
static pthread_mutex_t viewers_lock = PTHREAD_MUTEX_INITIALIZER;
static int initialized = 0;

/* Callback: PeerConnection state change */
static void on_state_change(int pc, rtcState state, void *ptr)
{
	int viewer_id = (int)(intptr_t)ptr;
	const char *state_str = "unknown";

	switch (state) {
	case RTC_NEW: state_str = "new"; break;
	case RTC_CONNECTING: state_str = "connecting"; break;
	case RTC_CONNECTED: state_str = "connected"; break;
	case RTC_DISCONNECTED: state_str = "disconnected"; break;
	case RTC_FAILED: state_str = "failed"; break;
	case RTC_CLOSED: state_str = "closed"; break;
	}

	syslog(LOG_INFO, "webrtc_bridge: viewer %d PC state: %s", viewer_id, state_str);

	/* Clean up on disconnect/fail/close */
	if (state == RTC_DISCONNECTED || state == RTC_FAILED || state == RTC_CLOSED) {
		pthread_mutex_lock(&viewers_lock);
		if (viewer_id >= 0 && viewer_id < MAX_VIEWERS &&
		    viewers[viewer_id].in_use && viewers[viewer_id].pc == pc) {
			/* Stop forwarding immediately */
			viewers[viewer_id].ready = 0;
			viewers[viewer_id].track = -1;
		}
		pthread_mutex_unlock(&viewers_lock);
	}
}

/* Callback: ICE gathering state change */
static void on_gathering_state_change(int pc, rtcGatheringState state, void *ptr)
{
	(void)pc; /* unused */
	int viewer_id = (int)(intptr_t)ptr;
	const char *state_str = "unknown";

	switch (state) {
	case RTC_GATHERING_NEW: state_str = "new"; break;
	case RTC_GATHERING_INPROGRESS: state_str = "in-progress"; break;
	case RTC_GATHERING_COMPLETE: state_str = "complete"; break;
	}

	syslog(LOG_DEBUG, "webrtc_bridge: viewer %d ICE gathering: %s",
	       viewer_id, state_str);
}

/* Callback: Track opened */
static void on_track_open(int id, void *ptr)
{
	(void)id; /* unused */
	int viewer_id = (int)(intptr_t)ptr;

	pthread_mutex_lock(&viewers_lock);
	if (viewer_id >= 0 && viewer_id < MAX_VIEWERS &&
	    viewers[viewer_id].in_use) {
		viewers[viewer_id].ready = 1;
		syslog(LOG_INFO, "webrtc_bridge: viewer %d track opened, ssrc=%u",
		       viewer_id, viewers[viewer_id].ssrc);
	}
	pthread_mutex_unlock(&viewers_lock);
}

/* Callback: Track closed */
static void on_track_closed(int id, void *ptr)
{
	(void)id; /* unused */
	int viewer_id = (int)(intptr_t)ptr;

	syslog(LOG_INFO, "webrtc_bridge: viewer %d track closed", viewer_id);

	pthread_mutex_lock(&viewers_lock);
	if (viewer_id >= 0 && viewer_id < MAX_VIEWERS &&
	    viewers[viewer_id].in_use) {
		/* Stop forwarding immediately */
		viewers[viewer_id].ready = 0;
		viewers[viewer_id].track = -1;
	}
	pthread_mutex_unlock(&viewers_lock);
}

/* Callback: Track error */
static void on_track_error(int id, const char *error, void *ptr)
{
	(void)id; /* unused */
	int viewer_id = (int)(intptr_t)ptr;
	syslog(LOG_ERR, "webrtc_bridge: viewer %d track error: %s",
	       viewer_id, error ? error : "(null)");
}

/* Callback: Local description generated (answer) */
static void on_local_description(int pc, const char *sdp, const char *type, void *ptr)
{
	(void)pc;
	int viewer_id = (int)(intptr_t)ptr;

	syslog(LOG_DEBUG, "webrtc_bridge: viewer %d local description type=%s",
	       viewer_id, type ? type : "(null)");

	pthread_mutex_lock(&viewers_lock);
	if (viewer_id >= 0 && viewer_id < MAX_VIEWERS &&
	    viewers[viewer_id].in_use && sdp) {
		/* Store the SDP and signal that it's ready */
		viewers[viewer_id].local_sdp = strdup(sdp);
		viewers[viewer_id].sdp_ready = 1;
		pthread_cond_signal(&viewers[viewer_id].sdp_cond);
	}
	pthread_mutex_unlock(&viewers_lock);
}

int webrtc_bridge_init(void)
{
	if (initialized)
		return 0;

	/* Initialize libdatachannel logging */
	rtcInitLogger(RTC_LOG_WARNING, NULL);

	memset(viewers, 0, sizeof(viewers));
	initialized = 1;

	syslog(LOG_INFO, "webrtc_bridge: initialized");
	return 0;
}

void webrtc_bridge_cleanup(void)
{
	if (!initialized)
		return;

	pthread_mutex_lock(&viewers_lock);
	for (int i = 0; i < MAX_VIEWERS; i++) {
		if (viewers[i].in_use) {
			if (viewers[i].track >= 0)
				rtcDeleteTrack(viewers[i].track);
			if (viewers[i].pc >= 0)
				rtcDeletePeerConnection(viewers[i].pc);
			viewers[i].in_use = 0;
		}
	}
	pthread_mutex_unlock(&viewers_lock);

	rtcCleanup();
	initialized = 0;

	syslog(LOG_INFO, "webrtc_bridge: cleanup complete");
}

/* Find a free viewer slot */
static int find_free_viewer(void)
{
	for (int i = 0; i < MAX_VIEWERS; i++) {
		if (!viewers[i].in_use)
			return i;
	}
	return -1;
}

int webrtc_bridge_handle_offer(const char *sdp_offer,
                               const char *fkey,
                               uint32_t ssrc,
                               int codec,
                               char *sdp_answer,
                               size_t answer_len)
{
	if (!initialized) {
		syslog(LOG_ERR, "webrtc_bridge: not initialized");
		return WEBRTC_ERR_NOT_INIT;
	}

	if (!sdp_offer || !fkey || !sdp_answer || answer_len == 0) {
		syslog(LOG_ERR, "webrtc_bridge: invalid parameters");
		return WEBRTC_ERR_BAD_PARAMS;
	}

	syslog(LOG_DEBUG, "webrtc_bridge: received SDP offer:\n%s", sdp_offer);

	pthread_mutex_lock(&viewers_lock);

	int viewer_id = find_free_viewer();
	if (viewer_id < 0) {
		pthread_mutex_unlock(&viewers_lock);
		syslog(LOG_WARNING, "webrtc_bridge: no free viewer slots (max=%d)",
		       MAX_VIEWERS);
		return WEBRTC_ERR_NO_SLOTS;
	}

	struct webrtc_viewer *v = &viewers[viewer_id];
	memset(v, 0, sizeof(*v));
	v->in_use = 1;
	v->pc = -1;
	v->track = -1;
	strncpy(v->fkey, fkey, FKEY_MAX_LEN - 1);
	v->ssrc = ssrc;
	v->codec = codec;
	v->local_sdp = NULL;
	v->sdp_ready = 0;
	v->waiting_for_keyframe = 1;  /* Wait for IDR before streaming */
	v->last_packet_time = time(NULL);  /* Initialize activity timestamp */
	pthread_cond_init(&v->sdp_cond, NULL);

	pthread_mutex_unlock(&viewers_lock);

	/* Create PeerConnection with no ICE servers (LAN only) */
	rtcConfiguration config = {0};
	config.iceServers = NULL;
	config.iceServersCount = 0;
	/* Force media transport even without data channels */
	config.forceMediaTransport = true;
	/* Disable auto-negotiation so we control when the answer is generated */
	config.disableAutoNegotiation = true;

	int pc = rtcCreatePeerConnection(&config);
	if (pc < 0) {
		syslog(LOG_ERR, "webrtc_bridge: failed to create PeerConnection");
		goto error;
	}

	pthread_mutex_lock(&viewers_lock);
	v->pc = pc;
	pthread_mutex_unlock(&viewers_lock);

	/* Set callbacks - MUST set local description callback BEFORE setRemoteDescription */
	rtcSetUserPointer(pc, (void *)(intptr_t)viewer_id);
	rtcSetStateChangeCallback(pc, on_state_change);
	rtcSetGatheringStateChangeCallback(pc, on_gathering_state_change);
	rtcSetLocalDescriptionCallback(pc, on_local_description);

	/* Parse SDP to find best H.264 payload type */
	struct h264_sdp_info h264_profiles[MAX_H264_PROFILES];
	int num_profiles = parse_sdp_h264_profiles(sdp_offer, h264_profiles,
	                                           MAX_H264_PROFILES);

	char profile_str[64] = "packetization-mode=1";
	int selected_pt = -1;

	if (codec == RTC_CODEC_H264 && num_profiles > 0) {
		selected_pt = select_h264_profile(h264_profiles, num_profiles,
		                                  profile_str, sizeof(profile_str));
	}

	if (selected_pt < 0) {
		/* Fallback to PT 96 with default profile if parsing failed */
		syslog(LOG_WARNING, "webrtc_bridge: no compatible H.264 profile found, "
		       "using fallback PT 96");
		selected_pt = 96;
		snprintf(profile_str, sizeof(profile_str),
		         "profile-level-id=42e01f;packetization-mode=1");
	}

	/* Set remote description (browser's offer) */
	if (rtcSetRemoteDescription(pc, sdp_offer, "offer") < 0) {
		syslog(LOG_ERR, "webrtc_bridge: failed to set remote description");
		goto error;
	}

	/* Add video track - mid must match browser's offer (typically "0") */
	rtcTrackInit track_init = {0};
	track_init.direction = RTC_DIRECTION_SENDONLY;
	track_init.codec = codec;
	track_init.payloadType = selected_pt;
	track_init.ssrc = ssrc;
	track_init.mid = "0";  /* Must match browser's offer mid */
	track_init.name = "video";
	track_init.profile = profile_str;

	int track = rtcAddTrackEx(pc, &track_init);
	if (track < 0) {
		syslog(LOG_ERR, "webrtc_bridge: failed to add track");
		goto error;
	}

	pthread_mutex_lock(&viewers_lock);
	v->track = track;
	pthread_mutex_unlock(&viewers_lock);

	/* Set track callbacks */
	rtcSetUserPointer(track, (void *)(intptr_t)viewer_id);
	rtcSetOpenCallback(track, on_track_open);
	rtcSetClosedCallback(track, on_track_closed);
	rtcSetErrorCallback(track, on_track_error);

	/*
	 * Set up H.264 packetizer. We will depacketize incoming RTP to extract
	 * NAL units, then let libdatachannel re-packetize them into SRTP.
	 * This is necessary because WebRTC requires SRTP encryption.
	 */
	if (codec == RTC_CODEC_H264) {
		rtcPacketizerInit pkt_init = {0};
		pkt_init.ssrc = ssrc;
		pkt_init.cname = "jittertrap";
		pkt_init.payloadType = selected_pt;
		pkt_init.clockRate = 90000;
		pkt_init.sequenceNumber = 0;
		pkt_init.timestamp = 0;

		if (rtcSetH264Packetizer(track, &pkt_init) < 0) {
			syslog(LOG_ERR, "webrtc_bridge: failed to set H264 packetizer");
			goto error;
		}
	}

	/* Generate the answer - this triggers the local description callback */
	if (rtcSetLocalDescription(pc, NULL) < 0) {
		syslog(LOG_ERR, "webrtc_bridge: failed to set local description");
		goto error;
	}

	/* Wait for the local description callback to provide the SDP answer */
	pthread_mutex_lock(&viewers_lock);
	struct timespec timeout;
	clock_gettime(CLOCK_REALTIME, &timeout);
	timeout.tv_sec += 5; /* 5 second timeout */

	while (!v->sdp_ready) {
		int rc = pthread_cond_timedwait(&v->sdp_cond, &viewers_lock, &timeout);
		if (rc != 0) {
			syslog(LOG_ERR, "webrtc_bridge: timeout waiting for SDP answer");
			goto error_locked;
		}
	}

	if (!v->local_sdp) {
		syslog(LOG_ERR, "webrtc_bridge: SDP answer is null");
		goto error_locked;
	}

	/* Copy the SDP answer to the output buffer */
	snprintf(sdp_answer, answer_len, "%s", v->local_sdp);
	free(v->local_sdp);
	v->local_sdp = NULL;
	pthread_mutex_unlock(&viewers_lock);

	syslog(LOG_INFO, "webrtc_bridge: viewer %d created for flow %s ssrc %u",
	       viewer_id, fkey, ssrc);
	syslog(LOG_DEBUG, "webrtc_bridge: viewer %d SDP answer:\n%s",
	       viewer_id, sdp_answer);

	return viewer_id;

error:
	pthread_mutex_lock(&viewers_lock);

error_locked:
	/* Mark as not in use first to stop any callbacks from accessing */
	v->in_use = 0;
	v->ready = 0;

	/* Capture handles before clearing them */
	int err_track = v->track;
	int err_pc = v->pc;
	v->track = -1;
	v->pc = -1;

	if (v->local_sdp) {
		free(v->local_sdp);
		v->local_sdp = NULL;
	}
	pthread_cond_destroy(&v->sdp_cond);
	pthread_mutex_unlock(&viewers_lock);

	/* Delete track and PC after releasing lock (these may block/callback) */
	if (err_track >= 0)
		rtcDeleteTrack(err_track);
	if (err_pc >= 0)
		rtcDeletePeerConnection(err_pc);

	return WEBRTC_ERR_SDP_FAILED;
}

int webrtc_bridge_add_ice_candidate(int viewer_id,
                                    const char *candidate,
                                    const char *mid)
{
	if (!initialized || viewer_id < 0 || viewer_id >= MAX_VIEWERS)
		return -1;

	pthread_mutex_lock(&viewers_lock);

	if (!viewers[viewer_id].in_use || viewers[viewer_id].pc < 0) {
		pthread_mutex_unlock(&viewers_lock);
		return -1;
	}

	int pc = viewers[viewer_id].pc;
	pthread_mutex_unlock(&viewers_lock);

	if (rtcAddRemoteCandidate(pc, candidate, mid) < 0) {
		syslog(LOG_WARNING, "webrtc_bridge: failed to add ICE candidate");
		return -1;
	}

	return 0;
}

void webrtc_bridge_forward_rtp(const uint8_t *rtp_packet,
                               size_t len,
                               const struct flow *f,
                               uint32_t ssrc)
{
	(void)f; /* TODO: use for flow key matching */

	if (!initialized || !rtp_packet || len == 0)
		return;

	/* Quick lockless check - avoid any lock overhead if no viewers */
	int any_viewers = 0;
	for (int i = 0; i < MAX_VIEWERS; i++) {
		if (viewers[i].in_use && viewers[i].ready && viewers[i].ssrc == ssrc) {
			any_viewers = 1;
			break;
		}
	}

	if (!any_viewers)
		return;

	if (len < 13) return; /* Need at least RTP header + 1 byte payload */

	/* Extract RTP timestamp (bytes 4-7, big-endian) */
	uint32_t rtp_timestamp = ((uint32_t)rtp_packet[4] << 24) |
	                         ((uint32_t)rtp_packet[5] << 16) |
	                         ((uint32_t)rtp_packet[6] << 8) |
	                         ((uint32_t)rtp_packet[7]);

	const uint8_t *payload = rtp_packet + 12;
	size_t payload_len = len - 12;

	/* Handle RTP header extensions if present */
	if (rtp_packet[0] & 0x10) {
		if (payload_len < 4) return;
		uint16_t ext_len = (payload[2] << 8) | payload[3];
		size_t ext_bytes = 4 + ext_len * 4;
		if (payload_len < ext_bytes) return;
		payload += ext_bytes;
		payload_len -= ext_bytes;
	}

	if (payload_len < 1) return;

	uint8_t nal_type = payload[0] & 0x1F;

	/*
	 * Process each matching viewer.
	 * We don't hold the lock while calling rtcSendMessage to avoid deadlock
	 * with libdatachannel callbacks that also need the lock.
	 */
	for (int i = 0; i < MAX_VIEWERS; i++) {
		struct webrtc_viewer *v = &viewers[i];

		/* Quick lockless check */
		if (!v->in_use || !v->ready || v->ssrc != ssrc)
			continue;

		int track = v->track;
		if (track < 0)
			continue;

		int ret = -1;

		if (nal_type >= 1 && nal_type <= 23) {
			/* Single NAL unit */
			uint8_t inner_type = payload[0] & 0x1F;

			if (inner_type == 7 || inner_type == 8) {
				store_parameter_set(&v->h264, payload, payload_len);
				continue;
			}

			if (v->waiting_for_keyframe) {
				if (inner_type == 5) {
					ret = send_idr_with_params(track, &v->h264, payload, payload_len, rtp_timestamp);
					if (ret >= 0) {
						v->waiting_for_keyframe = 0;
						v->nals_sent++;
					}
				}
			} else {
				ret = send_nal_with_length(track, payload, payload_len, rtp_timestamp);
				if (ret >= 0) {
					v->nals_sent++;
				}
			}
		} else if (nal_type == 28) {
			/* FU-A fragmentation unit */
			if (payload_len < 2) continue;

			uint8_t fu_header = payload[1];
			int start_bit = (fu_header >> 7) & 1;
			int end_bit = (fu_header >> 6) & 1;
			uint8_t nal_unit_type = fu_header & 0x1F;

			if (start_bit) {
				v->nal.nal_header = (payload[0] & 0xE0) | nal_unit_type;
				v->nal.buffer[0] = v->nal.nal_header;
				v->nal.len = 1;
				v->nal.in_progress = 1;
				v->nal_timestamp = rtp_timestamp;
			}

			if (v->nal.in_progress) {
				size_t frag_len = payload_len - 2;
				if (v->nal.len + frag_len <= MAX_NAL_SIZE) {
					memcpy(v->nal.buffer + v->nal.len, payload + 2, frag_len);
					v->nal.len += frag_len;
				}

				if (end_bit) {
					if (nal_unit_type == 7 || nal_unit_type == 8) {
						store_parameter_set(&v->h264, v->nal.buffer, v->nal.len);
					} else if (v->waiting_for_keyframe) {
						if (nal_unit_type == 5) {
							ret = send_idr_with_params(track, &v->h264,
							                           v->nal.buffer, v->nal.len,
							                           v->nal_timestamp);
							if (ret >= 0) {
								v->waiting_for_keyframe = 0;
								v->nals_sent++;
							}
						}
					} else {
						ret = send_nal_with_length(track, v->nal.buffer, v->nal.len,
						                           v->nal_timestamp);
						if (ret >= 0) {
							v->nals_sent++;
						}
					}
					v->nal.in_progress = 0;
					v->nal.len = 0;
				}
			}
		} else if (nal_type == 24) {
			/* STAP-A aggregation */
			const uint8_t *p = payload + 1;
			size_t remaining = payload_len - 1;

			while (remaining >= 2) {
				uint16_t nal_size = (p[0] << 8) | p[1];
				p += 2;
				remaining -= 2;

				if (nal_size > remaining || nal_size == 0) break;

				uint8_t inner_type = p[0] & 0x1F;

				if (inner_type == 7 || inner_type == 8) {
					store_parameter_set(&v->h264, p, nal_size);
				} else if (v->waiting_for_keyframe) {
					if (inner_type == 5) {
						ret = send_idr_with_params(track, &v->h264, p, nal_size, rtp_timestamp);
						if (ret >= 0) {
							v->waiting_for_keyframe = 0;
							v->nals_sent++;
						}
					}
				} else {
					ret = send_nal_with_length(track, p, nal_size, rtp_timestamp);
					if (ret >= 0) {
						v->nals_sent++;
					}
				}

				p += nal_size;
				remaining -= nal_size;
			}
		}

		v->packets_sent++;
		v->bytes_sent += len;
		v->last_packet_time = time(NULL);
	}
}

void webrtc_bridge_close_viewer(int viewer_id)
{
	if (!initialized || viewer_id < 0 || viewer_id >= MAX_VIEWERS)
		return;

	pthread_mutex_lock(&viewers_lock);

	struct webrtc_viewer *v = &viewers[viewer_id];
	if (!v->in_use) {
		pthread_mutex_unlock(&viewers_lock);
		return;
	}

	syslog(LOG_INFO, "webrtc_bridge: closing viewer %d (sent %lu packets, %lu bytes)",
	       viewer_id, v->packets_sent, v->bytes_sent);

	/*
	 * Mark viewer as inactive BEFORE deleting track/PC.
	 * This prevents webrtc_bridge_forward_rtp from trying to send
	 * to a closed track (it checks ready/in_use without lock).
	 */
	int track = v->track;
	int pc = v->pc;
	v->ready = 0;
	v->in_use = 0;
	v->track = -1;
	v->pc = -1;

	pthread_mutex_unlock(&viewers_lock);

	/* Delete track and PC after releasing lock (these may block/callback) */
	if (track >= 0)
		rtcDeleteTrack(track);

	if (pc >= 0)
		rtcDeletePeerConnection(pc);

	/* Clean up remaining state (no lock needed, viewer is already marked inactive) */
	if (v->local_sdp) {
		free(v->local_sdp);
		v->local_sdp = NULL;
	}
	pthread_cond_destroy(&v->sdp_cond);
}

int webrtc_bridge_has_viewers(const char *fkey, uint32_t ssrc)
{
	(void)fkey; /* TODO: use for flow key matching */

	if (!initialized)
		return 0;

	pthread_mutex_lock(&viewers_lock);

	for (int i = 0; i < MAX_VIEWERS; i++) {
		struct webrtc_viewer *v = &viewers[i];
		if (v->in_use && v->ready && v->ssrc == ssrc) {
			/* TODO: Also match fkey */
			pthread_mutex_unlock(&viewers_lock);
			return 1;
		}
	}

	pthread_mutex_unlock(&viewers_lock);
	return 0;
}

int webrtc_bridge_get_stats(int viewer_id,
                            uint64_t *packets_sent,
                            uint64_t *bytes_sent,
                            int *waiting_for_keyframe)
{
	if (!initialized || viewer_id < 0 || viewer_id >= MAX_VIEWERS)
		return -1;

	pthread_mutex_lock(&viewers_lock);

	struct webrtc_viewer *v = &viewers[viewer_id];
	if (!v->in_use) {
		pthread_mutex_unlock(&viewers_lock);
		return -1;
	}

	if (packets_sent)
		*packets_sent = v->packets_sent;
	if (bytes_sent)
		*bytes_sent = v->bytes_sent;
	if (waiting_for_keyframe)
		*waiting_for_keyframe = v->waiting_for_keyframe;

	pthread_mutex_unlock(&viewers_lock);
	return 0;
}

void webrtc_bridge_check_timeouts(void)
{
	if (!initialized)
		return;

	time_t now = time(NULL);
	int timed_out[MAX_VIEWERS];
	int num_timed_out = 0;

	/* First pass: identify timed-out viewers while holding lock */
	pthread_mutex_lock(&viewers_lock);
	for (int i = 0; i < MAX_VIEWERS; i++) {
		struct webrtc_viewer *v = &viewers[i];
		if (v->in_use && v->ready) {
			time_t idle_time = now - v->last_packet_time;
			if (idle_time > VIEWER_TIMEOUT_SEC) {
				timed_out[num_timed_out++] = i;
				syslog(LOG_INFO, "webrtc_bridge: viewer %d timed out "
				       "(idle %ld sec)", i, (long)idle_time);
			}
		}
	}
	pthread_mutex_unlock(&viewers_lock);

	/* Second pass: close timed-out viewers without holding lock */
	for (int i = 0; i < num_timed_out; i++) {
		webrtc_bridge_close_viewer(timed_out[i]);
	}
}

#else /* !ENABLE_WEBRTC_PLAYBACK */

/* Stub implementations when WebRTC is disabled */

int webrtc_bridge_init(void)
{
	return 0;
}

void webrtc_bridge_cleanup(void)
{
}

int webrtc_bridge_handle_offer(const char *sdp_offer,
                               const char *fkey,
                               uint32_t ssrc,
                               int codec,
                               char *sdp_answer,
                               size_t answer_len)
{
	(void)sdp_offer;
	(void)fkey;
	(void)ssrc;
	(void)codec;
	(void)sdp_answer;
	(void)answer_len;
	return -1;
}

int webrtc_bridge_add_ice_candidate(int viewer_id,
                                    const char *candidate,
                                    const char *mid)
{
	(void)viewer_id;
	(void)candidate;
	(void)mid;
	return -1;
}

void webrtc_bridge_forward_rtp(const uint8_t *rtp_packet,
                               size_t len,
                               const struct flow *f,
                               uint32_t ssrc)
{
	(void)rtp_packet;
	(void)len;
	(void)f;
	(void)ssrc;
}

void webrtc_bridge_close_viewer(int viewer_id)
{
	(void)viewer_id;
}

int webrtc_bridge_has_viewers(const char *fkey, uint32_t ssrc)
{
	(void)fkey;
	(void)ssrc;
	return 0;
}

int webrtc_bridge_get_stats(int viewer_id,
                            uint64_t *packets_sent,
                            uint64_t *bytes_sent,
                            int *waiting_for_keyframe)
{
	(void)viewer_id;
	(void)packets_sent;
	(void)bytes_sent;
	(void)waiting_for_keyframe;
	return -1;
}

void webrtc_bridge_check_timeouts(void)
{
	/* No-op when WebRTC is disabled */
}

#endif /* ENABLE_WEBRTC_PLAYBACK */
