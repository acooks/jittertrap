/*
 * ws_compress.c - WebSocket message compression using zlib with preset dictionary
 *
 * Uses raw deflate with a dictionary optimized for JitterTrap JSON messages.
 * The dictionary contains common strings that appear in messages, allowing
 * better compression especially for smaller messages.
 */

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <syslog.h>

#include "ws_compress.h"

static int compress_initialized = 0;

/*
 * Preset dictionary for JitterTrap JSON messages.
 *
 * Dictionary should contain strings most likely to appear in messages,
 * with the most common strings at the END (zlib prioritizes end of dictionary).
 *
 * Structure: Less common -> More common (left to right)
 */
static const char ws_dictionary[] =
	/* Common IP prefixes and network values */
	"192.168.10.0.172.16.fe80:::ffff:"
	/* Protocol names */
	"ICMPUDPTCP"
	/* Traffic class */
	"BULKBECS0CS1"
	/* Less frequent message types */
	"pcap_readypcap_statuspcap_configpcap_triggersample_period"
	"netem_paramsdev_selectiface_list"
	/* Video telemetry fields (less common - only video flows) */
	"\"video_codec_source\":"
	"\"video_bitrate_kbps\":"
	"\"video_gop_frames\":"
	"\"video_keyframes\":"
	"\"video_frames\":"
	"\"video_fps_x100\":"
	"\"video_profile\":"
	"\"video_level\":"
	"\"video_width\":"
	"\"video_height\":"
	"\"video_jitter_hist\":["
	"\"video_jitter_us\":"
	"\"video_seq_loss\":"
	"\"video_cc_errors\":"
	"\"video_codec\":"
	"\"video_ssrc\":"
	"\"video_type\":"
	/* Audio telemetry fields (less common - only audio flows) */
	"\"audio_bitrate_kbps\":"
	"\"audio_sample_rate\":"
	"\"audio_jitter_us\":"
	"\"audio_seq_loss\":"
	"\"audio_codec\":"
	"\"audio_ssrc\":"
	"\"audio_type\":"
	/* TCP congestion/window fields */
	"\"recent_events\":"
	"\"retransmit_cnt\":"
	"\"zero_window_cnt\":"
	"\"dup_ack_cnt\":"
	"\"ece_cnt\":"
	"\"window_scale\":"
	"\"rwnd_bytes\":"
	"\"saw_syn\":"
	"\"tcp_state\":"
	"\"rtt_us\":"
	/* TCP health indicator fields */
	"\"health_rtt_hist\":["
	"\"health_rtt_samples\":"
	"\"health_status\":"
	"\"health_flags\":"
	/* IPG histogram fields (medium frequency) */
	"\"ipg_hist\":["
	"\"ipg_samples\":"
	"\"ipg_mean_us\":"
	/* Frame size histogram fields (all flows) */
	"\"frame_size_hist\":["
	"\"frame_size_samples\":"
	"\"frame_size_variance\":"
	"\"frame_size_mean\":"
	"\"frame_size_min\":"
	"\"frame_size_max\":"
	/* PPS histogram fields (all flows) */
	"\"pps_hist\":["
	"\"pps_samples\":"
	"\"pps_variance\":"
	"\"pps_mean\":"
	/* Address fields */
	"\"tclass\":\""
	"\"proto\":\""
	"\"dport\":"
	"\"sport\":"
	"\"dst\":\""
	"\"src\":\""
	/* Stats message fields */
	"\"mean_tx_packet_gap\":"
	"\"mean_rx_packet_gap\":"
	"\"max_tx_packet_gap\":"
	"\"max_rx_packet_gap\":"
	"\"min_tx_packet_gap\":"
	"\"min_rx_packet_gap\":"
	"\"sd_whoosh\":"
	"\"max_whoosh\":"
	"\"mean_whoosh\":"
	"\"mean_tx_packets\":"
	"\"mean_rx_packets\":"
	"\"mean_tx_bytes\":"
	"\"mean_rx_bytes\":"
	"\"interval_ns\":"
	/* Toptalk aggregate fields */
	"\"tpackets\":"
	"\"tbytes\":"
	"\"tflows\":"
	/* Most common field names */
	"\"packets\":"
	"\"bytes\":"
	"\"flows\":["
	/* Common histogram patterns (zeros are very frequent) */
	",0,0,0,0"
	"[0,0,0,0"
	/* Most common JSON structure */
	"\"iface\":\""
	"\",\"p\":{"
	"\"msg\":\""
	/* Most frequent message types (at the very end for priority) */
	"toptalk"
	"stats"
	"}}";

int ws_compress_init(void)
{
	compress_initialized = 1;
	syslog(LOG_INFO,
	       "WebSocket compression enabled (threshold %d bytes, dictionary %zu bytes)",
	       WS_COMPRESS_THRESHOLD, sizeof(ws_dictionary) - 1);
	return 0;
}

int ws_should_compress(size_t msg_len)
{
	return compress_initialized &&
	       (msg_len > WS_COMPRESS_THRESHOLD) &&
	       (msg_len <= WS_COMPRESS_MAX_INPUT);
}

const char *ws_compress_get_dictionary(size_t *len)
{
	if (len) {
		*len = sizeof(ws_dictionary) - 1;  /* exclude null terminator */
	}
	return ws_dictionary;
}

int ws_compress(const char *in, size_t in_len, unsigned char **out,
                size_t *out_len)
{
	z_stream strm;
	unsigned char *compressed;
	size_t compressed_size;
	int ret;

	if (!in || !out || !out_len || in_len == 0) {
		return -1;
	}

	/* Estimate compressed size - worst case is slightly larger + header */
	compressed_size = compressBound(in_len) + 32;

	/* Allocate buffer: 1 byte header + compressed data */
	compressed = malloc(1 + compressed_size);
	if (!compressed) {
		syslog(LOG_ERR, "ws_compress: malloc failed");
		return -1;
	}

	/* Set header byte */
	compressed[0] = WS_COMPRESS_HEADER;

	/* Initialize zlib for raw deflate (no header/trailer) */
	memset(&strm, 0, sizeof(strm));
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;

	/* windowBits = -15 for raw deflate (negative = no zlib header) */
	ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
	                   -15, 8, Z_DEFAULT_STRATEGY);
	if (ret != Z_OK) {
		syslog(LOG_ERR, "ws_compress: deflateInit2 failed: %d", ret);
		free(compressed);
		return -1;
	}

	/* Set the preset dictionary */
	ret = deflateSetDictionary(&strm,
	                           (const unsigned char *)ws_dictionary,
	                           sizeof(ws_dictionary) - 1);
	if (ret != Z_OK) {
		syslog(LOG_ERR, "ws_compress: deflateSetDictionary failed: %d", ret);
		deflateEnd(&strm);
		free(compressed);
		return -1;
	}

	strm.next_in = (unsigned char *)in;
	strm.avail_in = in_len;
	strm.next_out = compressed + 1;  /* Skip header byte */
	strm.avail_out = compressed_size;

	ret = deflate(&strm, Z_FINISH);
	if (ret != Z_STREAM_END) {
		syslog(LOG_ERR, "ws_compress: deflate failed: %d", ret);
		deflateEnd(&strm);
		free(compressed);
		return -1;
	}

	*out_len = 1 + strm.total_out;  /* Header + compressed data */
	deflateEnd(&strm);

	/* Check if compression is beneficial (at least 10% reduction) */
	if (*out_len >= in_len * 9 / 10) {
		free(compressed);
		return 1;  /* Not beneficial, use original */
	}

	*out = compressed;
	return 0;
}
