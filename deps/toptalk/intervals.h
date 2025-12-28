#ifndef INTERVALS_H
#define INTERVALS_H

#include <pcap.h>
#include <stdatomic.h>
#include "utlist.h"
#include "uthash.h"
#include "decode.h"


/* ================================= NOTE: ===================================
 * INTERVAL_COUNT and MAX_FLOW_COUNT must be defined by the user at compile
 * time using CFLAGS like -DINTERVAL_COUNT=nnn and -DMAX_FLOW_COUNT=mmm
 */

/* intvervals[] must be defined in intervals_user.c */
extern struct timeval const tt_intervals[INTERVAL_COUNT];

struct tt_top_flows {
	struct timeval timestamp;
	int64_t flow_count;
	int64_t total_bytes;
	int64_t total_packets;
	struct flow_record flow[MAX_FLOW_COUNT][INTERVAL_COUNT];
};

/* forward declaration; definition and use is internal to tt thread */
struct tt_thread_private;

struct tt_thread_info {
	pthread_t thread_id;
	pthread_attr_t attr;
        char *dev;
	/* Double-buffer for lock-free reader access to top flows.
	 * Writer fills t5_buffers[t5_write_idx], then publishes via atomic
	 * pointer swap to t5. Readers atomically load t5 without locking.
	 */
	struct tt_top_flows t5_buffers[2];
	_Atomic(struct tt_top_flows *) t5;
	_Atomic int t5_write_idx;
	/* Mutex retained only for restart synchronization */
	pthread_mutex_t t5_mutex;
	unsigned int decode_errors;
	const char * const thread_name;
	const int thread_prio;
	int thread_state;
	struct tt_thread_private *priv;
};

void tt_update_ref_window_size(struct tt_thread_info *ti, struct timeval t);
int tt_get_flow_count(void);
void *tt_intervals_run(void *p);
int tt_intervals_init(struct tt_thread_info *ti);
int tt_intervals_free(struct tt_thread_info *ti);

/*
 * Runtime-configurable ring buffer for high-rate packet capture.
 *
 * The ring buffer stores compact packet entries (64 bytes each, cache-line
 * aligned) for sliding window byte/packet counting. Size determines max
 * sustained packet rate:
 *
 *   max_pps = ring_size / window_seconds
 *
 * Examples with 3-second window:
 *   262K entries (default): ~87K pps
 *   4M entries:             ~1.3M pps
 *   32M entries:            ~10M pps (requires ~2GB, uses hugepages)
 *
 * Must be called before tt_intervals_init(). Size must be a power of 2.
 * Returns 0 on success, -1 on error (invalid size or allocation failure).
 * Uses hugepages for allocations >= 2MB when available.
 */
int tt_set_ring_size(size_t entries);
size_t tt_get_ring_size(void);

/* Default ring size if tt_set_ring_size() not called */
#ifndef TT_DEFAULT_RING_SIZE
#define TT_DEFAULT_RING_SIZE (1 << 18)  /* 262144 entries */
#endif

/* Optional callbacks for packet capture integration */
void tt_set_pcap_callback(void (*store_cb)(const struct pcap_pkthdr *, const uint8_t *),
                          void (*iface_cb)(int dlt));

/* Optional callback for VLC passthrough (RTP packet forwarding) */
typedef void (*tt_rtp_forward_cb)(const struct flow *f, const uint8_t *rtp_data, size_t rtp_len);
void tt_set_rtp_forward_callback(tt_rtp_forward_cb cb);

#define handle_error_en(en, msg) \
        do {                                                                   \
                errno = en;                                                    \
                perror(msg);                                                   \
                exit(EXIT_FAILURE);                                            \
        } while (0)

/*
 * Benchmark hooks for performance testing.
 * These expose internal functions without pcap/thread overhead.
 */

/* Initialize data structures for benchmarking (no pcap needed) */
int tt_bench_init(void);

/* Cleanup after benchmarking */
void tt_bench_cleanup(void);

/* Process a decoded packet through the stats update path.
 * This is the hot path that runs for every captured packet. */
void tt_bench_update_stats(struct flow_pkt *pkt);

#endif
