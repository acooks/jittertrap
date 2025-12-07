#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/ip.h>

#include "tcp_window.h"
#include "decode.h"
#include "timeywimey.h"

/* Set to 1 to enable window debug output to stderr */
#define WINDOW_DEBUG 0

static struct tcp_window_entry *window_table = NULL;

/* Create canonical key (lower IP/port first for consistent lookup)
 * This is the same logic as tcp_rtt.c to ensure bidirectional matching
 */
static void make_canonical_key(struct tcp_flow_key *key,
                               const struct flow *flow,
                               int *is_forward)
{
	memset(key, 0, sizeof(*key));
	key->ethertype = flow->ethertype;

	int cmp;
	if (flow->ethertype == ETHERTYPE_IP) {
		cmp = memcmp(&flow->src_ip, &flow->dst_ip, sizeof(struct in_addr));
		if (cmp < 0 || (cmp == 0 && flow->sport <= flow->dport)) {
			key->ip_lo = flow->src_ip;
			key->ip_hi = flow->dst_ip;
			key->port_lo = flow->sport;
			key->port_hi = flow->dport;
			*is_forward = 1;
		} else {
			key->ip_lo = flow->dst_ip;
			key->ip_hi = flow->src_ip;
			key->port_lo = flow->dport;
			key->port_hi = flow->sport;
			*is_forward = 0;
		}
	} else { /* IPv6 */
		cmp = memcmp(&flow->src_ip6, &flow->dst_ip6, sizeof(struct in6_addr));
		if (cmp < 0 || (cmp == 0 && flow->sport <= flow->dport)) {
			key->ip6_lo = flow->src_ip6;
			key->ip6_hi = flow->dst_ip6;
			key->port_lo = flow->sport;
			key->port_hi = flow->dport;
			*is_forward = 1;
		} else {
			key->ip6_lo = flow->dst_ip6;
			key->ip6_hi = flow->src_ip6;
			key->port_lo = flow->dport;
			key->port_hi = flow->sport;
			*is_forward = 0;
		}
	}
}

/* Parse TCP options to extract window scale factor
 * Called only for SYN/SYN-ACK packets
 * Returns: scale factor (0-14), or -1 if not present
 */
static int parse_window_scale(const uint8_t *tcp_header,
                              const uint8_t *end_of_packet)
{
	const struct hdr_tcp *tcp = (const struct hdr_tcp *)tcp_header;
	unsigned int tcp_hdr_len = TH_OFF(tcp) * 4;

	/* Options start after the fixed 20-byte header */
	const uint8_t *opt_start = tcp_header + 20;
	const uint8_t *opt_end = tcp_header + tcp_hdr_len;

	/* Bounds check */
	if (opt_end > end_of_packet) {
		opt_end = end_of_packet;
	}

	/* No options present */
	if (opt_start >= opt_end) {
		return -1;
	}

	const uint8_t *opt = opt_start;
	while (opt < opt_end) {
		uint8_t kind = *opt;

		if (kind == TCP_OPT_EOL)
			break;  /* End of options */

		if (kind == TCP_OPT_NOP) {
			opt++;
			continue;
		}

		/* All other options have length field */
		if (opt + 1 >= opt_end)
			break;

		uint8_t len = *(opt + 1);
		if (len < 2 || opt + len > opt_end)
			break;

		if (kind == TCP_OPT_WSCALE && len == 3) {
			uint8_t scale = *(opt + 2);
			/* RFC 7323: max window scale is 14 */
			if (scale > 14)
				scale = 14;
			return scale;
		}

		opt += len;
	}

	return -1;  /* Window scale not present */
}

/* Update window statistics for a direction (atomic stores for lock-free reader access) */
static void update_window_stats(struct tcp_window_direction *dir,
                                uint16_t raw_window,
                                uint8_t flags)
{
	atomic_store_explicit(&dir->raw_window, raw_window, memory_order_relaxed);

	/* Calculate scaled window if we know the scale factor */
	int scale_status = atomic_load_explicit(&dir->scale_status, memory_order_relaxed);
	uint32_t scaled;
	if (scale_status == WSCALE_SEEN) {
		uint8_t scale = atomic_load_explicit(&dir->window_scale, memory_order_relaxed);
		scaled = (uint32_t)raw_window << scale;
	} else if (scale_status == WSCALE_NOT_PRESENT) {
		scaled = raw_window;
	} else {
		/* Scale unknown - just use raw value */
		scaled = raw_window;
	}
	atomic_store_explicit(&dir->scaled_window, scaled, memory_order_relaxed);

	/* Update min/max (skip first sample for initialization) */
	if (dir->min_window == 0 && dir->max_window == 0) {
		dir->min_window = scaled;
		dir->max_window = scaled;
	} else {
		if (scaled < dir->min_window)
			dir->min_window = scaled;
		if (scaled > dir->max_window)
			dir->max_window = scaled;
	}

	/* Zero window detection */
	if (raw_window == 0) {
		atomic_fetch_add_explicit(&dir->zero_window_count, 1, memory_order_relaxed);
		atomic_fetch_or_explicit(&dir->recent_events, CONG_EVENT_ZERO_WINDOW, memory_order_relaxed);
#if WINDOW_DEBUG
		fprintf(stderr, "Zero window detected! count=%u\n",
		        atomic_load_explicit(&dir->zero_window_count, memory_order_relaxed));
#endif
	}

	/* ECE/CWR flag tracking */
	if (flags & TH_ECE) {
		atomic_fetch_add_explicit(&dir->ece_count, 1, memory_order_relaxed);
		atomic_fetch_or_explicit(&dir->recent_events, CONG_EVENT_ECE, memory_order_relaxed);
	}
	if (flags & TH_CWR) {
		atomic_fetch_add_explicit(&dir->cwr_count, 1, memory_order_relaxed);
		atomic_fetch_or_explicit(&dir->recent_events, CONG_EVENT_CWR, memory_order_relaxed);
	}
}

/* Detect duplicate ACKs (same ACK number, no data) */
static void check_dup_ack(struct tcp_window_direction *dir,
                          uint32_t ack,
                          uint16_t payload_len,
                          uint8_t flags)
{
	/* Only count pure ACKs (no data, ACK flag set) */
	if (payload_len > 0 || !(flags & TH_ACK)) {
		dir->dup_ack_streak = 0;
		dir->last_ack = ack;
		return;
	}

	/* SYN/FIN/RST packets don't count as dup ACKs */
	if (flags & (TH_SYN | TH_FIN | TH_RST)) {
		dir->dup_ack_streak = 0;
		dir->last_ack = ack;
		return;
	}

	if (ack == dir->last_ack && dir->last_ack != 0) {
		dir->dup_ack_streak++;
		if (dir->dup_ack_streak == 3) {
			/* Triple duplicate ACK - likely packet loss */
			atomic_fetch_add_explicit(&dir->dup_ack_events, 1, memory_order_relaxed);
			atomic_fetch_or_explicit(&dir->recent_events, CONG_EVENT_DUP_ACK, memory_order_relaxed);
#if WINDOW_DEBUG
			fprintf(stderr, "Triple dup ACK detected! ack=%u events=%u\n",
			        ack, atomic_load_explicit(&dir->dup_ack_events, memory_order_relaxed));
#endif
		}
	} else {
		dir->dup_ack_streak = 0;
		dir->last_ack = ack;
	}
}

/* Detect retransmissions by checking if sequence number is lower
 * than the highest previously seen (simplified heuristic)
 */
static void check_retransmit(struct tcp_window_direction *dir,
                             uint32_t seq,
                             uint16_t payload_len,
                             uint8_t flags)
{
	if (payload_len == 0)
		return;

	/* SYN retransmits don't count */
	if (flags & TH_SYN)
		return;

	if (!dir->highest_seq_valid) {
		dir->highest_seq_seen = seq + payload_len;
		dir->highest_seq_valid = 1;
		return;
	}

	/* Handle sequence number wraparound with signed comparison */
	int32_t diff = (int32_t)(seq - dir->highest_seq_seen);

	if (diff < 0) {
		/* This sequence number is less than the highest seen - retransmit */
		atomic_fetch_add_explicit(&dir->retransmit_count, 1, memory_order_relaxed);
		atomic_fetch_or_explicit(&dir->recent_events, CONG_EVENT_RETRANSMIT, memory_order_relaxed);
#if WINDOW_DEBUG
		fprintf(stderr, "Retransmit detected! seq=%u highest=%u count=%u\n",
		        seq, dir->highest_seq_seen,
		        atomic_load_explicit(&dir->retransmit_count, memory_order_relaxed));
#endif
	} else if (diff > 0) {
		/* New highest sequence */
		dir->highest_seq_seen = seq + payload_len;
	}
}

void tcp_window_process_packet(const struct flow *flow,
                               const uint8_t *tcp_header,
                               const uint8_t *end_of_packet,
                               uint32_t seq,
                               uint32_t ack,
                               uint8_t flags,
                               uint16_t window,
                               uint16_t payload_len,
                               struct timeval timestamp)
{
	if (flow->proto != IPPROTO_TCP)
		return;

	struct tcp_flow_key key;
	int is_forward;
	make_canonical_key(&key, flow, &is_forward);

	struct tcp_window_entry *entry;
	HASH_FIND(hh, window_table, &key, sizeof(struct tcp_flow_key), entry);

	if (!entry) {
		entry = calloc(1, sizeof(struct tcp_window_entry));
		if (!entry)
			return;
		entry->key = key;
		HASH_ADD(hh, window_table, key, sizeof(struct tcp_flow_key), entry);
	}

	entry->last_activity = timestamp;

	/* Get transmit direction pointer (for outgoing packets from this side) */
	struct tcp_window_direction *tx_dir = is_forward ? &entry->fwd : &entry->rev;

	/* Parse window scale from SYN packets (atomic stores for lock-free reader access) */
	if (flags & TH_SYN) {
		int scale = parse_window_scale(tcp_header, end_of_packet);
		if (scale >= 0) {
			atomic_store_explicit(&tx_dir->window_scale, (uint8_t)scale, memory_order_relaxed);
			atomic_store_explicit(&tx_dir->scale_status, WSCALE_SEEN, memory_order_release);
#if WINDOW_DEBUG
			char src_str[INET6_ADDRSTRLEN], dst_str[INET6_ADDRSTRLEN];
			if (flow->ethertype == ETHERTYPE_IP) {
				inet_ntop(AF_INET, &flow->src_ip, src_str, sizeof(src_str));
				inet_ntop(AF_INET, &flow->dst_ip, dst_str, sizeof(dst_str));
			} else {
				inet_ntop(AF_INET6, &flow->src_ip6, src_str, sizeof(src_str));
				inet_ntop(AF_INET6, &flow->dst_ip6, dst_str, sizeof(dst_str));
			}
			fprintf(stderr, "SYN: %s:%d -> %s:%d is_fwd=%d scale=%d\n",
			        src_str, flow->sport, dst_str, flow->dport, is_forward, scale);
#endif
		} else {
			/* SYN without window scale option */
			atomic_store_explicit(&tx_dir->window_scale, 0, memory_order_relaxed);
			atomic_store_explicit(&tx_dir->scale_status, WSCALE_NOT_PRESENT, memory_order_release);
		}
	}

	/* Update window stats - the window field advertises the sender's
	 * receive window, so it applies to the transmit direction */
	update_window_stats(tx_dir, window, flags);

	/* Check for duplicate ACKs in the transmit direction
	 * (tracking ACKs sent in this direction) */
	check_dup_ack(tx_dir, ack, payload_len, flags);

	/* Check for retransmissions in the transmit direction */
	check_retransmit(tx_dir, seq, payload_len, flags);
}

int tcp_window_get_info(const struct flow *flow,
                        struct tcp_window_info *info)
{
	/* Initialize to defaults */
	memset(info, 0, sizeof(*info));
	info->rwnd_bytes = -1;
	info->window_scale = -1;

	if (flow->proto != IPPROTO_TCP) {
		return -1;
	}

	struct tcp_flow_key key;
	int is_forward;
	make_canonical_key(&key, flow, &is_forward);

	struct tcp_window_entry *entry;
	HASH_FIND(hh, window_table, &key, sizeof(struct tcp_flow_key), entry);

	if (!entry) {
		return -1;
	}

	/* Get info for the direction this flow represents (atomic loads for lock-free access) */
	struct tcp_window_direction *dir = is_forward ? &entry->fwd : &entry->rev;

	/* Return scaled window if available */
	int scale_status = atomic_load_explicit(&dir->scale_status, memory_order_acquire);
	if (scale_status == WSCALE_SEEN) {
		info->rwnd_bytes = atomic_load_explicit(&dir->scaled_window, memory_order_relaxed);
		info->window_scale = atomic_load_explicit(&dir->window_scale, memory_order_relaxed);
	} else if (scale_status == WSCALE_NOT_PRESENT) {
		/* SYN seen but no window scale option - raw value is accurate */
		info->rwnd_bytes = atomic_load_explicit(&dir->raw_window, memory_order_relaxed);
		info->window_scale = 0;
	} else {
		/* Scale unknown (missed the SYN) - return raw value, mark scale unknown */
		info->rwnd_bytes = atomic_load_explicit(&dir->raw_window, memory_order_relaxed);
		info->window_scale = -1;
	}

	info->zero_window_count = atomic_load_explicit(&dir->zero_window_count, memory_order_relaxed);
	info->dup_ack_count = atomic_load_explicit(&dir->dup_ack_events, memory_order_relaxed);
	info->retransmit_count = atomic_load_explicit(&dir->retransmit_count, memory_order_relaxed);
	info->ece_count = atomic_load_explicit(&dir->ece_count, memory_order_relaxed);
	info->cwr_count = atomic_load_explicit(&dir->cwr_count, memory_order_relaxed);

	/* Return recent events and clear the flag (atomic exchange) */
	info->recent_events = atomic_exchange_explicit(&dir->recent_events, 0, memory_order_relaxed);

	return 0;
}

void tcp_window_expire_old(struct timeval deadline, struct timeval window)
{
	struct tcp_window_entry *entry, *tmp;
	struct timeval expiry = tv_absdiff(deadline, window);

	HASH_ITER(hh, window_table, entry, tmp) {
		if (tv_cmp(entry->last_activity, expiry) < 0) {
			HASH_DEL(window_table, entry);
			free(entry);
		}
	}
}

void tcp_window_init(void)
{
	window_table = NULL;
}

void tcp_window_cleanup(void)
{
	struct tcp_window_entry *entry, *tmp;
	HASH_ITER(hh, window_table, entry, tmp) {
		HASH_DEL(window_table, entry);
		free(entry);
	}
	window_table = NULL;
}
