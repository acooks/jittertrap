#ifndef TCP_RTT_H
#define TCP_RTT_H

#include <stdint.h>
#include <stdatomic.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <net/ethernet.h>
#include "uthash.h"
#include "flow.h"

/* Maximum outstanding sequence numbers to track per direction */
#define MAX_SEQ_ENTRIES 16

/* EWMA alpha value (1/8 = 0.125, same as Linux TCP) */
#define RTT_EWMA_ALPHA_SHIFT 3

/* TCP connection states */
enum tcp_conn_state {
	TCP_STATE_UNKNOWN = 0,    /* Haven't seen enough to determine state */
	TCP_STATE_SYN_SEEN,       /* SYN observed - new connection */
	TCP_STATE_ACTIVE,         /* Connection is active (data flowing) */
	TCP_STATE_FIN_WAIT,       /* FIN seen, waiting for FIN-ACK */
	TCP_STATE_CLOSED,         /* FIN/FIN-ACK complete or RST seen */
};

/* TCP flags we care about */
#define TCP_FLAG_FIN  0x01
#define TCP_FLAG_SYN  0x02
#define TCP_FLAG_RST  0x04
#define TCP_FLAG_ACK  0x10

/* Structure for tracking an outstanding sequence number */
struct seq_entry {
	uint32_t seq_end;           /* Expected ACK value (seq + payload_len) */
	struct timeval timestamp;   /* When the segment was sent */
};

/* RTT histogram bucket count - log-scale from 0us to >1s */
#define RTT_HIST_BUCKETS 14

/* RTT state for one direction of a TCP connection
 * Fields accessed by reader thread are atomic for lock-free access.
 * Writer thread updates these atomically; reader thread reads atomically.
 */
struct tcp_rtt_direction {
	struct seq_entry pending_seqs[MAX_SEQ_ENTRIES];
	int seq_head;               /* Circular buffer head (writer only) */
	int seq_count;              /* Number of entries in use (writer only) */
	_Atomic int64_t rtt_ewma_us;   /* EWMA RTT in microseconds (lock-free) */
	_Atomic int64_t rtt_last_us;   /* Last RTT sample in microseconds (lock-free) */
	_Atomic uint32_t sample_count; /* Number of RTT samples collected (lock-free) */
	/* RTT histogram for percentile calculation (atomic for lock-free access) */
	_Atomic uint32_t rtt_hist[RTT_HIST_BUCKETS];
};

/* Bidirectional flow key for RTT lookup (canonical ordering) */
struct tcp_flow_key {
	uint16_t ethertype;
	uint16_t _pad;              /* Alignment padding */
	union {
		struct {
			struct in_addr ip_lo;
			struct in_addr ip_hi;
		};
		struct {
			struct in6_addr ip6_lo;
			struct in6_addr ip6_hi;
		};
	};
	uint16_t port_lo;
	uint16_t port_hi;
};

/* RTT tracking entry - one per TCP connection (bidirectional) */
struct tcp_rtt_entry {
	struct tcp_flow_key key;
	struct tcp_rtt_direction fwd;  /* lo->hi direction */
	struct tcp_rtt_direction rev;  /* hi->lo direction */
	struct timeval last_activity;
	_Atomic int state;             /* Connection state (lock-free) */
	uint8_t flags_seen_fwd;        /* Cumulative flags seen lo->hi */
	uint8_t flags_seen_rev;        /* Cumulative flags seen hi->lo */
	uint8_t _pad[2];               /* Alignment */
	UT_hash_handle hh;
};

/* Initialize RTT tracking subsystem */
void tcp_rtt_init(void);

/* Cleanup RTT tracking subsystem */
void tcp_rtt_cleanup(void);

/* Process a TCP packet for RTT tracking
 * flow: the flow tuple (from normal flow tracking)
 * seq: TCP sequence number
 * ack: TCP acknowledgement number
 * flags: TCP flags
 * payload_len: length of TCP payload (excluding headers)
 * timestamp: packet capture timestamp
 */
void tcp_rtt_process_packet(const struct flow *flow,
                            uint32_t seq,
                            uint32_t ack,
                            uint8_t flags,
                            uint16_t payload_len,
                            struct timeval timestamp);

/* Get EWMA RTT for a flow in microseconds
 * Returns -1 if no RTT data available (non-TCP or no samples yet)
 */
int64_t tcp_rtt_get_ewma(const struct flow *flow);

/* Get last RTT sample for a flow in microseconds
 * Returns -1 if no RTT data available
 */
int64_t tcp_rtt_get_last(const struct flow *flow);

/* Get connection state for a flow
 * Returns TCP_STATE_UNKNOWN if flow not found
 */
enum tcp_conn_state tcp_rtt_get_state(const struct flow *flow);

/* Get RTT and state in a single lookup (more efficient than separate calls)
 * rtt_us: output, set to EWMA RTT in microseconds or -1 if unavailable
 * state: output, set to connection state
 * saw_syn: output, set to 1 if SYN was observed for this connection, 0 otherwise
 * Returns 0 on success (entry found), -1 if flow not found or not TCP
 */
int tcp_rtt_get_info(const struct flow *flow, int64_t *rtt_us,
                     enum tcp_conn_state *state, int *saw_syn);

/* Expire old RTT entries that haven't been active within window */
void tcp_rtt_expire_old(struct timeval deadline, struct timeval window);

/* Health info structure for returning histogram and calculated metrics */
struct tcp_health_result {
	uint32_t rtt_hist[RTT_HIST_BUCKETS]; /* RTT histogram buckets */
	uint32_t rtt_samples;
	uint8_t health_status;    /* TCP_HEALTH_* from flow.h */
	uint8_t health_flags;     /* TCP_HEALTH_FLAG_* from flow.h */
};

/* Get RTT histogram and health status for a flow
 * Copies the histogram and determines health status based on
 * tail latency ratio and other metrics from window tracking.
 * Returns 0 on success, -1 if flow not found or not TCP
 */
int tcp_rtt_get_health(const struct flow *flow,
                       uint32_t retransmit_cnt,
                       uint32_t total_packets,
                       uint32_t zero_window_cnt,
                       struct tcp_health_result *result);

#endif /* TCP_RTT_H */
