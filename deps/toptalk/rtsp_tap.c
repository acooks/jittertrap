/*
 * rtsp_tap.c - RTSP passive tap implementation
 *
 * Passively observes RTSP signaling to extract SDP info and
 * link RTSP control sessions to RTP media flows.
 */

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "rtsp_tap.h"

/* RTSP methods we care about */
#define RTSP_METHOD_DESCRIBE "DESCRIBE"
#define RTSP_METHOD_SETUP    "SETUP"
#define RTSP_METHOD_PLAY     "PLAY"
#define RTSP_METHOD_PAUSE    "PAUSE"
#define RTSP_METHOD_TEARDOWN "TEARDOWN"

/* Session timeout: 60 seconds of inactivity */
#define RTSP_SESSION_TIMEOUT_NS (60ULL * 1000000000ULL)

/*
 * Helper: case-insensitive string prefix match
 */
static int starts_with_i(const char *str, const char *prefix)
{
	while (*prefix) {
		if (tolower((unsigned char)*str) != tolower((unsigned char)*prefix))
			return 0;
		str++;
		prefix++;
	}
	return 1;
}

/*
 * Helper: find end of line in buffer (handles \r\n and \n)
 */
static const char *find_eol(const char *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (buf[i] == '\r' && i + 1 < len && buf[i + 1] == '\n')
			return buf + i;
		if (buf[i] == '\n')
			return buf + i;
	}
	return NULL;
}

/*
 * Helper: skip to next line
 */
static const char *next_line(const char *eol, const char *end)
{
	if (!eol || eol >= end)
		return NULL;
	if (*eol == '\r' && eol + 1 < end && *(eol + 1) == '\n')
		return eol + 2;
	if (*eol == '\n')
		return eol + 1;
	return eol + 1;
}

/*
 * Helper: extract header value
 * Finds "Header: value\r\n" and returns pointer to value
 */
static const char *get_header_value(const char *msg, size_t len,
                                    const char *header, size_t *value_len)
{
	size_t hdr_len = strlen(header);
	const char *end = msg + len;
	const char *line = msg;

	while (line && line < end) {
		const char *eol = find_eol(line, end - line);
		if (!eol)
			break;

		/* Check for header match (case-insensitive) */
		if (starts_with_i(line, header) && line[hdr_len] == ':') {
			const char *val = line + hdr_len + 1;
			/* Skip whitespace */
			while (val < eol && (*val == ' ' || *val == '\t'))
				val++;
			*value_len = eol - val;
			return val;
		}

		line = next_line(eol, end);
	}

	*value_len = 0;
	return NULL;
}

/*
 * Parse SDP for codec info.
 * Looks for:
 *   m=video <port> RTP/AVP <payload_type>
 *   a=rtpmap:<pt> <codec>/<clock_rate>
 *   a=fmtp:<pt> <params including profile-level-id>
 */
static int parse_sdp(const char *sdp, size_t len, struct rtsp_media *media)
{
	const char *end = sdp + len;
	const char *line = sdp;
	int found_video = 0;
	uint8_t current_pt = 0;

	while (line && line < end) {
		const char *eol = find_eol(line, end - line);
		if (!eol)
			eol = end;

		size_t line_len = eol - line;

		/* m=video line */
		if (line_len > 8 && strncmp(line, "m=video ", 8) == 0) {
			/* Parse: m=video <port> RTP/AVP <pt> */
			int port, pt;
			if (sscanf(line, "m=video %d RTP/AVP %d", &port, &pt) >= 2) {
				found_video = 1;
				current_pt = (uint8_t)pt;
				media->payload_type = current_pt;
				media->valid = 1;
			}
		}

		/* a=rtpmap line */
		if (line_len > 9 && strncmp(line, "a=rtpmap:", 9) == 0) {
			int pt;
			char codec[32] = {0};
			int clock_rate;
			if (sscanf(line, "a=rtpmap:%d %31[^/]/%d", &pt, codec, &clock_rate) >= 2) {
				if ((uint8_t)pt == current_pt && found_video) {
					strncpy(media->codec_name, codec, sizeof(media->codec_name) - 1);
					media->clock_rate = clock_rate;

					/* Identify codec */
					if (strcasecmp(codec, "H264") == 0)
						media->codec = VIDEO_CODEC_H264;
					else if (strcasecmp(codec, "H265") == 0 || strcasecmp(codec, "HEVC") == 0)
						media->codec = VIDEO_CODEC_H265;
					else if (strcasecmp(codec, "VP8") == 0)
						media->codec = VIDEO_CODEC_VP8;
					else if (strcasecmp(codec, "VP9") == 0)
						media->codec = VIDEO_CODEC_VP9;
					else if (strcasecmp(codec, "AV1") == 0)
						media->codec = VIDEO_CODEC_AV1;
				}
			}
		}

		/* a=fmtp line */
		if (line_len > 7 && strncmp(line, "a=fmtp:", 7) == 0) {
			int pt;
			char params[512] = {0};
			if (sscanf(line, "a=fmtp:%d %511[^\r\n]", &pt, params) >= 2) {
				if ((uint8_t)pt == current_pt && found_video) {
					/* Look for profile-level-id */
					char *plid = strstr(params, "profile-level-id=");
					if (plid) {
						plid += strlen("profile-level-id=");
						/* profile-level-id is 6 hex chars: PPCCLL */
						/* PP = profile_idc, CC = constraint, LL = level_idc */
						unsigned int profile_level;
						if (sscanf(plid, "%6x", &profile_level) == 1) {
							media->profile_idc = (profile_level >> 16) & 0xFF;
							media->level_idc = profile_level & 0xFF;
						}
					}

					/* Store sprop-parameter-sets if present */
					char *sprop = strstr(params, "sprop-parameter-sets=");
					if (sprop) {
						sprop += strlen("sprop-parameter-sets=");
						/* Copy until semicolon or end */
						size_t i = 0;
						while (sprop[i] && sprop[i] != ';' && sprop[i] != ' ' &&
						       i < sizeof(media->sprop_params) - 1) {
							media->sprop_params[i] = sprop[i];
							i++;
						}
						media->sprop_params[i] = '\0';
					}
				}
			}
		}

		line = next_line(eol, end);
	}

	return found_video ? 1 : 0;
}

/*
 * Parse Transport header from SETUP response.
 * Example: Transport: RTP/AVP;unicast;client_port=5000-5001;server_port=6000-6001;ssrc=12345678
 */
static int parse_transport_header(const char *transport, size_t len,
                                  struct rtsp_media *media)
{
	/* Make null-terminated copy for easier parsing */
	char buf[512];
	size_t copy_len = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
	memcpy(buf, transport, copy_len);
	buf[copy_len] = '\0';

	/* Parse client_port */
	char *cp = strstr(buf, "client_port=");
	if (cp) {
		int rtp, rtcp;
		if (sscanf(cp, "client_port=%d-%d", &rtp, &rtcp) >= 1) {
			media->client_rtp_port = rtp;
			media->client_rtcp_port = rtcp ? rtcp : rtp + 1;
		}
	}

	/* Parse server_port */
	char *sp = strstr(buf, "server_port=");
	if (sp) {
		int rtp, rtcp;
		if (sscanf(sp, "server_port=%d-%d", &rtp, &rtcp) >= 1) {
			media->server_rtp_port = rtp;
			media->server_rtcp_port = rtcp ? rtcp : rtp + 1;
		}
	}

	/* Parse ssrc */
	char *ssrc_str = strstr(buf, "ssrc=");
	if (ssrc_str) {
		unsigned int ssrc;
		if (sscanf(ssrc_str, "ssrc=%x", &ssrc) == 1 ||
		    sscanf(ssrc_str, "ssrc=%u", &ssrc) == 1) {
			media->ssrc = ssrc;
		}
	}

	return (media->client_rtp_port || media->server_rtp_port) ? 1 : 0;
}

/*
 * Find or create session for a flow
 */
static struct rtsp_session *find_or_create_session(struct rtsp_tap_state *state,
                                                   const struct flow *flow)
{
	/* First, try to find existing session */
	for (int i = 0; i < RTSP_MAX_SESSIONS; i++) {
		struct rtsp_session *s = &state->sessions[i];
		if (s->active && flow_cmp(&s->control_flow, flow) == 0)
			return s;
	}

	/* Create new session in first free slot */
	for (int i = 0; i < RTSP_MAX_SESSIONS; i++) {
		struct rtsp_session *s = &state->sessions[i];
		if (!s->active) {
			memset(s, 0, sizeof(*s));
			s->active = 1;
			memcpy(&s->control_flow, flow, sizeof(*flow));
			state->session_count++;
			return s;
		}
	}

	return NULL; /* No free slots */
}

/*
 * Process an RTSP message (request or response)
 */
static int process_rtsp_message(struct rtsp_session *session,
                                const char *msg, size_t len,
                                uint64_t timestamp)
{
	session->last_activity_ns = timestamp;

	/* Check for response (starts with "RTSP/1.0 ") */
	if (len >= 9 && strncmp(msg, "RTSP/1.0 ", 9) == 0) {
		int status_code = 0;
		sscanf(msg + 9, "%d", &status_code);

		if (status_code != 200)
			return 0; /* Only process successful responses */

		/* Check Content-Type for SDP */
		size_t ct_len;
		const char *ct = get_header_value(msg, len, "Content-Type", &ct_len);
		if (ct && ct_len >= 15 && strncasecmp(ct, "application/sdp", 15) == 0) {
			/* Find body (after blank line) */
			const char *body = strstr(msg, "\r\n\r\n");
			if (body) {
				body += 4;
				size_t body_len = len - (body - msg);
				if (body_len > 0 && session->media_count < RTSP_MAX_MEDIA) {
					struct rtsp_media *media = &session->media[session->media_count];
					if (parse_sdp(body, body_len, media)) {
						session->media_count++;
						session->state = RTSP_STATE_DESCRIBED;
					}
				}
			}
		}

		/* Check for Transport header (SETUP response) */
		size_t tr_len;
		const char *tr = get_header_value(msg, len, "Transport", &tr_len);
		if (tr && tr_len > 0 && session->media_count > 0) {
			/* Apply to most recent media */
			struct rtsp_media *media = &session->media[session->media_count - 1];
			if (parse_transport_header(tr, tr_len, media)) {
				session->state = RTSP_STATE_SETUP;
			}
		}

		/* Check for Session header */
		size_t sess_len;
		const char *sess = get_header_value(msg, len, "Session", &sess_len);
		if (sess && sess_len > 0) {
			size_t copy_len = sess_len < sizeof(session->session_id) - 1 ?
			                  sess_len : sizeof(session->session_id) - 1;
			/* Copy up to semicolon (session ID may have ;timeout=X) */
			for (size_t i = 0; i < copy_len; i++) {
				if (sess[i] == ';')
					break;
				session->session_id[i] = sess[i];
			}
		}

		return 1;
	}

	/* Check for requests */
	if (strncmp(msg, RTSP_METHOD_PLAY, strlen(RTSP_METHOD_PLAY)) == 0) {
		session->state = RTSP_STATE_PLAYING;
		return 1;
	}
	if (strncmp(msg, RTSP_METHOD_PAUSE, strlen(RTSP_METHOD_PAUSE)) == 0) {
		session->state = RTSP_STATE_PAUSED;
		return 1;
	}
	if (strncmp(msg, RTSP_METHOD_TEARDOWN, strlen(RTSP_METHOD_TEARDOWN)) == 0) {
		session->state = RTSP_STATE_TEARDOWN;
		return 1;
	}
	if (strncmp(msg, RTSP_METHOD_DESCRIBE, strlen(RTSP_METHOD_DESCRIBE)) == 0) {
		/* Extract URL */
		const char *url_start = msg + strlen(RTSP_METHOD_DESCRIBE) + 1;
		const char *url_end = strchr(url_start, ' ');
		if (url_end) {
			size_t url_len = url_end - url_start;
			if (url_len < sizeof(session->url))
				strncpy(session->url, url_start, url_len);
		}
		return 1;
	}

	return 0;
}

/*
 * Compare flows, ignoring port direction (for matching client/server)
 */
static int flows_match_bidirectional(const struct flow *a, const struct flow *b)
{
	if (a->ethertype != b->ethertype || a->proto != b->proto)
		return 0;

	if (a->ethertype == 0x0800) { /* IPv4 */
		/* Check both directions */
		if (a->src_ip.s_addr == b->src_ip.s_addr &&
		    a->dst_ip.s_addr == b->dst_ip.s_addr &&
		    a->sport == b->sport && a->dport == b->dport)
			return 1;
		if (a->src_ip.s_addr == b->dst_ip.s_addr &&
		    a->dst_ip.s_addr == b->src_ip.s_addr &&
		    a->sport == b->dport && a->dport == b->sport)
			return 1;
	}
	/* TODO: IPv6 */

	return 0;
}

/* Public API */

void rtsp_tap_init(struct rtsp_tap_state *state)
{
	memset(state, 0, sizeof(*state));
}

int rtsp_tap_process_packet(struct rtsp_tap_state *state,
                            const struct flow *flow,
                            const uint8_t *payload, size_t len,
                            uint64_t timestamp)
{
	if (!state || !flow || !payload || len == 0)
		return 0;

	/* Quick check: RTSP messages start with "RTSP/" (response) or method name */
	const char *data = (const char *)payload;
	if (len < 4)
		return 0;

	int is_rtsp = (strncmp(data, "RTSP", 4) == 0 ||
	               strncmp(data, "DESC", 4) == 0 ||
	               strncmp(data, "SETU", 4) == 0 ||
	               strncmp(data, "PLAY", 4) == 0 ||
	               strncmp(data, "PAUS", 4) == 0 ||
	               strncmp(data, "TEAR", 4) == 0 ||
	               strncmp(data, "OPTI", 4) == 0 ||
	               strncmp(data, "ANNO", 4) == 0 ||
	               strncmp(data, "GET_", 4) == 0 ||
	               strncmp(data, "SET_", 4) == 0);

	if (!is_rtsp)
		return 0;

	struct rtsp_session *session = find_or_create_session(state, flow);
	if (!session)
		return 0;

	return process_rtsp_message(session, data, len, timestamp);
}

const struct rtsp_media *rtsp_tap_find_media_for_rtp(
    struct rtsp_tap_state *state,
    const struct flow *rtp_flow,
    uint32_t ssrc)
{
	if (!state || !rtp_flow)
		return NULL;

	for (int i = 0; i < RTSP_MAX_SESSIONS; i++) {
		struct rtsp_session *s = &state->sessions[i];
		if (!s->active)
			continue;

		for (int j = 0; j < s->media_count; j++) {
			struct rtsp_media *m = &s->media[j];
			if (!m->valid)
				continue;

			/* Match by SSRC if we have it */
			if (m->ssrc && m->ssrc == ssrc)
				return m;

			/* Match by port: RTP flow dst port should match client_rtp_port,
			 * or src port should match server_rtp_port */
			if (rtp_flow->proto == 17) { /* UDP */
				/* Check if this RTP flow matches the RTSP control flow IPs */
				if (rtp_flow->ethertype == 0x0800) { /* IPv4 */
					/* Server -> Client direction */
					if (rtp_flow->src_ip.s_addr == s->control_flow.dst_ip.s_addr &&
					    rtp_flow->sport == m->server_rtp_port)
						return m;
					/* Client -> Server (less common for RTP) */
					if (rtp_flow->dst_ip.s_addr == s->control_flow.dst_ip.s_addr &&
					    rtp_flow->dport == m->server_rtp_port)
						return m;
				}
			}
		}
	}

	return NULL;
}

enum rtsp_state rtsp_tap_get_session_state(
    struct rtsp_tap_state *state,
    const struct flow *rtp_flow)
{
	if (!state || !rtp_flow)
		return RTSP_STATE_INIT;

	for (int i = 0; i < RTSP_MAX_SESSIONS; i++) {
		struct rtsp_session *s = &state->sessions[i];
		if (!s->active)
			continue;

		for (int j = 0; j < s->media_count; j++) {
			struct rtsp_media *m = &s->media[j];
			if (m->rtp_flow_linked &&
			    flows_match_bidirectional(&m->rtp_flow, rtp_flow))
				return s->state;
		}
	}

	return RTSP_STATE_INIT;
}

void rtsp_tap_cleanup(struct rtsp_tap_state *state,
                      uint64_t now_ns, uint64_t timeout_ns)
{
	if (!state)
		return;

	for (int i = 0; i < RTSP_MAX_SESSIONS; i++) {
		struct rtsp_session *s = &state->sessions[i];
		if (s->active) {
			if (now_ns - s->last_activity_ns > timeout_ns ||
			    s->state == RTSP_STATE_TEARDOWN) {
				s->active = 0;
				state->session_count--;
			}
		}
	}
}
