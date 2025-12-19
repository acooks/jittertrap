/*
 * tcp_flow_key.h - Canonical flow key for bidirectional TCP tracking
 *
 * This header provides:
 * - struct tcp_flow_key: A canonical key for hash table lookups that produces
 *   the same key regardless of packet direction (A->B or B->A).
 * - make_canonical_key(): Creates a canonical key from a flow, normalizing
 *   so the lower IP/port comes first.
 *
 * Used by tcp_rtt.c and tcp_window.c for bidirectional connection tracking.
 */
#ifndef TCP_FLOW_KEY_H
#define TCP_FLOW_KEY_H

#include <stdint.h>
#include <string.h>
#include <netinet/in.h>
#include <net/ethernet.h>
#include "flow.h"

/* Bidirectional flow key for TCP lookup (canonical ordering).
 * The "lo" fields contain the numerically lower IP/port,
 * ensuring A->B and B->A flows map to the same key.
 */
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

/*
 * Create canonical key (lower IP/port first for consistent lookup).
 *
 * Given a flow (with src/dst addresses and ports), produces a canonical
 * key where the numerically lower IP address comes first. If IPs are equal,
 * the lower port decides. This ensures that packets in both directions of
 * a TCP connection map to the same hash table entry.
 *
 * Parameters:
 *   key        - Output: the canonical key to populate
 *   flow       - Input: the flow to canonicalize
 *   is_forward - Output: 1 if flow direction matches key ordering (lo->hi),
 *                        0 if reversed (hi->lo)
 *
 * The is_forward flag is used to select the correct direction-specific
 * state (e.g., fwd vs rev RTT tracking) within a bidirectional entry.
 */
static inline void make_canonical_key(struct tcp_flow_key *key,
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

#endif /* TCP_FLOW_KEY_H */
