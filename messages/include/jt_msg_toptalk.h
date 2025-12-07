#ifndef JT_MSG_TOPTALK_H
#define JT_MSG_TOPTALK_H

int jt_toptalk_packer(void *data, char **out);
int jt_toptalk_unpacker(json_t *root, void **data);
int jt_toptalk_printer(void *data, char *out, int len);
int jt_toptalk_free(void *data);
const char *jt_toptalk_test_msg_get(void);

/* MAX_FLOWS should be 2x the number of displayed top N flows. */
#define MAX_FLOWS 40
#define ADDR_LEN 50
#define PROTO_LEN 6
#define TCLASS_LEN 5

/* TCP connection states (matches tcp_rtt.h enum tcp_conn_state) */
#define TCP_CONN_STATE_UNKNOWN   0
#define TCP_CONN_STATE_SYN_SEEN  1
#define TCP_CONN_STATE_ACTIVE    2
#define TCP_CONN_STATE_FIN_WAIT  3
#define TCP_CONN_STATE_CLOSED    4

struct jt_msg_toptalk
{
	struct timespec timestamp;
	uint64_t interval_ns;
	uint32_t tflows;
	int64_t tbytes;
	int64_t tpackets;
	struct {
		int64_t bytes;
		int64_t packets;
		int64_t rtt_us;          /* RTT in microseconds, -1 if not available */
		int32_t tcp_state;       /* TCP connection state, -1 if not TCP */
		int32_t saw_syn;         /* 1 if SYN was observed, 0 otherwise */
		/* Window/Congestion tracking fields */
		int64_t rwnd_bytes;      /* Advertised window in bytes, -1 if unknown */
		int32_t window_scale;    /* Window scale factor, -1 if unknown */
		uint32_t zero_window_cnt; /* Zero-window events */
		uint32_t dup_ack_cnt;    /* Duplicate ACK bursts (3+) */
		uint32_t retransmit_cnt; /* Detected retransmissions */
		uint32_t ece_cnt;        /* ECE flag count */
		uint8_t recent_events;   /* Bitmask of recent congestion events */
		uint16_t sport;
		uint16_t dport;
		char src[ADDR_LEN];
		char dst[ADDR_LEN];
		char proto[PROTO_LEN];
		char tclass[TCLASS_LEN];
	} flows[MAX_FLOWS];
};

#endif
