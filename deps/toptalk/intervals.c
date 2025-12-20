#define _GNU_SOURCE
#include <time.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <sched.h>
#include <string.h>
#include <stdint.h>
#include <pcap.h>
#include <pcap/sll.h>
#include <pthread.h>
#include <poll.h>
#include <errno.h>

#include "utlist.h"
#include "uthash.h"

#include "flow.h"
#include "decode.h"
#include "timeywimey.h"
#include "tcp_rtt.h"
#include "tcp_window.h"
#include "video_detect.h"
#include "video_metrics.h"
#include "rtsp_tap.h"

#include "intervals.h"

/* Global RTSP tap state - shared across all packet processing */
static struct rtsp_tap_state g_rtsp_tap;

/* Optional packet capture callback - set by server if needed */
static void (*pcap_store_callback)(const struct pcap_pkthdr *, const uint8_t *) = NULL;
static void (*pcap_iface_callback)(int dlt) = NULL;

void tt_set_pcap_callback(void (*store_cb)(const struct pcap_pkthdr *, const uint8_t *),
                          void (*iface_cb)(int dlt))
{
	pcap_store_callback = store_cb;
	pcap_iface_callback = iface_cb;
}

#define PCAP_BUF_STORE(hdr, data) do { \
	if (pcap_store_callback) pcap_store_callback(hdr, data); \
} while(0)

/* Optional RTP forward callback for VLC passthrough */
static tt_rtp_forward_cb rtp_forward_callback = NULL;

void tt_set_rtp_forward_callback(tt_rtp_forward_cb cb)
{
	rtp_forward_callback = cb;
}

/* IPG tracking state - stored per flow in the hash table */
struct ipg_state {
	struct timeval last_pkt_time;  /* Timestamp of last packet */
	uint32_t ipg_hist[IPG_HIST_BUCKETS];
	uint32_t ipg_samples;
	int64_t ipg_sum_us;            /* Sum of all IPG values for mean calc */
	uint8_t initialized;           /* 1 if we've seen at least one packet */
};

struct flow_hash {
	struct flow_record f;
	struct ipg_state ipg;          /* IPG tracking state */
	uint32_t pkt_count_this_interval;  /* Packet count for PPS calculation */
	union {
		UT_hash_handle r_hh;  /* sliding window reference table */
		UT_hash_handle ts_hh; /* time series tables */
	};
};

struct flow_pkt_list {
	struct flow_pkt pkt;
	struct flow_pkt_list *next, *prev;
};

/*
 * Ring buffer for packet list - eliminates per-packet malloc/free.
 *
 * Size is configurable at compile time via PKT_RING_SIZE.
 * Must be a power of 2 for efficient modulo via bitmask.
 * Default 262144 entries supports ~87K pps with 3-second window.
 */
#ifndef PKT_RING_SIZE
#define PKT_RING_SIZE (1 << 18)  /* 262144 entries */
#endif

/* Verify PKT_RING_SIZE is a power of 2 at compile time */
_Static_assert((PKT_RING_SIZE & (PKT_RING_SIZE - 1)) == 0,
               "PKT_RING_SIZE must be a power of 2");

static struct flow_pkt pkt_ring[PKT_RING_SIZE];
static uint32_t pkt_ring_head = 0;  /* Next write position */
static uint32_t pkt_ring_tail = 0;  /* Oldest valid entry */
#define PKT_RING_MASK (PKT_RING_SIZE - 1)
#define PKT_RING_COUNT() ((pkt_ring_head - pkt_ring_tail) & UINT32_MAX)


typedef int (*pcap_decoder)(const struct pcap_pkthdr *h,
                            const uint8_t *wirebits,
                            struct flow_pkt *pkt,
                            char *errstr);

/* userdata for callback used in pcap_dispatch */
struct pcap_handler_user {
	pcap_decoder decoder; /* callback / function pointer */
	int datalink_type;    /* DLT_EN10MB or DLT_LINUX_SLL */
	struct {
		int err;
		char errstr[DECODE_ERRBUF_SIZE];
	} result;
};

struct pcap_info {
	pcap_t *handle;
	int selectable_fd;
	struct pcap_handler_user decoder_cbdata;
};

struct tt_thread_private {
	struct pcap_info pi;
};

/* long, continuous sliding window tracking top flows */
static struct flow_hash *flow_ref_table = NULL;

/* Legacy: packet list linked list head - no longer used with ring buffer */
/* static struct flow_pkt_list *pkt_list_ref_head = NULL; */

/* flows recorded as period-on-period intervals */
static struct flow_hash *incomplete_flow_tables[INTERVAL_COUNT] = { NULL };
static struct flow_hash *complete_flow_tables[INTERVAL_COUNT] = { NULL };

static struct timeval interval_end[INTERVAL_COUNT] = { 0 };
static struct timeval interval_start[INTERVAL_COUNT] = { 0 };

static struct timeval ref_window_size;

static struct {
	int64_t bytes;
	int64_t packets;
} totals;

/*
 * Free all entries in a flow hash table and reset the head pointer to NULL.
 * The table_head pointer is passed by reference (pointer-to-pointer) so we
 * can update the caller's head pointer directly, avoiding confusion about
 * which variable HASH_DELETE modifies.
 */
static void free_flow_table(struct flow_hash **table_head)
{
	struct flow_hash *iter, *tmp;

	HASH_ITER(ts_hh, *table_head, iter, tmp)
	{
		HASH_DELETE(ts_hh, *table_head, iter);
		free(iter);
	}
	/* table_head is already NULL after all deletions, but be explicit */
	*table_head = NULL;
}

/*
 * Rotate interval table: swap incomplete → complete (O(1) pointer swap).
 * This is called at each interval boundary to finalize the current interval's
 * data and prepare for the next interval.
 *
 * Previous approach: copy all entries (O(n) allocations)
 * Optimized approach: swap pointers (O(1))
 *
 * The only O(n) work is freeing the old complete table, which is unavoidable.
 */
static void clear_table(int table_idx)
{
	/* Step 1: Free the old complete table (data from 2 intervals ago) */
	free_flow_table(&complete_flow_tables[table_idx]);

	/* Step 2: Swap - incomplete becomes complete (O(1) pointer assignment) */
	complete_flow_tables[table_idx] = incomplete_flow_tables[table_idx];

	/* Step 3: Reset incomplete to empty table for next interval */
	incomplete_flow_tables[table_idx] = NULL;
}

/* initialise interval start and end times */
static void init_intervals(struct timeval now)
{
	for (int i = 0; i < INTERVAL_COUNT; i++) {
		interval_start[i] = now;
		interval_end[i] = tv_add(interval_start[i], tt_intervals[i]);
	}
}

/* Forward declaration for pps_to_bucket (defined later with other bucket functions) */
static inline int pps_to_bucket(uint32_t pps);

/* Compute window conditions at interval boundary (before table rotation).
 * This detects conditions like low window, zero-window, and starvation
 * based on accumulated window statistics during the interval.
 */
static void compute_window_conditions(int table_idx)
{
	struct flow_hash *iter, *tmp;

	HASH_ITER(ts_hh, incomplete_flow_tables[table_idx], iter, tmp) {
		struct flow_window_info *w = &iter->f.window;

		if (w->window_samples == 0)
			continue;

		uint32_t avg_window = w->window_sum / w->window_samples;
		w->window_conditions = 0;

		/* Zero window seen in this interval */
		if (w->window_min == 0) {
			w->window_conditions |= WINDOW_COND_ZERO_SEEN;
		}

		/* Threshold: low if below 25% of max seen, minimum 1 MSS */
		uint32_t threshold = w->window_max / 4;
		if (threshold < 1460)
			threshold = 1460;

		if (avg_window < threshold) {
			w->window_conditions |= WINDOW_COND_LOW;
			w->low_window_streak++;

			/* Hysteresis: starving after 3+ consecutive low intervals */
			if (w->low_window_streak >= 3) {
				w->window_conditions |= WINDOW_COND_STARVING;
			}
		} else {
			/* Recovered from low window */
			if (w->low_window_streak > 0) {
				w->window_conditions |= WINDOW_COND_RECOVERED;
			}
			w->low_window_streak = 0;
		}
	}
}

static void expire_old_interval_tables(struct timeval now)
{
	for (int i = 0; i < INTERVAL_COUNT; i++) {
		struct timeval interval = tt_intervals[i];

		/* interval elapsed? */
		if (0 < tv_cmp(now, interval_end[i])) {

			/* For the smallest interval (i=0), update PPS histogram
			 * for each flow based on packets seen in this interval.
			 * This gives us the distribution of per-interval packet rates.
			 */
			if (i == 0) {
				struct flow_hash *iter, *tmp;
				HASH_ITER(r_hh, flow_ref_table, iter, tmp) {
					uint32_t pps = iter->pkt_count_this_interval;
					if (pps > 0) {
						int bucket = pps_to_bucket(pps);
						iter->f.pps.pps_hist[bucket]++;
						iter->f.pps.pps_samples++;
						iter->f.pps.pps_sum += pps;
						iter->f.pps.pps_sum_sq += (uint64_t)pps * pps;
					}
					iter->pkt_count_this_interval = 0;
				}
			}

			/* Compute window conditions before rotating tables */
			compute_window_conditions(i);

			/* clear the hash table */
			clear_table(i);
			interval_start[i] = interval_end[i];
			interval_end[i] = tv_add(interval_end[i], interval);
		}
	}
}

/* Map IPG value in microseconds to histogram bucket (same scale as jitter) */
static inline int ipg_to_bucket(int64_t ipg_us)
{
	if (ipg_us < 10) return 0;
	if (ipg_us < 50) return 1;
	if (ipg_us < 100) return 2;
	if (ipg_us < 500) return 3;
	if (ipg_us < 1000) return 4;
	if (ipg_us < 2000) return 5;
	if (ipg_us < 5000) return 6;
	if (ipg_us < 10000) return 7;
	if (ipg_us < 20000) return 8;
	if (ipg_us < 50000) return 9;
	if (ipg_us < 100000) return 10;
	return 11;  /* >= 100ms */
}

/* Map packet size in bytes to histogram bucket (20 buckets)
 *
 * Designed to capture VoIP, MPEG-TS video, and tunnel MTU ceilings.
 * See struct flow_pkt_size_info in flow.h for bucket descriptions.
 */
static inline int pkt_size_to_bucket(uint32_t bytes)
{
	/* Small packets (VoIP focus) */
	if (bytes < 64) return 0;        /* <64B - undersized */
	if (bytes < 100) return 1;       /* 64-100B - minimum ACKs */
	if (bytes < 160) return 2;       /* 100-160B - G.729 VoIP */
	if (bytes < 220) return 3;       /* 160-220B - G.711 20ms */
	if (bytes < 300) return 4;       /* 220-300B - G.711 30ms */

	/* Medium packets (MPEG-TS focus) */
	if (bytes < 400) return 5;       /* 300-400B - MPEG-TS 2x */
	if (bytes < 576) return 6;       /* 400-576B - MPEG-TS 3x */
	if (bytes < 760) return 7;       /* 576-760B - MPEG-TS 4x */
	if (bytes < 950) return 8;       /* 760-950B - MPEG-TS 5x */
	if (bytes < 1140) return 9;      /* 950-1140B - MPEG-TS 6x */
	if (bytes < 1320) return 10;     /* 1140-1320B - MPEG-TS 7x */

	/* Near MTU (tunnel ceiling focus) */
	if (bytes < 1400) return 11;     /* 1320-1400B - pre-tunnel */
	if (bytes < 1430) return 12;     /* 1400-1430B - WireGuard */
	if (bytes < 1460) return 13;     /* 1430-1460B - VXLAN */
	if (bytes < 1480) return 14;     /* 1460-1480B - GRE */
	if (bytes < 1492) return 15;     /* 1480-1492B - MPLS */

	/* Full MTU */
	if (bytes < 1500) return 16;     /* 1492-1500B - near MTU */
	if (bytes < 1518) return 17;     /* 1500-1518B - standard */

	/* Oversized */
	if (bytes < 2000) return 18;     /* 1518-2000B - VLAN/small jumbo */
	return 19;                       /* >=2000B - jumbo frames */
}

/* Map PPS (packets per second) to histogram bucket (log scale) */
static inline int pps_to_bucket(uint32_t pps)
{
	if (pps < 10) return 0;
	if (pps < 50) return 1;
	if (pps < 100) return 2;
	if (pps < 500) return 3;
	if (pps < 1000) return 4;
	if (pps < 2000) return 5;
	if (pps < 5000) return 6;
	if (pps < 10000) return 7;
	if (pps < 20000) return 8;
	if (pps < 50000) return 9;
	if (pps < 100000) return 10;
	return 11;  /* >= 100K pps */
}

/* Update IPG tracking for a flow */
static void update_ipg(struct ipg_state *ipg, const struct timeval *pkt_time)
{
	if (!ipg->initialized) {
		ipg->last_pkt_time = *pkt_time;
		ipg->initialized = 1;
		return;
	}

	/* Calculate time delta from last packet */
	int64_t delta_us = (pkt_time->tv_sec - ipg->last_pkt_time.tv_sec) * 1000000LL +
	                   (pkt_time->tv_usec - ipg->last_pkt_time.tv_usec);

	/* Only track positive deltas (handle out-of-order timestamps) */
	if (delta_us > 0) {
		int bucket = ipg_to_bucket(delta_us);
		ipg->ipg_hist[bucket]++;
		ipg->ipg_samples++;
		ipg->ipg_sum_us += delta_us;
	}

	ipg->last_pkt_time = *pkt_time;
}

static int bytes_cmp(struct flow_hash *f1, struct flow_hash *f2)
{
	return (f2->f.bytes - f1->f.bytes);
}

/* t1 is the packet timestamp; deadline is the end of the current tick */
static int has_aged(struct timeval t1, struct timeval deadline)
{
	struct timeval expiretime = tv_add(t1, ref_window_size);

	return (tv_cmp(expiretime, deadline) < 0);
}

static void delete_pkt_from_ref_table(struct flow_record *fr)
{
	struct flow_hash *fte;

	HASH_FIND(r_hh, flow_ref_table,
	          &(fr->flow),
	          sizeof(struct flow), fte);
	assert(fte);

	fte->f.bytes -= fr->bytes;
	fte->f.packets -= fr->packets;

	assert(fte->f.bytes >= 0);
	assert(fte->f.packets >= 0);

	if (0 == fte->f.bytes) {
		HASH_DELETE(r_hh, flow_ref_table, fte);
		free(fte);
	}
}

/* remove pkt from the sliding window packet list as well as reference table */
static void delete_pkt_ring(struct flow_pkt *pkt)
{
	delete_pkt_from_ref_table(&pkt->flow_rec);

	totals.bytes -= pkt->flow_rec.bytes;
	totals.packets -= pkt->flow_rec.packets;

	assert(totals.bytes >= 0);
	assert(totals.packets >= 0);

	/* Ring buffer: increment tail to mark entry as freed */
	pkt_ring_tail++;
}

/*
 * remove the expired packets from the flow reference table,
 * and update totals for the sliding window reference interval
 *
 * NB: this must be called in both the packet receive and stats calculation
 * paths, because the total bytes/packets depend on the pkt_list and we don't
 * want to walk the whole list and redo the sum on every tick.
 */
static void expire_old_packets(struct timeval deadline)
{
	/* Ring buffer expiration: iterate from tail while entries are expired */
	while (pkt_ring_tail != pkt_ring_head) {
		struct flow_pkt *pkt = &pkt_ring[pkt_ring_tail & PKT_RING_MASK];
		if (has_aged(pkt->timestamp, deadline)) {
			delete_pkt_ring(pkt);
		} else {
			break;  /* Rest are newer, stop */
		}
	}

	/* Also expire old RTT, window, and video tracking entries */
	tcp_rtt_expire_old(deadline, ref_window_size);
	tcp_window_expire_old(deadline, ref_window_size);
	video_metrics_expire_old(deadline, ref_window_size);
}


static void clear_ref_table(void)
{
	/* Ring buffer: delete all entries from tail to head */
	while (pkt_ring_tail != pkt_ring_head) {
		struct flow_pkt *pkt = &pkt_ring[pkt_ring_tail & PKT_RING_MASK];
		delete_pkt_ring(pkt);
	}

	assert(totals.packets == 0);
	assert(totals.bytes == 0);
}

/*
 * Clear all the flow tables (reference table and interval tables), to purge
 * stale flows when restarting the thread (eg. switching interfaces)
 */
void clear_all_tables(void)
{
	/* clear ref table */
	clear_ref_table();

	/* clear interval tables */
	for (int i = 0; i < INTERVAL_COUNT; i++)
		clear_table(i);
}

/*
 * add the packet to the flow reference table.
 *
 * The reference table stores all the flows observed in a sliding window that
 * is as long as the longest of the period-on-period-type intervals.
 */
static void add_flow_to_ref_table(struct flow_pkt *pkt)
{
	struct flow_hash *fte;

	/* Store packet in ring buffer for sliding window byte counts.
	 * No malloc needed - just copy to next slot and advance head. */
	pkt_ring[pkt_ring_head & PKT_RING_MASK] = *pkt;
	pkt_ring_head++;

	/* Update the flow accounting table */
	/* id already in the hash? */
	HASH_FIND(r_hh, flow_ref_table, &(pkt->flow_rec.flow),
	          sizeof(struct flow), fte);
	if (!fte) {
		fte = (struct flow_hash *)malloc(sizeof(struct flow_hash));
		if (!fte)
			return;  /* Drop packet on allocation failure */
		memset(fte, 0, sizeof(struct flow_hash));
		memcpy(&(fte->f), &(pkt->flow_rec), sizeof(struct flow_record));
		HASH_ADD(r_hh, flow_ref_table, f.flow, sizeof(struct flow),
		         fte);
	} else {
		fte->f.bytes += pkt->flow_rec.bytes;
		fte->f.packets += pkt->flow_rec.packets;
	}

	/* Update IPG tracking for this flow */
	update_ipg(&fte->ipg, &pkt->timestamp);

	/* Update packet size histogram for this flow */
	uint32_t frame_size = (uint32_t)pkt->flow_rec.bytes;
	int size_bucket = pkt_size_to_bucket(frame_size);
	fte->f.pkt_size.frame_hist[size_bucket]++;
	fte->f.pkt_size.frame_samples++;
	fte->f.pkt_size.frame_sum += frame_size;
	fte->f.pkt_size.frame_sum_sq += (uint64_t)frame_size * frame_size;
	if (fte->f.pkt_size.frame_min == 0 || frame_size < fte->f.pkt_size.frame_min)
		fte->f.pkt_size.frame_min = frame_size;
	if (frame_size > fte->f.pkt_size.frame_max)
		fte->f.pkt_size.frame_max = frame_size;

	/* Track packet count for PPS calculation at interval boundary */
	fte->pkt_count_this_interval++;

	totals.bytes += pkt->flow_rec.bytes;
	assert(totals.bytes >= 0);
	totals.packets += pkt->flow_rec.packets;
	assert(totals.packets >= 0);
}

/*
 * add the packet to the period-on-period interval table for the selected
 * time series / interval.
 */
static void add_flow_to_interval(struct flow_pkt *pkt, int time_series)
{
	struct flow_hash *fte;

	/* Update the flow accounting table */
	/* id already in the hash? */
	HASH_FIND(ts_hh, incomplete_flow_tables[time_series],
	          &(pkt->flow_rec.flow), sizeof(struct flow), fte);
	if (!fte) {
		fte = (struct flow_hash *)malloc(sizeof(struct flow_hash));
		if (!fte)
			return;  /* Drop packet on allocation failure */
		memset(fte, 0, sizeof(struct flow_hash));
		memcpy(&(fte->f), &(pkt->flow_rec), sizeof(struct flow_record));
		/* Initialize window_min to UINT32_MAX so first sample sets it */
		fte->f.window.window_min = UINT32_MAX;
		HASH_ADD(ts_hh, incomplete_flow_tables[time_series], f.flow,
		         sizeof(struct flow), fte);
	} else {
		fte->f.bytes += pkt->flow_rec.bytes;
		fte->f.packets += pkt->flow_rec.packets;
	}

	/* Accumulate window stats if this is a TCP packet with valid window.
	 * This piggybacks on the existing hash lookup - no extra cost. */
	if (pkt->has_tcp_window) {
		uint32_t w = pkt->tcp_scaled_window;
		fte->f.window.window_sum += w;
		fte->f.window.window_samples++;
		if (w < fte->f.window.window_min)
			fte->f.window.window_min = w;
		if (w > fte->f.window.window_max)
			fte->f.window.window_max = w;
	}
}

/* Propagate events to all interval tables for this flow.
 * This mirrors how bytes/packets are accumulated via add_flow_to_interval().
 * Events are ORed into the flow entry in each interval table.
 */
static void propagate_events_to_intervals(const struct flow *flow, uint64_t events)
{
	if (events == 0)
		return;

	for (int i = 0; i < INTERVAL_COUNT; i++) {
		struct flow_hash *fte;
		HASH_FIND(ts_hh, incomplete_flow_tables[i],
		          flow, sizeof(struct flow), fte);
		if (fte) {
			fte->f.window.recent_events |= events;
		}
	}
}

static inline unsigned int rate_calc(struct timeval interval, int bytes)
{
	double dt = interval.tv_sec + interval.tv_usec * 1E-6;
	return (unsigned int)((float)bytes / dt);
}

static void fill_short_int_flows(struct flow_record st_flows[INTERVAL_COUNT],
                                 const struct flow_hash *ref_flow,
                                 struct timeval deadline)
{
	struct flow_hash *fti; /* flow table iter (short-interval tables */
	struct flow_hash *te;  /* flow table entry */

	/*
	 * PHASE 1: Populate per-interval data (bytes, packets, events) from interval tables.
	 * Each interval has its own complete_flow_tables[] entry containing data accumulated
	 * during that specific measurement period. Events (recent_events) are per-interval
	 * so that markers appear at their correct historical timestamps.
	 */
	for (int i = INTERVAL_COUNT - 1; i >= 0; i--) {
		fti = complete_flow_tables[i];
		memcpy(&st_flows[i], &(ref_flow->f),
		       sizeof(struct flow_record));

		if (!fti) {
			/* table doesn't have anything in it yet */
			st_flows[i].bytes = 0;
			st_flows[i].packets = 0;
			st_flows[i].window.recent_events = 0;
			continue;
		}

		/* try to find the reference flow in the short flow table */
		HASH_FIND(ts_hh, fti, &(ref_flow->f.flow),
		          sizeof(struct flow), te);

		st_flows[i].bytes = te ? te->f.bytes : 0;
		st_flows[i].packets = te ? te->f.packets : 0;
		/* Get events accumulated during this interval (like bytes/packets) */
		st_flows[i].window.recent_events = te ? te->f.window.recent_events : 0;

		/* Copy per-interval window stats if available */
		if (te && te->f.window.window_samples > 0) {
			/* Average window for this interval */
			st_flows[i].window.rwnd_bytes =
			    te->f.window.window_sum / te->f.window.window_samples;
			/* Copy raw stats for client use */
			st_flows[i].window.window_sum = te->f.window.window_sum;
			st_flows[i].window.window_samples = te->f.window.window_samples;
			st_flows[i].window.window_min = te->f.window.window_min;
			st_flows[i].window.window_max = te->f.window.window_max;
			/* Copy computed conditions */
			st_flows[i].window.window_conditions = te->f.window.window_conditions;
			st_flows[i].window.low_window_streak = te->f.window.low_window_streak;
		} else {
			/* No per-interval window data - will use fallback from tcp_window_get_info */
			st_flows[i].window.window_sum = 0;
			st_flows[i].window.window_samples = 0;
			st_flows[i].window.window_min = 0;
			st_flows[i].window.window_max = 0;
			st_flows[i].window.window_conditions = 0;
		}

		/* convert to bytes per second */
		st_flows[i].bytes =
		    rate_calc(tt_intervals[i], st_flows[i].bytes);
		/* convert to packets per second */
		st_flows[i].packets =
		    rate_calc(tt_intervals[i], st_flows[i].packets);
	}

	/*
	 * PHASE 2: Populate cached RTT and window info for this flow.
	 * This is done here (in the writer thread context) to avoid
	 * race conditions with the reader thread accessing the hash tables.
	 * We only need to do this once and copy to all intervals.
	 *
	 * IMPORTANT: st_flows[0] serves two roles:
	 * 1. "Live" snapshot: Gets current rwnd_bytes, window_scale, counts from tcp_window hash
	 * 2. Template: Other intervals copy these "live" values but keep their own recent_events
	 *
	 * The recent_events field must use per-interval data (from interval tables) for ALL
	 * intervals including [0], so markers appear at the correct historical timestamps.
	 */
	int64_t rtt_us;
	enum tcp_conn_state tcp_state;
	int saw_syn;
	struct tcp_window_info win_info;

	/* Save interval 0's per-interval events before we overwrite with
	 * tcp_window_get_info() data. We want per-interval events for consistency
	 * with other intervals, not cumulative events from the tcp_window hash.
	 */
	uint64_t interval0_events = st_flows[0].window.recent_events;

	/* Get RTT info */
	if (tcp_rtt_get_info(&ref_flow->f.flow, &rtt_us, &tcp_state, &saw_syn) == 0) {
		st_flows[0].rtt.rtt_us = rtt_us;
		st_flows[0].rtt.tcp_state = tcp_state;
		st_flows[0].rtt.saw_syn = saw_syn;
	} else {
		st_flows[0].rtt.rtt_us = -1;
		st_flows[0].rtt.tcp_state = -1;
		st_flows[0].rtt.saw_syn = 0;
	}

	/* Get window/congestion info - live snapshot values.
	 * NOTE: win_info.recent_events is CUMULATIVE (never cleared) from the tcp_window
	 * hash table. We DON'T use this for st_flows[].recent_events because markers
	 * need to appear at their historical timestamps. Per-interval events were
	 * already populated from interval tables above. We only use the counts/scale. */
	if (tcp_window_get_info(&ref_flow->f.flow, &win_info) == 0) {
		st_flows[0].window.rwnd_bytes = win_info.rwnd_bytes;
		st_flows[0].window.window_scale = win_info.window_scale;
		st_flows[0].window.zero_window_cnt = win_info.zero_window_count;
		st_flows[0].window.dup_ack_cnt = win_info.dup_ack_count;
		st_flows[0].window.retransmit_cnt = win_info.retransmit_count;
		st_flows[0].window.ece_cnt = win_info.ece_count;
		/* NOT copying: win_info.recent_events - see comment above */
	} else {
		st_flows[0].window.rwnd_bytes = -1;
		st_flows[0].window.window_scale = -1;
		st_flows[0].window.zero_window_cnt = 0;
		st_flows[0].window.dup_ack_cnt = 0;
		st_flows[0].window.retransmit_cnt = 0;
		st_flows[0].window.ece_cnt = 0;
	}

	/* Restore interval 0's per-interval events */
	st_flows[0].window.recent_events = interval0_events;

	/* Get TCP health info (RTT histogram and health status) */
	struct tcp_health_result health_result;
	if (tcp_rtt_get_health(&ref_flow->f.flow,
	                       st_flows[0].window.retransmit_cnt,
	                       ref_flow->f.packets,
	                       st_flows[0].window.zero_window_cnt,
	                       &health_result) == 0) {
		memcpy(st_flows[0].health.rtt_hist, health_result.rtt_hist,
		       sizeof(st_flows[0].health.rtt_hist));
		st_flows[0].health.rtt_samples = health_result.rtt_samples;
		st_flows[0].health.health_status = health_result.health_status;
		st_flows[0].health.health_flags = health_result.health_flags;
	} else {
		memset(&st_flows[0].health, 0, sizeof(st_flows[0].health));
	}

	/* Get video stream metrics (for UDP flows) */
	struct flow_video_info video_info;
	if (video_metrics_get(&ref_flow->f.flow, &video_info) == 0) {
		st_flows[0].video = video_info;
	} else {
		memset(&st_flows[0].video, 0, sizeof(st_flows[0].video));
	}

	/* Copy IPG histogram from flow tracking state */
	memcpy(st_flows[0].ipg.ipg_hist, ref_flow->ipg.ipg_hist,
	       sizeof(st_flows[0].ipg.ipg_hist));
	st_flows[0].ipg.ipg_samples = ref_flow->ipg.ipg_samples;
	st_flows[0].ipg.ipg_mean_us = ref_flow->ipg.ipg_samples > 0 ?
	                              ref_flow->ipg.ipg_sum_us / ref_flow->ipg.ipg_samples : 0;

	/*
	 * PHASE 3: Copy live snapshot to all intervals, preserving per-interval events.
	 * The live values (RTT, window size, counts, health, etc.) from PHASE 2 apply
	 * to all intervals. However, recent_events must remain per-interval so that
	 * markers appear at their correct historical timestamps.
	 *
	 * Note: Loop starts at i=1 because st_flows[0] already has live values and
	 * its recent_events was already restored after PHASE 2.
	 */
	for (int i = 1; i < INTERVAL_COUNT; i++) {
		/* Save interval-specific events (already read from interval table in PHASE 1) */
		uint64_t interval_events = st_flows[i].window.recent_events;
		st_flows[i].rtt = st_flows[0].rtt;
		st_flows[i].window = st_flows[0].window;
		st_flows[i].health = st_flows[0].health;
		st_flows[i].ipg = st_flows[0].ipg;
		st_flows[i].video = st_flows[0].video;
		/* Restore interval-specific events */
		st_flows[i].window.recent_events = interval_events;
	}
}

static void update_stats_tables(struct flow_pkt *pkt)
{
	/*
	 * expire the old packets in the receive path
	 * NB: must be called in stats path as well.
	 */
	expire_old_packets(pkt->timestamp);

	add_flow_to_ref_table(pkt);

	for (int i = 0; i < INTERVAL_COUNT; i++) {
		add_flow_to_interval(pkt, i);
	}
}

#define DEBUG 1
#if DEBUG
static void dbg_per_second(struct tt_top_flows *t5)
{
	double dt = ref_window_size.tv_sec + ref_window_size.tv_usec * 1E-6;

	printf("\rref window: %f, flows:  %ld total bytes:   %ld, Bps: %lu total packets: %ld, pps: %lu\n",
	dt, t5->flow_count, totals.bytes, t5->total_bytes, totals.packets, t5->total_packets);
}
#endif

static void tt_get_top5(struct tt_top_flows *t5, struct timeval deadline)
{
	struct flow_hash *rfti; /* reference flow table iter */

	/* sort the flow reference table by byte count */
	HASH_SRT(r_hh, flow_ref_table, bytes_cmp);

	/*
	 * Expire old packets in the output path.
	 * NB: must be called in packet receive path as well.
	 */
	expire_old_packets(deadline);

	/* Check if the interval is complete and then rotate tables */
	expire_old_interval_tables(deadline);

	/* For each of the top N flows in the reference table,
	 * fill the counts from the short-interval flow tables. */
	rfti = flow_ref_table;
	for (int i = 0; i < MAX_FLOW_COUNT && rfti; i++) {
		fill_short_int_flows(t5->flow[i], rfti, deadline);
		rfti = rfti->r_hh.next;
	}

	t5->flow_count = HASH_CNT(r_hh, flow_ref_table);
	t5->total_bytes = rate_calc(ref_window_size, totals.bytes);
	t5->total_packets = rate_calc(ref_window_size, totals.packets);
	t5->timestamp = deadline;

#if DEBUG
	if (t5->flow_count == 0 &&
	    (t5->total_bytes > 0 || t5->total_packets > 0)) {
		fprintf(stderr, "logic error in %s. flows is 0, but bytes and packets are > 0\n", __func__);
		dbg_per_second(t5);
		assert(0);
	} else if (t5->flow_count > 0 && totals.bytes == 0) {
		fprintf(stderr, "logic error in %s. flows is >0, but bytes are 0\n", __func__);
		dbg_per_second(t5);
		assert(0);
	} else if (t5->flow_count > 0 && totals.packets == 0) {
		fprintf(stderr, "logic error in %s. flows is >0, but packets are 0\n", __func__);
		dbg_per_second(t5);
		assert(0);
	}
#endif
}

int tt_get_flow_count(void)
{
	return HASH_CNT(r_hh, flow_ref_table);
}

void tt_update_ref_window_size(struct tt_thread_info *ti, struct timeval t)
{
	pthread_mutex_lock(&ti->t5_mutex);
	ref_window_size = t;
	pthread_mutex_unlock(&ti->t5_mutex);
}

/* Find UDP payload in packet for video stream detection.
 * Returns pointer to UDP payload or NULL if not found.
 * Also sets payload_len to the UDP payload length.
 */
static const uint8_t *find_udp_payload(const struct pcap_pkthdr *h,
                                       const uint8_t *wirebits,
                                       int datalink_type,
                                       size_t *payload_len)
{
	const uint8_t *ptr = wirebits;
	const uint8_t *end_of_packet = wirebits + h->caplen;
	uint16_t ethertype;

	*payload_len = 0;

	/* Skip link layer header based on datalink type */
	if (datalink_type == DLT_EN10MB) {
		const struct hdr_ethernet *eth = (const struct hdr_ethernet *)ptr;
		ethertype = ntohs(eth->type);

		if (ethertype == VLAN_TPID) {
			ptr += HDR_LEN_ETHER_VLAN;
			ethertype = ntohs(eth->tagged_type);
		} else {
			ptr += HDR_LEN_ETHER;
		}
	} else if (datalink_type == DLT_LINUX_SLL) {
		const struct sll_header *sll = (const struct sll_header *)ptr;
		ethertype = ntohs(sll->sll_protocol);
		ptr += SLL_HDR_LEN;
	} else {
		return NULL;
	}

	/* Skip IP header */
	if (ethertype == ETHERTYPE_IP) {
		const struct hdr_ipv4 *ip = (const struct hdr_ipv4 *)ptr;
		if (ip->ip_p != IPPROTO_UDP)
			return NULL;
		unsigned int ip_hdr_len = IP_HL(ip) * 4;
		ptr += ip_hdr_len;
	} else if (ethertype == ETHERTYPE_IPV6) {
		const struct hdr_ipv6 *ip6 = (const struct hdr_ipv6 *)ptr;
		uint8_t next_hdr = ip6->next_hdr;
		ptr += sizeof(struct hdr_ipv6);

		/* Skip extension headers to find UDP */
		while (ptr < end_of_packet) {
			if (next_hdr == IPPROTO_UDP) {
				break;
			}
			if (next_hdr == IPPROTO_HOPOPTS ||
			    next_hdr == IPPROTO_ROUTING ||
			    next_hdr == IPPROTO_FRAGMENT ||
			    next_hdr == IPPROTO_DSTOPTS) {
				if (ptr + 2 > end_of_packet)
					return NULL;
				next_hdr = ptr[0];
				uint8_t hdr_len = ptr[1];
				size_t ext_len = (hdr_len + 1) * 8;
				ptr += ext_len;
			} else {
				return NULL;
			}
		}
		if (next_hdr != IPPROTO_UDP)
			return NULL;
	} else {
		return NULL;
	}

	if (ptr + sizeof(struct hdr_udp) > end_of_packet)
		return NULL;

	/* Skip UDP header (8 bytes) to get to payload */
	const struct hdr_udp *udp = (const struct hdr_udp *)ptr;
	uint16_t udp_len = ntohs(udp->ip_len);

	ptr += sizeof(struct hdr_udp);

	/* Calculate actual payload length */
	if (udp_len > sizeof(struct hdr_udp)) {
		*payload_len = udp_len - sizeof(struct hdr_udp);
		/* Clamp to actual captured data */
		if (ptr + *payload_len > end_of_packet) {
			*payload_len = end_of_packet - ptr;
		}
	}

	if (*payload_len == 0)
		return NULL;

	return ptr;
}

/* Find TCP header in packet for RTT tracking.
 * Returns pointer to TCP header or NULL if not found.
 * Also sets end_of_packet pointer for payload length calculation.
 */
static const struct hdr_tcp *find_tcp_header(const struct pcap_pkthdr *h,
                                             const uint8_t *wirebits,
                                             int datalink_type,
                                             const uint8_t **end_of_packet)
{
	const uint8_t *ptr = wirebits;
	*end_of_packet = wirebits + h->caplen;
	uint16_t ethertype;

	/* Skip link layer header based on datalink type */
	if (datalink_type == DLT_EN10MB) {
		const struct hdr_ethernet *eth = (const struct hdr_ethernet *)ptr;
		ethertype = ntohs(eth->type);

		if (ethertype == VLAN_TPID) {
			ptr += HDR_LEN_ETHER_VLAN;
			ethertype = ntohs(eth->tagged_type);
		} else {
			ptr += HDR_LEN_ETHER;
		}
	} else if (datalink_type == DLT_LINUX_SLL) {
		const struct sll_header *sll = (const struct sll_header *)ptr;
		ethertype = ntohs(sll->sll_protocol);
		ptr += SLL_HDR_LEN;
	} else {
		return NULL;
	}

	/* Skip IP header */
	if (ethertype == ETHERTYPE_IP) {
		const struct hdr_ipv4 *ip = (const struct hdr_ipv4 *)ptr;
		if (ip->ip_p != IPPROTO_TCP)
			return NULL;
		unsigned int ip_hdr_len = IP_HL(ip) * 4;
		ptr += ip_hdr_len;
	} else if (ethertype == ETHERTYPE_IPV6) {
		const struct hdr_ipv6 *ip6 = (const struct hdr_ipv6 *)ptr;
		uint8_t next_hdr = ip6->next_hdr;
		ptr += sizeof(struct hdr_ipv6);

		/* Skip extension headers to find TCP */
		while (ptr < *end_of_packet) {
			if (next_hdr == IPPROTO_TCP) {
				break;  /* Found TCP */
			}
			/* Handle known extension headers */
			if (next_hdr == IPPROTO_HOPOPTS ||
			    next_hdr == IPPROTO_ROUTING ||
			    next_hdr == IPPROTO_FRAGMENT ||
			    next_hdr == IPPROTO_DSTOPTS) {
				if (ptr + 2 > *end_of_packet)
					return NULL;
				next_hdr = ptr[0];
				uint8_t hdr_len = ptr[1];
				size_t ext_len = (hdr_len + 1) * 8;
				ptr += ext_len;
			} else {
				/* Not TCP and not a known extension header */
				return NULL;
			}
		}
		if (next_hdr != IPPROTO_TCP)
			return NULL;
	} else {
		return NULL;
	}

	if (ptr >= *end_of_packet)
		return NULL;

	return (const struct hdr_tcp *)ptr;
}

static void handle_packet(uint8_t *user, const struct pcap_pkthdr *pcap_hdr,
                          const uint8_t *wirebits)
{
	struct pcap_handler_user *cbdata = (struct pcap_handler_user *)user;
	char errstr[DECODE_ERRBUF_SIZE];
	struct flow_pkt pkt = { 0 };

	/* Store raw packet in rolling buffer for pcap capture */
	PCAP_BUF_STORE(pcap_hdr, wirebits);

	if (0 == cbdata->decoder(pcap_hdr, wirebits, &pkt, errstr)) {
		update_stats_tables(&pkt);

		/* Process TCP packets for RTT tracking */
		if (pkt.flow_rec.flow.proto == IPPROTO_TCP) {
			const uint8_t *end_of_packet = wirebits + pcap_hdr->caplen;
			const struct hdr_tcp *tcp_hdr;

			/* Use stored L4 offset if available (avoids re-parsing headers) */
			if (pkt.has_l4_offset) {
				tcp_hdr = (const struct hdr_tcp *)(wirebits + pkt.l4_offset);
			} else {
				/* Fallback to find_tcp_header for edge cases */
				tcp_hdr = find_tcp_header(pcap_hdr, wirebits,
				                          cbdata->datalink_type,
				                          &end_of_packet);
			}
			if (tcp_hdr) {
				struct flow_pkt_tcp tcp_pkt = { 0 };
				if (0 == decode_tcp_extended(tcp_hdr, end_of_packet,
				                             &tcp_pkt, errstr)) {
					tcp_rtt_process_packet(&pkt.flow_rec.flow,
					                       tcp_pkt.seq,
					                       tcp_pkt.ack,
					                       tcp_pkt.flags,
					                       tcp_pkt.payload_len,
					                       pkt.timestamp);
					uint32_t scaled_window = 0;
					uint64_t tcp_events = tcp_window_process_packet(
					                          &pkt.flow_rec.flow,
					                          (const uint8_t *)tcp_hdr,
					                          end_of_packet,
					                          tcp_pkt.seq,
					                          tcp_pkt.ack,
					                          tcp_pkt.flags,
					                          tcp_pkt.window,
					                          tcp_pkt.payload_len,
					                          pkt.timestamp,
					                          &scaled_window);

					/* Store scaled window in pkt for interval accumulation */
					pkt.tcp_scaled_window = scaled_window;
					pkt.has_tcp_window = 1;

					/* Propagate events to the REVERSE flow's interval tables.
					 * TCP window/congestion events detected in packets FROM host A
					 * affect the flow TO host A (the sender can't send more).
					 * E.g., zero-window advertised by server in server->client packets
					 * should appear on client->server flow (where server's window is shown).
					 */
					if (tcp_events != 0) {
						struct flow rev_flow = flow_reverse(&pkt.flow_rec.flow);
						propagate_events_to_intervals(&rev_flow, tcp_events);
					}

					/* Process RTSP traffic on port 554 */
					if (tcp_pkt.payload_len > 0 &&
					    (pkt.flow_rec.flow.sport == 554 ||
					     pkt.flow_rec.flow.dport == 554)) {
						/* Calculate TCP payload pointer */
						unsigned int tcp_hdr_len = TH_OFF(tcp_hdr) * 4;
						const uint8_t *tcp_payload =
						    (const uint8_t *)tcp_hdr + tcp_hdr_len;
						if (tcp_payload + tcp_pkt.payload_len <= end_of_packet) {
							uint64_t ts_ns =
							    (uint64_t)pkt.timestamp.tv_sec * 1000000000ULL +
							    (uint64_t)pkt.timestamp.tv_usec * 1000ULL;
							rtsp_tap_process_packet(&g_rtsp_tap,
							                        &pkt.flow_rec.flow,
							                        tcp_payload,
							                        tcp_pkt.payload_len,
							                        ts_ns);
						}
					}
				}
			}
		}

		/* Process UDP packets for video stream detection and metrics */
		if (pkt.flow_rec.flow.proto == IPPROTO_UDP) {
			size_t udp_payload_len;
			const uint8_t *udp_payload = find_udp_payload(pcap_hdr,
			                                              wirebits,
			                                              cbdata->datalink_type,
			                                              &udp_payload_len);
			if (udp_payload && udp_payload_len > 0) {
				struct flow_video_info video_info;
				enum video_stream_type vtype =
				    video_detect(udp_payload, udp_payload_len, &video_info);

					if (vtype == VIDEO_STREAM_RTP) {
					/* Process RTP metrics */
					video_metrics_rtp_process(&pkt.flow_rec.flow,
					                          &video_info.rtp,
					                          pkt.timestamp);

					/* Extract RTP payload for frame/keyframe tracking */
					size_t rtp_hdr_size = RTP_HDR_MIN_SIZE +
					    (RTP_CSRC_COUNT((struct hdr_rtp *)udp_payload) * 4);
					if (RTP_EXTENSION((struct hdr_rtp *)udp_payload) &&
					    udp_payload_len > rtp_hdr_size + 4) {
						uint16_t ext_len = ntohs(*(uint16_t *)(udp_payload + rtp_hdr_size + 2));
						rtp_hdr_size += 4 + (ext_len * 4);
					}

					if (udp_payload_len > rtp_hdr_size) {
						const uint8_t *rtp_payload = udp_payload + rtp_hdr_size;
						size_t rtp_payload_len = udp_payload_len - rtp_hdr_size;
						uint32_t rtp_ts = video_detect_get_rtp_timestamp(
						    udp_payload, udp_payload_len);

						/* Detect keyframe for frame counting */
						int is_keyframe = video_detect_is_keyframe(
						    rtp_payload, rtp_payload_len, video_info.rtp.codec);

						/* Update frame metrics (called per-packet for bitrate) */
						video_metrics_update_frame(&pkt.flow_rec.flow,
						                           video_info.rtp.ssrc,
						                           is_keyframe,
						                           rtp_ts,
						                           udp_payload_len);
					}

					/* Forward to VLC passthrough if callback registered */
					if (rtp_forward_callback) {
						rtp_forward_callback(&pkt.flow_rec.flow,
						                     udp_payload,
						                     udp_payload_len);
					}
				} else if (vtype == VIDEO_STREAM_MPEG_TS) {
					/* Process MPEG-TS metrics */
					video_metrics_mpegts_process(&pkt.flow_rec.flow,
					                             &video_info.mpegts,
					                             udp_payload,
					                             udp_payload_len);
				} else {
					/* Try audio detection if no video detected */
					struct rtp_info audio_info;
					if (audio_detect_rtp(udp_payload, udp_payload_len, &audio_info)) {
						/* Process RTP audio metrics */
						video_metrics_rtp_process(&pkt.flow_rec.flow,
						                          &audio_info,
						                          pkt.timestamp);
						/* Store audio info in video union (reuses RTP fields) */
						video_info.stream_type = VIDEO_STREAM_RTP;
						video_info.rtp = audio_info;
					}
				}

				/* Store in packet's flow record for aggregation */
				pkt.flow_rec.video = video_info;
			}
		}

		cbdata->result.err = 0;
	} else {
		cbdata->result.err = -1;
		snprintf(cbdata->result.errstr, DECODE_ERRBUF_SIZE, "%s", errstr);
	}
}

static int init_pcap(char **dev, struct pcap_info *pi)
{
	char errbuf[PCAP_ERRBUF_SIZE];
	int dlt; /* pcap data link type */
	pcap_if_t *alldevs;

	if (!*dev) {
		int err = pcap_findalldevs(&alldevs, errbuf);
		if (err) {
			fprintf(stderr, "Couldn't list devices: %s\n", errbuf);
			return 1;
		}

		if (!alldevs) {
			fprintf(stderr,
			        "No devices available. Check permissions.\n");
			return 1;
		}

		*dev = strdup(alldevs->name);
		pcap_freealldevs(alldevs);
	}

	if (*dev == NULL) {
		fprintf(stderr, "Couldn't find default device: %s\n", errbuf);
		return 1;
	}

	pi->handle = pcap_open_live(*dev, BUFSIZ, 1, 3, errbuf);
	if (pi->handle == NULL) {
		fprintf(stderr, "Couldn't open device %s\n", errbuf);
		return 1;
	}

	dlt = pcap_datalink(pi->handle);
	pi->decoder_cbdata.datalink_type = dlt;
	switch (dlt) {
	case DLT_EN10MB:
		pi->decoder_cbdata.decoder = decode_ethernet;
		break;
	case DLT_LINUX_SLL:
		pi->decoder_cbdata.decoder = decode_linux_sll;
		break;
	default:
		fprintf(stderr, "Device %s doesn't provide Ethernet headers - "
		                "not supported\n",
		        *dev);
		return 1;
	}

	/* Notify server of interface/datalink change if callback registered */
	if (pcap_iface_callback) {
		pcap_iface_callback(dlt);
	}

	if (pcap_setnonblock(pi->handle, 1, errbuf) != 0) {
		fprintf(stderr, "Non-blocking mode failed: %s\n", errbuf);
		return 1;
	}

	pi->selectable_fd = pcap_get_selectable_fd(pi->handle);
	if (-1 == pi->selectable_fd) {
		fprintf(stderr, "pcap handle not selectable.\n");
		return 1;
	}
	return 0;
}

static int free_pcap(struct pcap_info *pi)
{
	pcap_close(pi->handle);
	return 0;
}

static void set_affinity(struct tt_thread_info *ti)
{
	int s, j;
	cpu_set_t cpuset;
	pthread_t thread;
	thread = pthread_self();

	/* Set affinity mask to include CPUs 1 only */
	CPU_ZERO(&cpuset);
#ifndef RT_CPU
#define RT_CPU 0
#endif
	CPU_SET(RT_CPU, &cpuset);
	s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (s != 0) {
		handle_error_en(s, "pthread_setaffinity_np");
	}

	/* Check the actual affinity mask assigned to the thread */
	s = pthread_getaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (s != 0) {
		handle_error_en(s, "pthread_getaffinity_np");
	}

	printf("RT thread [%s] priority [%d] CPU affinity: ",
	       ti->thread_name,
	       ti->thread_prio);
	for (j = 0; j < CPU_SETSIZE; j++) {
		if (CPU_ISSET(j, &cpuset)) {
			printf(" CPU%d", j);
		}
	}
	printf("\n");
}

static int init_realtime(struct tt_thread_info *ti)
{
	struct sched_param schedparm;
	int ret;

	memset(&schedparm, 0, sizeof(schedparm));
	schedparm.sched_priority = ti->thread_prio;
	ret = sched_setscheduler(0, SCHED_FIFO, &schedparm);
	if (ret != 0) {
		fprintf(stderr,
		        "[%s] Failed to set SCHED_FIFO priority %d: %s. "
		        "Running without real-time scheduling. "
		        "Grant CAP_SYS_NICE for RT priority.\n",
		        ti->thread_name, ti->thread_prio,
		        strerror(errno));
	}
	set_affinity(ti);
	return 0;
}

static struct timeval ts_to_tv(struct timespec t_in)
{
	struct timeval t_out;

	t_out.tv_sec = t_in.tv_sec;
	t_out.tv_usec = t_in.tv_nsec / 1000;
	return t_out;
}

void *tt_intervals_run(void *p)
{
	struct pcap_handler_user *cbdata;
	struct tt_thread_info *ti = (struct tt_thread_info *)p;

	assert(ti);

	init_realtime(ti);

	assert(ti->priv);
	assert(ti->priv->pi.handle);
	assert(ti->priv->pi.selectable_fd);
	assert(ti->priv->pi.decoder_cbdata.decoder);

	cbdata = &ti->priv->pi.decoder_cbdata;

	struct timespec deadline;
	struct timespec interval = { .tv_sec = 0, .tv_nsec = 1E6 };

	clock_gettime(CLOCK_MONOTONIC, &deadline);
	init_intervals(ts_to_tv(deadline));

	/*
	 * We need the 1ms tick for the stats update!
	 * The 1ms has to be split between receiving packets and
	 * stats calculations, as follows:
	 *
	 * max pcap_dispatch + tt_get_top5 + mutex contention < 1ms tick
	 *            ~500us +      ~500ms +              ??? < 1ms
	 *
	 * If pcap_dispatch or tt_get_top5 takes too long,
	 * the deadline will be missed and the stats will be wrong.
	 *
	 * 1Gbps Ethernet line rate is 1.5Mpps - 666ns/pkt
	 * Say the time budget for processing packets is
	 * roughly 500ns/pkt (should be less, but unknown and
	 * machine dependent), then to cap the processing to
	 * 500us total is 1000pkts.
	 */
	int cnt, max = 1000;

	while (1) {
		deadline = ts_add(deadline, interval);

		/* Double-buffer: write to non-published buffer, then publish */
		int write_idx = atomic_load_explicit(&ti->t5_write_idx,
		                                     memory_order_relaxed);
		struct tt_top_flows *write_buf = &ti->t5_buffers[write_idx];

		tt_get_top5(write_buf, ts_to_tv(deadline));

		/* Publish: atomically update pointer for readers */
		atomic_store_explicit(&ti->t5, write_buf, memory_order_release);

		/* Swap write index for next iteration */
		atomic_store_explicit(&ti->t5_write_idx, 1 - write_idx,
		                      memory_order_relaxed);

		cnt = pcap_dispatch(ti->priv->pi.handle, max,
		                    handle_packet, (u_char *)cbdata);
		if (cnt && cbdata->result.err) {
			/* FIXME: think of an elegant way to
			 * get the errors out of this thread. */
			ti->decode_errors++;
		}
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
	}

	/* close the pcap session */
	pcap_close(ti->priv->pi.handle);
	return NULL;
}

int tt_intervals_init(struct tt_thread_info *ti)
{
	int err;
	pthread_mutex_init(&(ti->t5_mutex), NULL);

	ref_window_size = (struct timeval){.tv_sec = 3, .tv_usec = 0 };
	flow_ref_table = NULL;

	/* Reset ring buffer indices */
	pkt_ring_head = 0;
	pkt_ring_tail = 0;

	/* Initialize TCP RTT, window, video metrics, and RTSP tap tracking */
	tcp_rtt_init();
	tcp_window_init();
	video_metrics_init();
	rtsp_tap_init(&g_rtsp_tap);

	/* Initialize double-buffer: clear both buffers, start writing to [0] */
	memset(ti->t5_buffers, 0, sizeof(ti->t5_buffers));
	atomic_store(&ti->t5, NULL);
	atomic_store(&ti->t5_write_idx, 0);

	ti->priv = calloc(1, sizeof(struct tt_thread_private));
	if (!ti->priv) { return 1; }

	err = init_pcap(&(ti->dev), &(ti->priv->pi));
	if (err)
		goto cleanup;

	totals.bytes = 0;
	totals.packets = 0;
	return 0;

cleanup:
	free(ti->priv);
	return 1;
}

int tt_intervals_free(struct tt_thread_info *ti)
{
	assert(ti);
	assert(ti->priv);

	clear_all_tables();

	/* Cleanup TCP RTT, window, and video metrics tracking */
	tcp_rtt_cleanup();
	tcp_window_cleanup();
	video_metrics_cleanup();

	free_pcap(&(ti->priv->pi));
	free(ti->priv);

	/* Reset double-buffer state (buffers are static, no free needed) */
	atomic_store(&ti->t5, NULL);
	atomic_store(&ti->t5_write_idx, 0);
	return 0;
}
