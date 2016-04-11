#include "uthash.h"

struct flow {
	int ethertype;
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
};

struct pkt_record {
	struct timeval timestamp;
	uint32_t len; /* this is cumulative in tables */
	struct flow flow;
	UT_hash_handle hh; /* makes this structure hashable */
};
