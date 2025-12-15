/*
 * video_detect.c - RTP and MPEG-TS stream detection
 *
 * Detects video streams via header inspection (not port heuristics).
 */

#include <string.h>
#include <arpa/inet.h>
#include "video_detect.h"
#include "flow.h"

/* H.264 NAL unit types */
#define H264_NAL_SLICE     1   /* Non-IDR slice */
#define H264_NAL_IDR       5   /* IDR slice (keyframe) */
#define H264_NAL_SPS       7   /* Sequence Parameter Set */
#define H264_NAL_PPS       8   /* Picture Parameter Set */
#define H264_NAL_STAP_A   24   /* Single-time aggregation packet */
#define H264_NAL_FU_A     28   /* Fragmentation unit */

/* H.265 NAL unit types */
#define H265_NAL_VPS      32   /* Video Parameter Set */
#define H265_NAL_SPS      33   /* Sequence Parameter Set */
#define H265_NAL_PPS      34   /* Picture Parameter Set */
#define H265_NAL_AP       48   /* Aggregation Packet */
#define H265_NAL_FU       49   /* Fragmentation Unit */

/* H.265 profile values (profile_idc) */
#define H265_PROFILE_MAIN              1
#define H265_PROFILE_MAIN_10           2
#define H265_PROFILE_MAIN_STILL        3
#define H265_PROFILE_REXT              4   /* Range Extensions */
#define H265_PROFILE_HIGH_THROUGHPUT   5
#define H265_PROFILE_MULTIVIEW_MAIN    6
#define H265_PROFILE_SCALABLE_MAIN     7
#define H265_PROFILE_3D_MAIN           8
#define H265_PROFILE_SCREEN_CONTENT    9
#define H265_PROFILE_SCALABLE_REXT    10

/*
 * Bitstream reader utilities for SPS parsing (exponential-golomb decoding)
 */
struct bitstream {
	const uint8_t *data;
	size_t size;
	size_t bit_pos;
};

static int bs_read_bit(struct bitstream *bs)
{
	if (bs->bit_pos / 8 >= bs->size) return 0;
	int bit = (bs->data[bs->bit_pos / 8] >> (7 - (bs->bit_pos % 8))) & 1;
	bs->bit_pos++;
	return bit;
}

static uint32_t bs_read_bits(struct bitstream *bs, int n)
{
	uint32_t val = 0;
	for (int i = 0; i < n; i++) {
		val = (val << 1) | bs_read_bit(bs);
	}
	return val;
}

/* Read unsigned exp-golomb coded value */
static uint32_t bs_read_ue(struct bitstream *bs)
{
	int leading_zeros = 0;
	while (bs_read_bit(bs) == 0 && leading_zeros < 32) {
		leading_zeros++;
	}
	if (leading_zeros == 0) return 0;
	return (1 << leading_zeros) - 1 + bs_read_bits(bs, leading_zeros);
}

/*
 * Remove emulation prevention bytes (0x03) from NAL unit.
 * In H.264/H.265, the byte sequence 0x000003 is inserted to prevent
 * accidental start code emulation. We need to remove these before parsing.
 *
 * Returns the new length after removal.
 */
static size_t remove_emulation_prevention_bytes(const uint8_t *src, size_t src_len,
                                                 uint8_t *dst, size_t dst_size)
{
	size_t si = 0, di = 0;
	while (si < src_len && di < dst_size) {
		if (si + 2 < src_len && src[si] == 0 && src[si+1] == 0 && src[si+2] == 3) {
			/* Copy two zeros, skip the 0x03 */
			dst[di++] = 0;
			if (di < dst_size) dst[di++] = 0;
			si += 3;
		} else {
			dst[di++] = src[si++];
		}
	}
	return di;
}

/*
 * Parse H.264 SPS to extract resolution and codec parameters.
 * This is a simplified parser that extracts the essential fields.
 *
 * @param sps      Pointer to SPS NAL unit (including NAL header)
 * @param len      Length of SPS data
 * @param width    Output: Video width in pixels
 * @param height   Output: Video height in pixels
 *
 * @return 0 on success, -1 on failure
 */
static int parse_sps_resolution(const uint8_t *sps, size_t len, int *width, int *height)
{
	if (len < 5) return -1;

	struct bitstream bs = { .data = sps + 1, .size = len - 1, .bit_pos = 0 };

	/* Skip profile_idc (8 bits) */
	bs_read_bits(&bs, 8);
	/* Skip constraint_set flags and reserved (8 bits) */
	bs_read_bits(&bs, 8);
	/* Skip level_idc (8 bits) */
	bs_read_bits(&bs, 8);
	/* Skip seq_parameter_set_id */
	bs_read_ue(&bs);

	uint8_t profile_idc = sps[1];

	/* High profile has additional fields */
	if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
	    profile_idc == 244 || profile_idc == 44 || profile_idc == 83 ||
	    profile_idc == 86 || profile_idc == 118 || profile_idc == 128) {
		uint32_t chroma_format_idc = bs_read_ue(&bs);
		if (chroma_format_idc == 3) {
			bs_read_bit(&bs);  /* separate_colour_plane_flag */
		}
		bs_read_ue(&bs);  /* bit_depth_luma_minus8 */
		bs_read_ue(&bs);  /* bit_depth_chroma_minus8 */
		bs_read_bit(&bs);  /* qpprime_y_zero_transform_bypass_flag */
		int seq_scaling_matrix_present_flag = bs_read_bit(&bs);
		if (seq_scaling_matrix_present_flag) {
			int count = (chroma_format_idc != 3) ? 8 : 12;
			for (int i = 0; i < count; i++) {
				if (bs_read_bit(&bs)) {  /* seq_scaling_list_present_flag */
					/* Skip scaling list - simplified, just break */
					int size = (i < 6) ? 16 : 64;
					for (int j = 0; j < size; j++) {
						bs_read_ue(&bs);
						break;  /* Can't parse this fully */
					}
				}
			}
		}
	}

	/* log2_max_frame_num_minus4 */
	bs_read_ue(&bs);
	/* pic_order_cnt_type */
	uint32_t poc_type = bs_read_ue(&bs);
	if (poc_type == 0) {
		bs_read_ue(&bs);  /* log2_max_pic_order_cnt_lsb_minus4 */
	} else if (poc_type == 1) {
		bs_read_bit(&bs);  /* delta_pic_order_always_zero_flag */
		bs_read_ue(&bs);  /* offset_for_non_ref_pic */
		bs_read_ue(&bs);  /* offset_for_top_to_bottom_field */
		uint32_t num_ref_frames_in_pic_order_cnt_cycle = bs_read_ue(&bs);
		for (uint32_t i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++) {
			bs_read_ue(&bs);  /* offset_for_ref_frame */
		}
	}
	/* num_ref_frames */
	bs_read_ue(&bs);
	/* gaps_in_frame_num_value_allowed_flag */
	bs_read_bit(&bs);
	/* pic_width_in_mbs_minus1 */
	uint32_t pic_width_in_mbs = bs_read_ue(&bs) + 1;
	/* pic_height_in_map_units_minus1 */
	uint32_t pic_height_in_map_units = bs_read_ue(&bs) + 1;

	/* frame_mbs_only_flag */
	int frame_mbs_only = bs_read_bit(&bs);
	if (!frame_mbs_only) {
		bs_read_bit(&bs);  /* mb_adaptive_frame_field_flag */
	}

	/* direct_8x8_inference_flag */
	bs_read_bit(&bs);

	/* Cropping info */
	int crop_left = 0, crop_right = 0, crop_top = 0, crop_bottom = 0;
	int frame_cropping_flag = bs_read_bit(&bs);
	if (frame_cropping_flag) {
		crop_left = bs_read_ue(&bs);
		crop_right = bs_read_ue(&bs);
		crop_top = bs_read_ue(&bs);
		crop_bottom = bs_read_ue(&bs);
	}

	/* Calculate resolution */
	int w = pic_width_in_mbs * 16;
	int h = (2 - frame_mbs_only) * pic_height_in_map_units * 16;

	/* Apply cropping (simplified - assumes 4:2:0) */
	w -= (crop_left + crop_right) * 2;
	h -= (crop_top + crop_bottom) * 2 * (2 - frame_mbs_only);

	/* Sanity check - minimum 64x64 to reject garbage data */
	if (w < 64 || w > 8192 || h < 64 || h > 8192)
		return -1;

	*width = w;
	*height = h;

	return 0;
}

/*
 * RTP payload types for video (RFC 3551 and dynamic)
 * Static types (0-34 are well-defined):
 *   26 = JPEG
 *   28 = nv (Xerox PARC)
 *   31 = H.261
 *   32 = MPV (MPEG-1/2 Video)
 *   34 = H.263
 *
 * Dynamic types (96-127) are commonly used for:
 *   H.264, H.265, VP8, VP9, AV1
 */
static int is_video_payload_type(uint8_t pt)
{
	/* Static video payload types */
	if (pt == 26 || pt == 28 || pt == 31 || pt == 32 || pt == 34)
		return 1;

	/* Dynamic payload types (96-127) - commonly used for modern codecs */
	if (pt >= 96 && pt <= 127)
		return 1;

	return 0;
}

/*
 * Validate RTP header structure.
 * Returns 1 if valid RTP, 0 otherwise.
 */
static int validate_rtp_header(const struct hdr_rtp *rtp, size_t payload_len)
{
	/* Check RTP version (must be 2) */
	if (RTP_VERSION(rtp) != 2)
		return 0;

	/* Payload type must be valid (0-127) */
	uint8_t pt = RTP_PAYLOAD_TYPE(rtp);
	if (pt > 127)
		return 0;

	/* CSRC count must not exceed remaining packet size */
	uint8_t cc = RTP_CSRC_COUNT(rtp);
	size_t min_size = RTP_HDR_MIN_SIZE + (cc * 4);
	if (payload_len < min_size)
		return 0;

	/* Check for header extension */
	if (RTP_EXTENSION(rtp)) {
		/* Extension header is 4 bytes minimum after CSRC list */
		if (payload_len < min_size + 4)
			return 0;
	}

	/* SSRC of 0 is suspicious but technically valid */
	/* Very common SSRCs like 0 or 0xFFFFFFFF could be random data */
	uint32_t ssrc = ntohl(rtp->ssrc);
	if (ssrc == 0 || ssrc == 0xFFFFFFFF)
		return 0;

	return 1;
}

/*
 * Extract H.264 SPS profile and level from NAL unit.
 * SPS format after NAL header: profile_idc (1) | constraint_flags (1) | level_idc (1) | ...
 *
 * For STAP-A, SPS may be aggregated with PPS.
 * For FU-A, we need to see start fragment to get profile/level.
 *
 * Returns 1 if SPS found and params extracted, 0 otherwise.
 */
static int extract_h264_sps_params(const uint8_t *rtp_payload, size_t len,
                                   uint8_t *profile, uint8_t *level,
                                   int *width, int *height)
{
	if (!rtp_payload || len < 2 || !profile || !level)
		return 0;

	uint8_t first = rtp_payload[0];
	uint8_t nal_type = first & 0x1F;

	/* Single NAL unit: SPS (type 7) */
	if (nal_type == H264_NAL_SPS && len >= 4) {
		*profile = rtp_payload[1];
		/* Skip constraint_set flags at byte 2 */
		*level = rtp_payload[3];
		/* Parse resolution from SPS */
		if (width && height) {
			parse_sps_resolution(rtp_payload, len, width, height);
		}
		return 1;
	}

	/* STAP-A (type 24): may contain SPS */
	if (nal_type == H264_NAL_STAP_A && len >= 6) {
		/* STAP-A format: [NAL header (1)] [NALU size (2)] [NALU data] ... */
		size_t offset = 1;
		while (offset + 2 < len) {
			uint16_t nalu_size = (rtp_payload[offset] << 8) | rtp_payload[offset + 1];
			offset += 2;

			if (offset + nalu_size > len || nalu_size < 1)
				break;

			uint8_t inner_type = rtp_payload[offset] & 0x1F;
			if (inner_type == H264_NAL_SPS && nalu_size >= 4) {
				*profile = rtp_payload[offset + 1];
				*level = rtp_payload[offset + 3];
				/* Parse resolution from inner SPS NAL */
				if (width && height) {
					parse_sps_resolution(rtp_payload + offset, nalu_size, width, height);
				}
				return 1;
			}

			offset += nalu_size;
		}
	}

	/* FU-A (type 28): check for SPS start fragment */
	if (nal_type == H264_NAL_FU_A && len >= 6) {
		uint8_t fu_header = rtp_payload[1];
		int start_bit = (fu_header >> 7) & 1;
		uint8_t inner_type = fu_header & 0x1F;

		/* Start fragment of SPS */
		if (start_bit && inner_type == H264_NAL_SPS && len >= 5) {
			/* FU-A: [indicator (1)] [header (1)] [payload...] */
			/* SPS payload starts at byte 2 */
			*profile = rtp_payload[2];
			*level = rtp_payload[4];
			/*
			 * Note: FU-A SPS resolution parsing is incomplete.
			 * The SPS NAL is fragmented across multiple packets,
			 * so we'd need to reassemble it first. For now, we
			 * only extract resolution from complete SPS NALs.
			 */
			return 1;
		}
	}

	return 0;
}

/*
 * Extract H.265 profile/tier/level and resolution from SPS NAL unit.
 *
 * H.265 NAL header is 2 bytes:
 *   Byte 0: F(1) | Type(6) | LayerId_high(1)
 *   Byte 1: LayerId_low(5) | TID(3)
 *
 * SPS structure (after NAL header):
 *   sps_video_parameter_set_id (4 bits)
 *   sps_max_sub_layers_minus1 (3 bits)
 *   sps_temporal_id_nesting_flag (1 bit)
 *   profile_tier_level(1, sps_max_sub_layers_minus1) - contains profile/level
 *   sps_seq_parameter_set_id (ue)
 *   chroma_format_idc (ue)
 *   if (chroma_format_idc == 3) separate_colour_plane_flag (1)
 *   pic_width_in_luma_samples (ue)   <-- RESOLUTION WIDTH
 *   pic_height_in_luma_samples (ue)  <-- RESOLUTION HEIGHT
 *   conformance_window_flag (1)
 *   if (conformance_window_flag) crop offsets (4 ue values)
 *   ...
 *
 * profile_tier_level structure:
 *   general_profile_space (2 bits)
 *   general_tier_flag (1 bit)
 *   general_profile_idc (5 bits)
 *   general_profile_compatibility_flags (32 bits)
 *   general_progressive_source_flag (1 bit)
 *   ... more flags ...
 *   general_level_idc (8 bits) at byte offset ~12
 *
 * Returns 1 if params extracted, 0 otherwise.
 */
static int extract_h265_sps_params(const uint8_t *sps_nal, size_t len,
                                   uint8_t *profile, uint8_t *level,
                                   int *width, int *height)
{
	if (!sps_nal || len < 15 || !profile || !level)
		return 0;

	/*
	 * Remove emulation prevention bytes before parsing.
	 * The sequence 0x000003 is inserted in NAL units to prevent start code
	 * emulation; we need to remove the 0x03 bytes before bitstream parsing.
	 */
	uint8_t rbsp[256];
	size_t rbsp_len = remove_emulation_prevention_bytes(sps_nal + 2, len - 2,
	                                                     rbsp, sizeof(rbsp));
	if (rbsp_len < 13)
		return 0;

	struct bitstream bs = { .data = rbsp, .size = rbsp_len, .bit_pos = 0 };

	/* sps_video_parameter_set_id (4 bits) */
	bs_read_bits(&bs, 4);

	/* sps_max_sub_layers_minus1 (3 bits) */
	uint32_t max_sub_layers = bs_read_bits(&bs, 3) + 1;

	/* sps_temporal_id_nesting_flag (1 bit) */
	bs_read_bits(&bs, 1);

	/* profile_tier_level() */
	/* general_profile_space (2 bits) */
	bs_read_bits(&bs, 2);

	/* general_tier_flag (1 bit) - 0=Main, 1=High tier */
	uint8_t tier = bs_read_bits(&bs, 1);

	/* general_profile_idc (5 bits) */
	*profile = bs_read_bits(&bs, 5);

	/* general_profile_compatibility_flags (32 bits) */
	bs_read_bits(&bs, 32);

	/* Skip constraint flags (48 bits total) */
	/* general_progressive_source_flag through general_inbld_flag = 16 bits */
	/* Then 32 bits of reserved_zero_bits (depending on profile) */
	bs_read_bits(&bs, 48);

	/* general_level_idc (8 bits) */
	*level = bs_read_bits(&bs, 8);

	/* Encode tier in high bit of profile (for UI display) */
	/* Use 0x80 flag to indicate High tier */
	if (tier)
		*profile |= 0x80;

	/* Skip sub_layer profile/tier/level if max_sub_layers > 1 */
	if (max_sub_layers > 1) {
		/* sub_layer_profile_present_flag[i] and sub_layer_level_present_flag[i] */
		/* for i = 0 to max_sub_layers - 2 */
		for (uint32_t i = 0; i < max_sub_layers - 1; i++) {
			bs_read_bits(&bs, 2);  /* profile_present + level_present */
		}
		/* If max_sub_layers > 1, skip reserved bits */
		if (max_sub_layers > 1) {
			for (uint32_t i = max_sub_layers - 1; i < 8; i++) {
				bs_read_bits(&bs, 2);  /* reserved_zero_2bits */
			}
		}
		/* Skip sub_layer profile_tier_level data - simplified, assume no sub-layers */
	}

	/* sps_seq_parameter_set_id (ue) */
	bs_read_ue(&bs);

	/* chroma_format_idc (ue) */
	uint32_t chroma_format_idc = bs_read_ue(&bs);
	if (chroma_format_idc == 3) {
		bs_read_bit(&bs);  /* separate_colour_plane_flag */
	}

	/* pic_width_in_luma_samples (ue) - this is the actual resolution width */
	uint32_t pic_width = bs_read_ue(&bs);

	/* pic_height_in_luma_samples (ue) - this is the actual resolution height */
	uint32_t pic_height = bs_read_ue(&bs);

	/* conformance_window_flag (1 bit) */
	int conformance_window_flag = bs_read_bit(&bs);
	int crop_left = 0, crop_right = 0, crop_top = 0, crop_bottom = 0;
	if (conformance_window_flag) {
		crop_left = bs_read_ue(&bs);
		crop_right = bs_read_ue(&bs);
		crop_top = bs_read_ue(&bs);
		crop_bottom = bs_read_ue(&bs);
	}

	/* Calculate cropping unit based on chroma_format_idc */
	int sub_width_c = (chroma_format_idc == 1 || chroma_format_idc == 2) ? 2 : 1;
	int sub_height_c = (chroma_format_idc == 1) ? 2 : 1;

	/* Apply conformance window cropping */
	int w = pic_width - sub_width_c * (crop_left + crop_right);
	int h = pic_height - sub_height_c * (crop_top + crop_bottom);

	/* Sanity check resolution - only set if valid (64x64 to 8192x8192) */
	if (w >= 64 && w <= 8192 && h >= 64 && h <= 8192) {
		if (width) *width = w;
		if (height) *height = h;
	}
	/* Resolution is optional - profile/level were already extracted successfully */

	return 1;
}

/*
 * Extract H.265 SPS from RTP payload.
 * Handles single NAL, AP (aggregation), and FU (fragmentation) packets.
 *
 * Returns 1 if SPS found and params extracted, 0 otherwise.
 */
static int extract_h265_rtp_sps_params(const uint8_t *rtp_payload, size_t len,
                                       uint8_t *profile, uint8_t *level,
                                       int *width, int *height)
{
	if (!rtp_payload || len < 4 || !profile || !level)
		return 0;

	/* H.265 NAL header is 2 bytes */
	uint8_t nal_type = (rtp_payload[0] >> 1) & 0x3F;

	/* Single NAL unit: SPS (type 33) */
	if (nal_type == H265_NAL_SPS) {
		return extract_h265_sps_params(rtp_payload, len, profile, level,
		                               width, height);
	}

	/* AP (Aggregation Packet, type 48): may contain SPS */
	if (nal_type == H265_NAL_AP && len >= 6) {
		/* AP format: [NAL header (2)] [NALU size (2)] [NALU data] ... */
		size_t offset = 2;
		while (offset + 2 < len) {
			uint16_t nalu_size = (rtp_payload[offset] << 8) | rtp_payload[offset + 1];
			offset += 2;

			if (offset + nalu_size > len || nalu_size < 2)
				break;

			uint8_t inner_type = (rtp_payload[offset] >> 1) & 0x3F;
			if (inner_type == H265_NAL_SPS) {
				return extract_h265_sps_params(rtp_payload + offset, nalu_size,
				                               profile, level, width, height);
			}

			offset += nalu_size;
		}
	}

	/* FU (Fragmentation Unit, type 49): check for SPS start fragment */
	if (nal_type == H265_NAL_FU && len >= 5) {
		/* FU format: [PayloadHdr (2)] [FU header (1)] [payload...] */
		uint8_t fu_header = rtp_payload[2];
		int start_bit = (fu_header >> 7) & 1;
		uint8_t inner_type = fu_header & 0x3F;

		/* Start fragment of SPS - extract what we can */
		if (start_bit && inner_type == H265_NAL_SPS && len >= 15) {
			/* Reconstruct NAL header for SPS */
			uint8_t reconstructed[64];
			if (len - 3 > sizeof(reconstructed) - 2)
				return 0;

			/* Build NAL header for SPS (type 33) */
			reconstructed[0] = (inner_type << 1) | (rtp_payload[0] & 0x81);
			reconstructed[1] = rtp_payload[1];
			memcpy(reconstructed + 2, rtp_payload + 3, len - 3);

			return extract_h265_sps_params(reconstructed, len - 1,
			                               profile, level, width, height);
		}
	}

	return 0;
}

/*
 * Check if RTP payload contains a keyframe (IDR for H.264/H.265).
 * Returns 1 if keyframe detected, 0 otherwise.
 */
static int is_h264_keyframe(const uint8_t *rtp_payload, size_t len)
{
	if (!rtp_payload || len < 2)
		return 0;

	uint8_t first = rtp_payload[0];
	uint8_t nal_type = first & 0x1F;

	/* Single NAL unit: IDR (type 5) */
	if (nal_type == H264_NAL_IDR)
		return 1;

	/* STAP-A: check for IDR in aggregation */
	if (nal_type == H264_NAL_STAP_A && len >= 4) {
		size_t offset = 1;
		while (offset + 2 < len) {
			uint16_t nalu_size = (rtp_payload[offset] << 8) | rtp_payload[offset + 1];
			offset += 2;

			if (offset + nalu_size > len || nalu_size < 1)
				break;

			uint8_t inner_type = rtp_payload[offset] & 0x1F;
			if (inner_type == H264_NAL_IDR)
				return 1;

			offset += nalu_size;
		}
	}

	/* FU-A: check for IDR fragment start */
	if (nal_type == H264_NAL_FU_A && len >= 2) {
		uint8_t fu_header = rtp_payload[1];
		int start_bit = (fu_header >> 7) & 1;
		uint8_t inner_type = fu_header & 0x1F;

		if (start_bit && inner_type == H264_NAL_IDR)
			return 1;
	}

	return 0;
}

int video_detect_rtp(const uint8_t *payload, size_t payload_len,
                     struct rtp_info *rtp_info)
{
	if (!payload || payload_len < RTP_HDR_MIN_SIZE || !rtp_info)
		return 0;

	const struct hdr_rtp *rtp = (const struct hdr_rtp *)payload;

	if (!validate_rtp_header(rtp, payload_len))
		return 0;

	uint8_t pt = RTP_PAYLOAD_TYPE(rtp);

	/* Only track streams with video-like payload types */
	if (!is_video_payload_type(pt))
		return 0;

	/* Extract RTP header fields */
	rtp_info->payload_type = pt;
	rtp_info->last_seq = ntohs(rtp->seq);
	rtp_info->ssrc = ntohl(rtp->ssrc);
	rtp_info->rtp_timestamp = ntohl(rtp->timestamp);
	rtp_info->jitter_us = 0;
	rtp_info->seq_loss = 0;
	rtp_info->packets_seen = 1;
	/* Initialize fields that may not be set if SPS is not found */
	rtp_info->codec = VIDEO_CODEC_UNKNOWN;
	rtp_info->profile_idc = 0;
	rtp_info->level_idc = 0;
	rtp_info->width = 0;
	rtp_info->height = 0;

	/* Calculate payload offset (skip CSRC and extension) */
	size_t hdr_size = RTP_HDR_MIN_SIZE + (RTP_CSRC_COUNT(rtp) * 4);
	if (RTP_EXTENSION(rtp) && payload_len > hdr_size + 4) {
		/* Extension length is in 32-bit words at offset 2-3 */
		uint16_t ext_len = ntohs(*(uint16_t *)(payload + hdr_size + 2));
		hdr_size += 4 + (ext_len * 4);
	}

	/* Detect codec from RTP payload - ensure we don't underflow */
	if (payload_len <= hdr_size) {
		return VIDEO_STREAM_RTP;  /* Valid RTP but no payload to inspect */
	}

	const uint8_t *rtp_payload = payload + hdr_size;
	size_t rtp_payload_len = payload_len - hdr_size;

	if (rtp_payload_len > 0) {
		rtp_info->codec = video_detect_rtp_codec(rtp_payload,
		                                         rtp_payload_len,
		                                         pt);

		/* Mark as in-band detected when we identify a known codec */
		if (rtp_info->codec != VIDEO_CODEC_UNKNOWN) {
			rtp_info->codec_source = CODEC_SRC_INBAND;
		}

		/* For H.264, try to extract SPS profile/level and resolution */
		if (rtp_info->codec == VIDEO_CODEC_H264) {
			uint8_t profile = 0, level = 0;
			int width = 0, height = 0;
			if (extract_h264_sps_params(rtp_payload, rtp_payload_len,
			                            &profile, &level, &width, &height)) {
				rtp_info->profile_idc = profile;
				rtp_info->level_idc = level;
				if (width > 0 && height > 0) {
					rtp_info->width = (uint16_t)width;
					rtp_info->height = (uint16_t)height;
				}
			}
		}
		/* For H.265, try to extract SPS profile/tier/level and resolution */
		else if (rtp_info->codec == VIDEO_CODEC_H265) {
			uint8_t profile = 0, level = 0;
			int width = 0, height = 0;
			if (extract_h265_rtp_sps_params(rtp_payload, rtp_payload_len,
			                                &profile, &level, &width, &height)) {
				rtp_info->profile_idc = profile;
				rtp_info->level_idc = level;
				if (width > 0 && height > 0) {
					rtp_info->width = (uint16_t)width;
					rtp_info->height = (uint16_t)height;
				}
			}
		}
	} else {
		rtp_info->codec = VIDEO_CODEC_UNKNOWN;
	}

	return 1;
}

int video_detect_mpegts(const uint8_t *payload, size_t payload_len,
                        struct mpegts_info *ts_info)
{
	if (!payload || !ts_info)
		return 0;

	/* MPEG-TS packets are 188 bytes; UDP payload should be multiple */
	if (payload_len < MPEGTS_PACKET_SIZE)
		return 0;

	/* Check sync bytes at expected positions */
	int sync_count = 0;
	int ts_packets = payload_len / MPEGTS_PACKET_SIZE;

	/* Verify at least first few packets have sync byte */
	int check_count = (ts_packets > 4) ? 4 : ts_packets;
	for (int i = 0; i < check_count; i++) {
		if (payload[i * MPEGTS_PACKET_SIZE] == MPEGTS_SYNC_BYTE)
			sync_count++;
	}

	/* Require all checked packets to have sync byte */
	if (sync_count < check_count)
		return 0;

	/* Valid MPEG-TS detected - initialize info */
	memset(ts_info, 0, sizeof(*ts_info));
	ts_info->packets_seen = ts_packets;

	/* Scan for video PID by looking at adaptation field and stream type */
	for (int i = 0; i < ts_packets; i++) {
		const struct hdr_mpegts *ts =
		    (const struct hdr_mpegts *)(payload + i * MPEGTS_PACKET_SIZE);

		uint16_t pid = MPEGTS_PID(ts);

		/* Skip PAT, CAT, and null packets */
		if (pid == MPEGTS_PID_PAT || pid == MPEGTS_PID_CAT ||
		    pid == MPEGTS_PID_NULL)
			continue;

		/* Record this as potential video PID if not already set */
		if (ts_info->video_pid == 0 && pid > 0x20 && pid < 0x1FFF) {
			ts_info->video_pid = pid;

			/* Try to detect codec from payload */
			const uint8_t *ts_payload = payload + i * MPEGTS_PACKET_SIZE;
			ts_info->codec = video_detect_ts_codec(ts_payload,
			                                       MPEGTS_PACKET_SIZE);
		}
	}

	return 1;
}

enum video_stream_type video_detect(const uint8_t *payload, size_t payload_len,
                                    struct flow_video_info *video_info)
{
	if (!payload || !video_info)
		return VIDEO_STREAM_NONE;

	memset(video_info, 0, sizeof(*video_info));

	/* Try RTP detection first (more common for video streaming) */
	if (video_detect_rtp(payload, payload_len, &video_info->rtp)) {
		video_info->stream_type = VIDEO_STREAM_RTP;
		return VIDEO_STREAM_RTP;
	}

	/* Try MPEG-TS detection */
	if (video_detect_mpegts(payload, payload_len, &video_info->mpegts)) {
		video_info->stream_type = VIDEO_STREAM_MPEG_TS;
		return VIDEO_STREAM_MPEG_TS;
	}

	return VIDEO_STREAM_NONE;
}

/*
 * Detect H.264/H.265 from NAL unit header.
 * H.264 NAL: forbidden_zero_bit (1) | nal_ref_idc (2) | nal_unit_type (5)
 * H.265 NAL: forbidden_zero_bit (1) | nal_unit_type (6) | nuh_layer_id (6) | nuh_temporal_id_plus1 (3)
 *
 * RTP packetization (RFC 6184 for H.264, RFC 7798 for H.265):
 * H.264 RTP:
 *   Types 1-23: Single NAL unit
 *   Type 24: STAP-A (aggregation)
 *   Type 28: FU-A (fragmentation) - most common for video
 *
 * H.265 RTP:
 *   Types 0-47: Single NAL unit
 *   Type 48: AP (aggregation)
 *   Type 49: FU (fragmentation)
 *
 * Key differences:
 * - H.264: 1-byte NAL header, type in bits 0-4, ref_idc in bits 5-6
 * - H.265: 2-byte NAL header, type in bits 1-6 of byte 0, layer_id and tid in byte 1
 */
static enum video_codec detect_nal_codec(const uint8_t *payload, size_t len)
{
	if (len < 2)
		return VIDEO_CODEC_UNKNOWN;

	uint8_t first = payload[0];
	uint8_t second = payload[1];

	/* Forbidden zero bit must be 0 */
	if (first & 0x80)
		return VIDEO_CODEC_UNKNOWN;

	/* Get H.264 NAL type from first byte */
	uint8_t h264_type = first & H264_NAL_TYPE_MASK;

	/*
	 * Check for H.264 FU-A fragmentation unit (type 28) first.
	 * This is the most common packetization for H.264 video over RTP.
	 * FU-A format: [FU indicator (1)] [FU header (1)] [payload...]
	 * FU indicator has same format as NAL header.
	 * FU header: S(1) | E(1) | R(1) | Type(5)
	 */
	if (h264_type == 28) {
		/* FU-A detected - this is H.264 fragmentation */
		/* Extract actual NAL type from FU header (second byte, bits 0-4) */
		uint8_t fu_nal_type = second & H264_NAL_TYPE_MASK;
		if (fu_nal_type >= 1 && fu_nal_type <= 23) {
			return VIDEO_CODEC_H264;
		}
	}

	/* Check for H.264 STAP-A aggregation (type 24) */
	if (h264_type == 24) {
		return VIDEO_CODEC_H264;
	}

	/*
	 * Check for H.265 FU (fragmentation unit, type 49) or AP (aggregation, type 48)
	 * H.265 NAL type is in bits 1-6 of first byte.
	 */
	uint8_t h265_type = (first >> 1) & H265_NAL_TYPE_MASK;
	if (h265_type == 49 || h265_type == 48) {
		/* H.265 fragmentation or aggregation detected */
		return VIDEO_CODEC_H265;
	}

	/*
	 * Check for H.265 VPS/SPS/PPS (types 32-40) BEFORE H.264 heuristics.
	 * These H.265 NAL types, when interpreted as H.264, look like normal
	 * H.264 slice types (1-5) with valid ref_idc, causing false detection.
	 *
	 * H.265 SPS example: 0x42 0x01
	 * - H.265: type = (0x42 >> 1) & 0x3F = 33 (SPS), tid = 0x01 & 0x07 = 1 ✓
	 * - H.264: type = 0x42 & 0x1F = 2 (slice), ref_idc = 2 → would match H.264!
	 */
	if (h265_type >= 32 && h265_type <= 40) {
		uint8_t tid = second & 0x07;  /* temporal_id_plus1 */
		uint8_t layer_id = ((first & 0x01) << 5) | ((second >> 3) & 0x1F);

		/* H.265 requires tid in 1-7 and typically layer_id=0 */
		if (tid >= 1 && tid <= 7 && layer_id == 0) {
			return VIDEO_CODEC_H265;
		}
	}

	/*
	 * For single NAL units, try to distinguish H.264 from H.265.
	 *
	 * H.264 single NAL: type 1-23 in bits 0-4
	 * H.265 single NAL: type 0-47 in bits 1-6, plus 2-byte header
	 *
	 * H.265 has additional constraints on second byte (nuh_layer_id + tid):
	 * - temporal_id_plus1 (bits 0-2) must be 1-7 for valid streams
	 * - layer_id (bits 3-7 + bit 0 of first byte shifted) usually 0
	 */

	/* Check H.264 first for common single NAL types */
	if (h264_type >= 1 && h264_type <= 23) {
		/*
		 * Distinguish from H.265: check if second byte looks like
		 * H.265 nuh_layer_id/tid or H.264 NAL payload.
		 *
		 * For H.265: tid (bits 0-2) must be 1-7
		 * For H.264: second byte is start of NAL payload (varies)
		 *
		 * H.265 typically has tid=1 (0x01 in bits 0-2) for simple streams.
		 * If layer_id is 0, second byte would be 0x01.
		 *
		 * Use nal_ref_idc (bits 5-6) as additional H.264 hint:
		 * - For VCL NAL units (1-5), ref_idc is typically non-zero
		 * - For SPS/PPS (7,8), ref_idc is 3 (0x60)
		 */
		uint8_t ref_idc = (first >> 5) & 0x03;

		/* SPS (7) or PPS (8) with ref_idc=3 is definitely H.264 */
		if ((h264_type == 7 || h264_type == 8) && ref_idc == 3) {
			return VIDEO_CODEC_H264;
		}

		/* IDR slice (5) with non-zero ref_idc is likely H.264 */
		if (h264_type == 5 && ref_idc > 0) {
			return VIDEO_CODEC_H264;
		}

		/* Non-IDR slice (1) with reasonable ref_idc */
		if (h264_type == 1 && ref_idc <= 3) {
			return VIDEO_CODEC_H264;
		}

		/* For other types, assume H.264 if ref_idc pattern is consistent */
		if (h264_type >= 1 && h264_type <= 5 && ref_idc > 0) {
			return VIDEO_CODEC_H264;
		}
	}

	/*
	 * Try H.265 VCL types (0-31): must have valid tid in second byte.
	 * Note: H.265 types 32-40 (VPS/SPS/PPS) are already handled above.
	 */
	if (h265_type <= 31) {
		uint8_t tid = second & 0x07;  /* temporal_id_plus1 */
		uint8_t layer_id = ((first & 0x01) << 5) | ((second >> 3) & 0x1F);

		/*
		 * H.265 requires tid in 1-7 and typically layer_id=0.
		 * Accept any valid tid (1-7), not just tid=1.
		 * Second byte format: [layer_id_low(5 bits)][tid(3 bits)]
		 * For layer_id=0: second byte can be 0x01-0x07 (tid=1-7)
		 */
		if (tid >= 1 && tid <= 7 && layer_id == 0) {
			return VIDEO_CODEC_H265;
		}
	}

	/* Fallback: if we have a valid H.264 type, assume H.264 */
	if (h264_type >= 1 && h264_type <= 23) {
		return VIDEO_CODEC_H264;
	}

	return VIDEO_CODEC_UNKNOWN;
}

/*
 * Detect VP8/VP9 from payload descriptor.
 * VP8: First byte has specific bit pattern
 * VP9: More complex header with profile info
 */
static enum video_codec detect_vpx_codec(const uint8_t *payload, size_t len)
{
	if (len < 3)
		return VIDEO_CODEC_UNKNOWN;

	/*
	 * VP8 payload descriptor:
	 *   X R N S R PID (first byte for non-partitioned)
	 *   For keyframes, first 3 bytes of payload are: 0x9d 0x01 0x2a
	 */
	if (payload[0] == 0x9d && payload[1] == 0x01 && payload[2] == 0x2a) {
		return VIDEO_CODEC_VP8;
	}

	/*
	 * VP9 payload descriptor is more complex.
	 * Check for VP9 frame sync code: 0x49 0x83 0x42
	 */
	if (len >= 10) {
		/* Search for VP9 frame sync code in first bytes */
		for (size_t i = 0; i < len - 3 && i < 10; i++) {
			if (payload[i] == 0x49 && payload[i+1] == 0x83 &&
			    payload[i+2] == 0x42) {
				return VIDEO_CODEC_VP9;
			}
		}
	}

	return VIDEO_CODEC_UNKNOWN;
}

/*
 * Detect AV1 from OBU (Open Bitstream Unit) header.
 * AV1 OBU: obu_forbidden_bit (1) | obu_type (4) | obu_extension_flag (1) |
 *          obu_has_size_field (1) | obu_reserved_1bit (1)
 */
static enum video_codec detect_av1_codec(const uint8_t *payload, size_t len)
{
	if (len < 2)
		return VIDEO_CODEC_UNKNOWN;

	uint8_t first = payload[0];

	/* Forbidden bit must be 0 */
	if (first & 0x80)
		return VIDEO_CODEC_UNKNOWN;

	/* Extract OBU type (bits 4-7, but bit 7 is forbidden) */
	uint8_t obu_type = (first >> 3) & 0x0F;

	/* Valid OBU types: 1-8, 15 */
	if ((obu_type >= 1 && obu_type <= 8) || obu_type == 15) {
		/* Check reserved bit is 0 */
		if ((first & 0x01) == 0) {
			return VIDEO_CODEC_AV1;
		}
	}

	return VIDEO_CODEC_UNKNOWN;
}

enum video_codec video_detect_rtp_codec(const uint8_t *payload,
                                        size_t payload_len,
                                        uint8_t payload_type)
{
	if (!payload || payload_len < 2)
		return VIDEO_CODEC_UNKNOWN;

	/* Static payload types give us a hint */
	switch (payload_type) {
	case 31: /* H.261 - treat as H.264 family */
		return VIDEO_CODEC_H264;
	case 32: /* MPV - MPEG-1/2, not directly supported */
		return VIDEO_CODEC_UNKNOWN;
	case 34: /* H.263 - treat as H.264 family */
		return VIDEO_CODEC_H264;
	}

	/* For dynamic payload types, inspect the payload */
	enum video_codec codec;

	/* Try H.264/H.265 (most common) */
	codec = detect_nal_codec(payload, payload_len);
	if (codec != VIDEO_CODEC_UNKNOWN)
		return codec;

	/* Try VP8/VP9 */
	codec = detect_vpx_codec(payload, payload_len);
	if (codec != VIDEO_CODEC_UNKNOWN)
		return codec;

	/* Try AV1 */
	codec = detect_av1_codec(payload, payload_len);
	if (codec != VIDEO_CODEC_UNKNOWN)
		return codec;

	return VIDEO_CODEC_UNKNOWN;
}

enum video_codec video_detect_ts_codec(const uint8_t *payload,
                                       size_t payload_len)
{
	if (!payload || payload_len < MPEGTS_PACKET_SIZE)
		return VIDEO_CODEC_UNKNOWN;

	const struct hdr_mpegts *ts = (const struct hdr_mpegts *)payload;

	/* Skip header and adaptation field to get to payload */
	size_t offset = 4; /* TS header size */
	uint8_t afc = MPEGTS_AFC(ts);

	if (afc == 2 || afc == 3) {
		/* Adaptation field present */
		if (offset >= payload_len)
			return VIDEO_CODEC_UNKNOWN;
		uint8_t af_len = payload[offset];
		offset += 1 + af_len;
	}

	if (offset >= payload_len - 4)
		return VIDEO_CODEC_UNKNOWN;

	/* Check for PES start code (0x00 0x00 0x01) */
	if (MPEGTS_PUSI(ts)) {
		if (payload[offset] == 0x00 && payload[offset+1] == 0x00 &&
		    payload[offset+2] == 0x01) {
			uint8_t stream_id = payload[offset+3];

			/* Video stream IDs: 0xE0-0xEF */
			if (stream_id >= 0xE0 && stream_id <= 0xEF) {
				/* Skip PES header to find video data */
				if (offset + 9 < payload_len) {
					uint8_t pes_hdr_len = payload[offset+8];
					size_t video_offset = offset + 9 + pes_hdr_len;

					if (video_offset + 4 < payload_len) {
						/* Try to detect codec from video data */
						const uint8_t *video_data = payload + video_offset;
						size_t video_len = payload_len - video_offset;

						/* Check for H.264/H.265 start code */
						if (video_data[0] == 0x00 &&
						    video_data[1] == 0x00 &&
						    (video_data[2] == 0x01 ||
						     (video_data[2] == 0x00 && video_data[3] == 0x01))) {

							size_t nal_offset = (video_data[2] == 0x01) ? 3 : 4;
							if (nal_offset < video_len) {
								return detect_nal_codec(
								    video_data + nal_offset,
								    video_len - nal_offset);
							}
						}
					}
				}
			}
		}
	}

	return VIDEO_CODEC_UNKNOWN;
}

int video_detect_is_keyframe(const uint8_t *rtp_payload, size_t len,
                             enum video_codec codec)
{
	if (!rtp_payload || len < 2)
		return 0;

	/* H.264 keyframe detection */
	if (codec == VIDEO_CODEC_H264) {
		return is_h264_keyframe(rtp_payload, len);
	}

	/* H.265 keyframe detection */
	if (codec == VIDEO_CODEC_H265) {
		uint8_t first = rtp_payload[0];
		uint8_t h265_type = (first >> 1) & H265_NAL_TYPE_MASK;

		/* H.265 FU fragmentation */
		if (h265_type == 49 && len >= 3) {
			uint8_t fu_header = rtp_payload[2];
			int start_bit = (fu_header >> 7) & 1;
			uint8_t inner_type = fu_header & 0x3F;
			/* H.265 IDR types: 19 (IDR_W_RADL), 20 (IDR_N_LP) */
			if (start_bit && (inner_type == 19 || inner_type == 20))
				return 1;
		}
		/* H.265 single NAL: IDR types 19, 20 */
		if (h265_type == 19 || h265_type == 20)
			return 1;
	}

	return 0;
}

uint32_t video_detect_get_rtp_timestamp(const uint8_t *payload, size_t payload_len)
{
	if (!payload || payload_len < RTP_HDR_MIN_SIZE)
		return 0;

	const struct hdr_rtp *rtp = (const struct hdr_rtp *)payload;

	if (RTP_VERSION(rtp) != 2)
		return 0;

	return ntohl(rtp->timestamp);
}

/*
 * Audio detection functions - RFC 3551 static payload types
 */

/*
 * Check if RTP payload type is a static audio type.
 * RFC 3551 defines static audio payload types 0-23.
 */
static int is_audio_payload_type(uint8_t pt)
{
	switch (pt) {
	case 0:   /* PCMU - G.711 μ-law */
	case 8:   /* PCMA - G.711 A-law */
	case 18:  /* G729 */
		return 1;
	default:
		return 0;
	}
}

/*
 * Detect audio codec from RTP payload type.
 */
enum audio_codec audio_detect_codec(uint8_t payload_type)
{
	switch (payload_type) {
	case 0:
		return AUDIO_CODEC_PCMU;
	case 8:
		return AUDIO_CODEC_PCMA;
	case 18:
		return AUDIO_CODEC_G729;
	default:
		return AUDIO_CODEC_UNKNOWN;
	}
}

/*
 * Get sample rate in kHz for an audio codec.
 */
static uint8_t audio_sample_rate_khz(enum audio_codec codec)
{
	switch (codec) {
	case AUDIO_CODEC_PCMU:
	case AUDIO_CODEC_PCMA:
	case AUDIO_CODEC_G729:
		return 8;   /* 8 kHz */
	case AUDIO_CODEC_OPUS:
		return 48;  /* 48 kHz */
	case AUDIO_CODEC_AAC:
		return 48;  /* 48 kHz typical */
	default:
		return 0;
	}
}

/*
 * Get RTP clock rate in Hz for an audio codec.
 * Used for RFC 3550 jitter calculation which requires
 * converting arrival time deltas to RTP timestamp units.
 */
uint32_t audio_clock_rate_hz(enum audio_codec codec)
{
	switch (codec) {
	case AUDIO_CODEC_PCMU:
	case AUDIO_CODEC_PCMA:
	case AUDIO_CODEC_G729:
		return 8000;   /* 8 kHz clock rate */
	case AUDIO_CODEC_OPUS:
		return 48000;  /* 48 kHz clock rate */
	case AUDIO_CODEC_AAC:
		return 48000;  /* 48 kHz typical */
	default:
		return 8000;   /* Default to common VoIP rate */
	}
}

/*
 * Detect if a UDP payload is an RTP audio stream.
 * Similar to video_detect_rtp() but for audio payload types.
 */
int audio_detect_rtp(const uint8_t *payload, size_t payload_len,
                     struct rtp_info *rtp_info)
{
	if (!payload || !rtp_info || payload_len < RTP_HDR_MIN_SIZE)
		return 0;

	const struct hdr_rtp *rtp = (const struct hdr_rtp *)payload;

	/* Validate RTP version */
	if (RTP_VERSION(rtp) != 2)
		return 0;

	uint8_t pt = RTP_PAYLOAD_TYPE(rtp);

	/* Only accept audio payload types */
	if (!is_audio_payload_type(pt))
		return 0;

	/* Extract RTP header fields */
	rtp_info->payload_type = pt;
	rtp_info->last_seq = ntohs(rtp->seq);
	rtp_info->ssrc = ntohl(rtp->ssrc);
	rtp_info->rtp_timestamp = ntohl(rtp->timestamp);
	rtp_info->jitter_us = 0;
	rtp_info->seq_loss = 0;
	rtp_info->packets_seen = 1;

	/* Calculate payload length (excluding RTP header and CSRC list) */
	size_t header_len = RTP_HDR_MIN_SIZE + (RTP_CSRC_COUNT(rtp) * 4);
	rtp_info->payload_len = (payload_len > header_len) ? payload_len - header_len : 0;

	/* Set audio-specific fields */
	rtp_info->audio_codec = audio_detect_codec(pt);
	rtp_info->sample_rate_khz = audio_sample_rate_khz(rtp_info->audio_codec);
	rtp_info->channels = 1;  /* Most VoIP audio is mono */

	/* Clear video fields */
	rtp_info->codec = VIDEO_CODEC_UNKNOWN;
	rtp_info->codec_source = CODEC_SRC_UNKNOWN;

	return 1;
}
