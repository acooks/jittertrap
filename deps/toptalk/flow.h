#ifndef FLOW_H
#define FLOW_H

struct flow {
	uint16_t ethertype;
	union {
		struct {
			struct in_addr src_ip;
			struct in_addr dst_ip;
		};
		struct {
			struct in6_addr src_ip6;
			struct in6_addr dst_ip6;
		};
	};
	uint16_t sport;
	uint16_t dport;
	uint16_t proto;
	uint8_t tclass;
};

/* Cached TCP RTT info - populated by tt_get_top5() for thread-safe access */
struct flow_rtt_info {
	int64_t rtt_us;           /* RTT in microseconds, -1 if unknown */
	int32_t tcp_state;        /* TCP connection state, -1 if unknown */
};

struct flow_record {
	struct flow flow;
	int64_t bytes;
	int64_t packets;
	/* Cached TCP info - populated by writer thread for thread-safe reader access */
	struct flow_rtt_info rtt;
};

struct flow_pkt {
	struct flow_record flow_rec;
	struct timeval timestamp;
};

#endif
