#ifndef TCP_WINDOW_H
#define TCP_WINDOW_H

#include <stdint.h>
#include <stdatomic.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <net/ethernet.h>
#include "uthash.h"
#include "flow.h"
#include "tcp_rtt.h"  /* For tcp_flow_key */

/* TCP Options */
#define TCP_OPT_EOL       0
#define TCP_OPT_NOP       1
#define TCP_OPT_MSS       2
#define TCP_OPT_WSCALE    3
#define TCP_OPT_SACK_OK   4
#define TCP_OPT_SACK      5
#define TCP_OPT_TIMESTAMP 8

/* Window scale status */
enum window_scale_status {
	WSCALE_UNKNOWN = 0,    /* Haven't seen SYN, scale factor unknown */
	WSCALE_SEEN,           /* SYN seen, scale factor captured */
	WSCALE_NOT_PRESENT     /* SYN seen but no window scale option */
};

/* Congestion event bitmask flags */
#define CONG_EVENT_ZERO_WINDOW  0x01
#define CONG_EVENT_DUP_ACK      0x02
#define CONG_EVENT_RETRANSMIT   0x04
#define CONG_EVENT_ECE          0x08
#define CONG_EVENT_CWR          0x10

/* Per-direction window and congestion tracking
 * Fields accessed by reader thread are atomic for lock-free access.
 */
struct tcp_window_direction {
	/* Window tracking (atomic for lock-free reader access) */
	_Atomic uint32_t raw_window;      /* Current raw window value (16-bit from header) */
	_Atomic uint32_t scaled_window;   /* Window after applying scale factor */
	_Atomic uint8_t window_scale;     /* Scale factor (0-14, from SYN) */
	_Atomic int scale_status;         /* enum window_scale_status */

	/* Window statistics */
	uint32_t min_window;
	uint32_t max_window;

	/* Zero-window tracking (atomic for lock-free reader access) */
	_Atomic uint32_t zero_window_count;    /* Number of zero-window advertisements */
	int in_zero_window;                    /* State for edge-triggered detection (writer only) */
	int recovered_from_zero;               /* Window recovered above threshold (writer only) */

	/* Duplicate ACK detection */
	uint32_t last_ack;             /* Last ACK number seen (writer only) */
	uint32_t dup_ack_streak;       /* Consecutive duplicate ACKs (writer only) */
	_Atomic uint32_t dup_ack_events;       /* Count of 3+ dup ACK bursts (lock-free) */

	/* Retransmission detection */
	uint32_t highest_seq_seen;     /* Highest sequence number sent (writer only) */
	int highest_seq_valid;         /* Whether highest_seq_seen is initialized (writer only) */
	_Atomic uint32_t retransmit_count;     /* Packets with seq < highest_seq (lock-free) */

	/* ECN tracking (atomic for lock-free reader access) */
	_Atomic uint32_t ece_count;            /* ECE flag count */
	_Atomic uint32_t cwr_count;            /* CWR flag count */

	/* Events since last query (for UI markers) */
	_Atomic uint64_t recent_events;        /* Bitmask of all events ever (lock-free) */

	/* Event timestamps for interval-aware queries */
	struct timeval last_zero_window_time;
	struct timeval last_dup_ack_time;
	struct timeval last_retransmit_time;
	struct timeval last_ece_time;
	struct timeval last_cwr_time;
};

/* Bidirectional TCP window entry - one per TCP connection */
struct tcp_window_entry {
	struct tcp_flow_key key;       /* Same canonical key as RTT tracking */
	struct tcp_window_direction fwd;  /* lo->hi direction */
	struct tcp_window_direction rev;  /* hi->lo direction */
	struct timeval last_activity;
	UT_hash_handle hh;
};

/* Result structure for single-lookup query */
struct tcp_window_info {
	int64_t rwnd_bytes;            /* Current advertised window, -1 if unknown */
	int32_t window_scale;          /* Scale factor, -1 if unknown */
	uint32_t zero_window_count;
	uint32_t dup_ack_count;
	uint32_t retransmit_count;
	uint32_t ece_count;
	uint32_t cwr_count;
	uint64_t recent_events;        /* Bitmask of events since last query */
};

/* Initialize window tracking subsystem */
void tcp_window_init(void);

/* Cleanup window tracking subsystem */
void tcp_window_cleanup(void);

/* Process a TCP packet for window/congestion tracking
 * tcp_header: pointer to start of TCP header
 * end_of_packet: pointer to end of packet (for bounds checking options)
 * seq: TCP sequence number
 * ack: TCP acknowledgement number
 * flags: TCP flags
 * window: raw window field from TCP header
 * payload_len: length of TCP payload (excluding headers)
 * timestamp: packet capture timestamp
 * scaled_window_out: output parameter for scaled window value (can be NULL)
 *
 * Returns: bitmask of CONG_EVENT_* flags detected for this packet
 *          (uint64_t for efficiency and future expansion)
 */
uint64_t tcp_window_process_packet(const struct flow *flow,
                                   const uint8_t *tcp_header,
                                   const uint8_t *end_of_packet,
                                   uint32_t seq,
                                   uint32_t ack,
                                   uint8_t flags,
                                   uint16_t window,
                                   uint16_t payload_len,
                                   struct timeval timestamp,
                                   uint32_t *scaled_window_out);

/* Get window and congestion info for a flow (single lookup)
 * Returns 0 on success (entry found), -1 if flow not found or not TCP
 */
int tcp_window_get_info(const struct flow *flow,
                        struct tcp_window_info *info);

/* Get events that occurred within a lookback period (for interval-specific queries)
 * Returns bitmask of CONG_EVENT_* flags for events where:
 *   (current_time - event_time) < lookback_period
 * This allows each interval to see only events within its time window.
 */
uint64_t tcp_window_get_events_for_period(const struct flow *flow,
                                          struct timeval current_time,
                                          struct timeval lookback_period);

/* Expire old window entries that haven't been active within window */
void tcp_window_expire_old(struct timeval deadline, struct timeval window);

#endif /* TCP_WINDOW_H */
