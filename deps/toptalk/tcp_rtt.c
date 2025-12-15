#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/ip.h>

#include "tcp_rtt.h"
#include "flow.h"  /* For TCP_HEALTH_* constants */
#include "decode.h"
#include "timeywimey.h"

/* Set to 1 to enable RTT debug output to stderr */
#define RTT_DEBUG 0

static struct tcp_rtt_entry *rtt_table = NULL;

/* Convert RTT in microseconds to histogram bucket index.
 * Log-scale buckets for better resolution at low latencies:
 * 0: 0-99us, 1: 100-199us, 2: 200-499us, 3: 500-999us,
 * 4: 1-2ms, 5: 2-5ms, 6: 5-10ms, 7: 10-20ms, 8: 20-50ms,
 * 9: 50-100ms, 10: 100-200ms, 11: 200-500ms, 12: 500ms-1s, 13: >1s
 */
static int rtt_to_bucket(int64_t rtt_us)
{
	if (rtt_us < 100) return 0;
	if (rtt_us < 200) return 1;
	if (rtt_us < 500) return 2;
	if (rtt_us < 1000) return 3;
	if (rtt_us < 2000) return 4;
	if (rtt_us < 5000) return 5;
	if (rtt_us < 10000) return 6;
	if (rtt_us < 20000) return 7;
	if (rtt_us < 50000) return 8;
	if (rtt_us < 100000) return 9;
	if (rtt_us < 200000) return 10;
	if (rtt_us < 500000) return 11;
	if (rtt_us < 1000000) return 12;
	return 13; /* > 1s */
}

/* Create canonical key (lower IP/port first for consistent lookup) */
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

/* Record an outgoing sequence number */
static void record_seq(struct tcp_rtt_direction *dir,
                       uint32_t seq, uint16_t payload_len,
                       struct timeval timestamp)
{
	if (payload_len == 0)
		return;  /* Skip pure ACKs */

	/* Calculate expected ACK */
	uint32_t seq_end = seq + payload_len;

	/* Add to circular buffer */
	int idx;
	if (dir->seq_count >= MAX_SEQ_ENTRIES) {
		/* Buffer full, overwrite oldest */
		idx = dir->seq_head;
		dir->seq_head = (dir->seq_head + 1) % MAX_SEQ_ENTRIES;
	} else {
		idx = (dir->seq_head + dir->seq_count) % MAX_SEQ_ENTRIES;
		dir->seq_count++;
	}

	dir->pending_seqs[idx].seq_end = seq_end;
	dir->pending_seqs[idx].timestamp = timestamp;
}

/* Check ACK and calculate RTT if matched */
static void process_ack(struct tcp_rtt_direction *dir,
                        uint32_t ack,
                        struct timeval timestamp)
{
	if (dir->seq_count == 0)
		return;

	/* Find matching seq entry (ACK acknowledges up to this seq)
	 * We look for entries where ack >= seq_end, meaning this ACK
	 * covers the data we sent.
	 */
	int matched_idx = -1;
	int matched_count = 0;

	for (int i = 0; i < dir->seq_count; i++) {
		int idx = (dir->seq_head + i) % MAX_SEQ_ENTRIES;

		/* Check if this ACK covers this sequence entry
		 * Handle sequence number wraparound with signed comparison
		 */
		int32_t diff = (int32_t)(ack - dir->pending_seqs[idx].seq_end);
		if (diff >= 0) {
			matched_idx = idx;
			matched_count = i + 1;
		}
	}

	if (matched_idx < 0)
		return;

	/* Calculate RTT from the matched entry */
	struct timeval rtt_tv = tv_absdiff(timestamp,
	                                   dir->pending_seqs[matched_idx].timestamp);
	int64_t rtt_us = rtt_tv.tv_sec * 1000000LL + rtt_tv.tv_usec;

#if RTT_DEBUG
	fprintf(stderr, "RTT sample: %ld us (ack=%u, matched seq_end=%u)\n",
	        rtt_us, ack, dir->pending_seqs[matched_idx].seq_end);
#endif

	/* Update EWMA: rtt_ewma = (1-alpha)*rtt_ewma + alpha*rtt_sample
	 * Using shift for efficiency: alpha = 1/8
	 * Use atomic operations for lock-free access by reader thread.
	 */
	uint32_t count = atomic_load_explicit(&dir->sample_count, memory_order_relaxed);
	if (count == 0) {
		atomic_store_explicit(&dir->rtt_ewma_us, rtt_us, memory_order_relaxed);
	} else {
		int64_t ewma = atomic_load_explicit(&dir->rtt_ewma_us, memory_order_relaxed);
		ewma = ewma - (ewma >> RTT_EWMA_ALPHA_SHIFT) +
		       (rtt_us >> RTT_EWMA_ALPHA_SHIFT);
		atomic_store_explicit(&dir->rtt_ewma_us, ewma, memory_order_relaxed);
	}
	atomic_store_explicit(&dir->rtt_last_us, rtt_us, memory_order_relaxed);
	atomic_store_explicit(&dir->sample_count, count + 1, memory_order_release);

	/* Update histogram for percentile calculation */
	int bucket = rtt_to_bucket(rtt_us);
	atomic_fetch_add_explicit(&dir->rtt_hist[bucket], 1, memory_order_relaxed);

	/* Remove matched entries (cumulative ACK covers all prior data) */
	dir->seq_head = (dir->seq_head + matched_count) % MAX_SEQ_ENTRIES;
	dir->seq_count -= matched_count;
}

void tcp_rtt_process_packet(const struct flow *flow,
                            uint32_t seq,
                            uint32_t ack,
                            uint8_t flags,
                            uint16_t payload_len,
                            struct timeval timestamp)
{
	if (flow->proto != IPPROTO_TCP)
		return;

	struct tcp_flow_key key;
	int is_forward;
	make_canonical_key(&key, flow, &is_forward);

	struct tcp_rtt_entry *entry;
	HASH_FIND(hh, rtt_table, &key, sizeof(struct tcp_flow_key), entry);

	if (!entry) {
		entry = calloc(1, sizeof(struct tcp_rtt_entry));
		if (!entry)
			return;
		entry->key = key;
		HASH_ADD(hh, rtt_table, key, sizeof(struct tcp_flow_key), entry);
	}

	entry->last_activity = timestamp;

	/* Track flags seen per direction */
	if (is_forward) {
		entry->flags_seen_fwd |= flags;
	} else {
		entry->flags_seen_rev |= flags;
	}

	/* Update connection state based on flags (atomic for lock-free reader access) */
	int current_state = atomic_load_explicit(&entry->state, memory_order_relaxed);
	if (flags & TCP_FLAG_RST) {
		atomic_store_explicit(&entry->state, TCP_STATE_CLOSED, memory_order_relaxed);
	} else if (flags & TCP_FLAG_FIN) {
		/* Check if FIN has been seen in BOTH directions (not just twice) */
		int fin_fwd = entry->flags_seen_fwd & TCP_FLAG_FIN;
		int fin_rev = entry->flags_seen_rev & TCP_FLAG_FIN;
		if (fin_fwd && fin_rev) {
			/* FIN seen in both directions - fully closed */
			atomic_store_explicit(&entry->state, TCP_STATE_CLOSED, memory_order_relaxed);
		} else if (current_state != TCP_STATE_CLOSED) {
			/* FIN seen in one direction only - half-closed */
			atomic_store_explicit(&entry->state, TCP_STATE_FIN_WAIT, memory_order_relaxed);
		}
	} else if (flags & TCP_FLAG_SYN) {
		/* SYN seen - mark as new connection (unless already further along) */
		if (current_state == TCP_STATE_UNKNOWN) {
			atomic_store_explicit(&entry->state, TCP_STATE_SYN_SEEN, memory_order_relaxed);
		}
	} else if (current_state == TCP_STATE_UNKNOWN ||
	           current_state == TCP_STATE_SYN_SEEN) {
		/* Data packet - connection is now active */
		if (payload_len > 0) {
			atomic_store_explicit(&entry->state, TCP_STATE_ACTIVE, memory_order_relaxed);
		}
	}

	struct tcp_rtt_direction *tx_dir = is_forward ? &entry->fwd : &entry->rev;
	struct tcp_rtt_direction *rx_dir = is_forward ? &entry->rev : &entry->fwd;

	/* Record outgoing sequence if this packet has data */
	if (payload_len > 0) {
		record_seq(tx_dir, seq, payload_len, timestamp);
	}

	/* Process ACK - check against pending sequences in opposite direction */
	if (flags & TCP_FLAG_ACK) {
		process_ack(rx_dir, ack, timestamp);
	}
}

int64_t tcp_rtt_get_ewma(const struct flow *flow)
{
	if (flow->proto != IPPROTO_TCP)
		return -1;

	struct tcp_flow_key key;
	int is_forward;
	make_canonical_key(&key, flow, &is_forward);

	struct tcp_rtt_entry *entry;
	HASH_FIND(hh, rtt_table, &key, sizeof(struct tcp_flow_key), entry);

	if (!entry)
		return -1;

	/* Return RTT for the direction this flow represents (atomic load) */
	struct tcp_rtt_direction *dir = is_forward ? &entry->fwd : &entry->rev;
	if (atomic_load_explicit(&dir->sample_count, memory_order_acquire) == 0)
		return -1;

	return atomic_load_explicit(&dir->rtt_ewma_us, memory_order_relaxed);
}

int64_t tcp_rtt_get_last(const struct flow *flow)
{
	if (flow->proto != IPPROTO_TCP)
		return -1;

	struct tcp_flow_key key;
	int is_forward;
	make_canonical_key(&key, flow, &is_forward);

	struct tcp_rtt_entry *entry;
	HASH_FIND(hh, rtt_table, &key, sizeof(struct tcp_flow_key), entry);

	if (!entry)
		return -1;

	/* Atomic load for lock-free access */
	struct tcp_rtt_direction *dir = is_forward ? &entry->fwd : &entry->rev;
	if (atomic_load_explicit(&dir->sample_count, memory_order_acquire) == 0)
		return -1;

	return atomic_load_explicit(&dir->rtt_last_us, memory_order_relaxed);
}

enum tcp_conn_state tcp_rtt_get_state(const struct flow *flow)
{
	if (flow->proto != IPPROTO_TCP)
		return TCP_STATE_UNKNOWN;

	struct tcp_flow_key key;
	int is_forward;
	make_canonical_key(&key, flow, &is_forward);

	struct tcp_rtt_entry *entry;
	HASH_FIND(hh, rtt_table, &key, sizeof(struct tcp_flow_key), entry);

	if (!entry)
		return TCP_STATE_UNKNOWN;

	return atomic_load_explicit(&entry->state, memory_order_relaxed);
}

int tcp_rtt_get_info(const struct flow *flow, int64_t *rtt_us,
                     enum tcp_conn_state *state, int *saw_syn)
{
	if (flow->proto != IPPROTO_TCP) {
		*rtt_us = -1;
		*state = TCP_STATE_UNKNOWN;
		*saw_syn = 0;
		return -1;
	}

	struct tcp_flow_key key;
	int is_forward;
	make_canonical_key(&key, flow, &is_forward);

	struct tcp_rtt_entry *entry;
	HASH_FIND(hh, rtt_table, &key, sizeof(struct tcp_flow_key), entry);

	if (!entry) {
		*rtt_us = -1;
		*state = TCP_STATE_UNKNOWN;
		*saw_syn = 0;
		return -1;
	}

	/* Atomic loads for lock-free access */
	*state = atomic_load_explicit(&entry->state, memory_order_relaxed);

	/* Check if SYN was ever seen in either direction */
	*saw_syn = ((entry->flags_seen_fwd | entry->flags_seen_rev) & TCP_FLAG_SYN) ? 1 : 0;

	struct tcp_rtt_direction *dir = is_forward ? &entry->fwd : &entry->rev;
	if (atomic_load_explicit(&dir->sample_count, memory_order_acquire) == 0) {
		*rtt_us = -1;
	} else {
		*rtt_us = atomic_load_explicit(&dir->rtt_ewma_us, memory_order_relaxed);
	}

	return 0;
}

void tcp_rtt_expire_old(struct timeval deadline, struct timeval window)
{
	struct tcp_rtt_entry *entry, *tmp;
	struct timeval expiry = tv_absdiff(deadline, window);

	HASH_ITER(hh, rtt_table, entry, tmp) {
		if (tv_cmp(entry->last_activity, expiry) < 0) {
			HASH_DEL(rtt_table, entry);
			free(entry);
		}
	}
}

void tcp_rtt_init(void)
{
	rtt_table = NULL;
}

void tcp_rtt_cleanup(void)
{
	struct tcp_rtt_entry *entry, *tmp;
	HASH_ITER(hh, rtt_table, entry, tmp) {
		HASH_DEL(rtt_table, entry);
		free(entry);
	}
	rtt_table = NULL;
}

/* Get RTT histogram and determine health status */
int tcp_rtt_get_health(const struct flow *flow,
                       uint32_t retransmit_cnt,
                       uint32_t total_packets,
                       uint32_t zero_window_cnt,
                       struct tcp_health_result *result)
{
	memset(result, 0, sizeof(*result));

	if (flow->proto != IPPROTO_TCP) {
		result->health_status = TCP_HEALTH_UNKNOWN;
		return -1;
	}

	struct tcp_flow_key key;
	int is_forward;
	make_canonical_key(&key, flow, &is_forward);

	struct tcp_rtt_entry *entry;
	HASH_FIND(hh, rtt_table, &key, sizeof(struct tcp_flow_key), entry);

	if (!entry) {
		result->health_status = TCP_HEALTH_UNKNOWN;
		return -1;
	}

	struct tcp_rtt_direction *dir = is_forward ? &entry->fwd : &entry->rev;
	uint32_t sample_count = atomic_load_explicit(&dir->sample_count,
	                                              memory_order_acquire);

	/* Copy histogram to result (atomic loads) */
	for (int i = 0; i < RTT_HIST_BUCKETS; i++) {
		result->rtt_hist[i] = atomic_load_explicit(&dir->rtt_hist[i],
		                                           memory_order_relaxed);
	}
	result->rtt_samples = sample_count;

	/* Need at least 10 samples for meaningful health assessment */
	if (sample_count < 10) {
		result->health_status = TCP_HEALTH_UNKNOWN;
		return 0;
	}

#if RTT_DEBUG
	fprintf(stderr, "Health: sample_count=%u, is_forward=%d\n",
	        sample_count, is_forward);
	for (int i = 0; i < RTT_HIST_BUCKETS; i++) {
		if (result->rtt_hist[i] > 0)
			fprintf(stderr, "  bucket[%d] = %u\n", i, result->rtt_hist[i]);
	}
#endif

	/* Calculate p50 and p99 bucket indices for tail latency check */
	uint32_t p50_target = sample_count / 2;
	uint32_t p99_target = sample_count * 99 / 100;

	uint32_t cumulative = 0;
	int p50_bucket = -1, p99_bucket = -1;

	for (int i = 0; i < RTT_HIST_BUCKETS; i++) {
		cumulative += result->rtt_hist[i];
		if (p50_bucket < 0 && cumulative >= p50_target) {
			p50_bucket = i;
		}
		if (p99_bucket < 0 && cumulative >= p99_target) {
			p99_bucket = i;
			break;
		}
	}

	/* Calculate health status based on thresholds:
	 * - Retransmit %: Warning > 0.5%, Problem > 2%
	 * - RTT p99/p50 bucket spread: Warning if 3+ buckets apart, Problem if 5+
	 *   (each bucket is roughly 2x, so 3 buckets ≈ 8x, 5 buckets ≈ 32x)
	 * - Zero window events: Warning > 0, Problem > 3
	 */
	result->health_status = TCP_HEALTH_GOOD;
	result->health_flags = 0;

	/* Check retransmit rate */
	if (total_packets > 0) {
		/* Calculate retransmit percentage x100 (e.g., 150 = 1.50%) */
		uint32_t retransmit_pct_x100 = (retransmit_cnt * 10000) / total_packets;

		if (retransmit_pct_x100 > 200) {  /* > 2% */
			result->health_flags |= TCP_HEALTH_FLAG_HIGH_LOSS;
			result->health_status = TCP_HEALTH_PROBLEM;
		} else if (retransmit_pct_x100 > 50) {  /* > 0.5% */
			result->health_flags |= TCP_HEALTH_FLAG_ELEVATED_LOSS;
			if (result->health_status < TCP_HEALTH_WARNING)
				result->health_status = TCP_HEALTH_WARNING;
		}
	}

	/* Check tail latency spread (bucket difference instead of ratio) */
	if (p50_bucket >= 0 && p99_bucket >= 0) {
		int bucket_spread = p99_bucket - p50_bucket;
		if (bucket_spread >= 5) {  /* ~32x latency difference */
			result->health_flags |= TCP_HEALTH_FLAG_HIGH_TAIL_LATENCY;
			result->health_status = TCP_HEALTH_PROBLEM;
		} else if (bucket_spread >= 3) {  /* ~8x latency difference */
			result->health_flags |= TCP_HEALTH_FLAG_HIGH_TAIL_LATENCY;
			if (result->health_status < TCP_HEALTH_WARNING)
				result->health_status = TCP_HEALTH_WARNING;
		}
	}

	/* Check zero window events */
	if (zero_window_cnt > 3) {
		result->health_flags |= TCP_HEALTH_FLAG_WINDOW_STARVATION;
		result->health_status = TCP_HEALTH_PROBLEM;
	} else if (zero_window_cnt > 0) {
		result->health_flags |= TCP_HEALTH_FLAG_WINDOW_STARVATION;
		if (result->health_status < TCP_HEALTH_WARNING)
			result->health_status = TCP_HEALTH_WARNING;
	}

	return 0;
}
