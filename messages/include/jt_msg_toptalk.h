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
#define TCP_CONN_STATE_ACTIVE    1
#define TCP_CONN_STATE_FIN_WAIT  2
#define TCP_CONN_STATE_CLOSED    3

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
		uint16_t sport;
		uint16_t dport;
		char src[ADDR_LEN];
		char dst[ADDR_LEN];
		char proto[PROTO_LEN];
		char tclass[TCLASS_LEN];
	} flows[MAX_FLOWS];
};

#endif
