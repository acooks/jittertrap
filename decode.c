#define _GNU_SOURCE
#include <pcap.h>
#include <time.h>
#include <net/ethernet.h>
#include <arpa/inet.h>

#include "flow.h"
#include "decode.h"

#define ERR_LINE_OFFSET 2
int decode_ethernet(const struct pcap_pkthdr *h, const uint8_t *wirebits,
                    struct pkt_record *pkt)
{
	const struct hdr_ethernet *ethernet;
	int ret;

	pkt->ts_sec = h->ts.tv_sec;
	pkt->ts_usec = h->ts.tv_usec;
	pkt->len = h->len;

	ethernet = (struct hdr_ethernet *)wirebits;

	switch (ntohs(ethernet->type)) {
	case ETHERTYPE_IP:
		decode_ip4(wirebits + HDR_LEN_ETHER, pkt);
		ret = 0;
		break;
	case ETHERTYPE_VLAN:
		decode_ethernet(h, wirebits + HDR_LEN_ETHER_VLAN, pkt);
		ret = 0; /* TODO: check recursion! */
		break;
	case ETHERTYPE_IPV6:
		decode_ip6(wirebits + HDR_LEN_ETHER, pkt);
		ret = 0;
		break;
	case ETHERTYPE_ARP:
		mvprintw(ERR_LINE_OFFSET, 0, "%80s", " ");
		mvprintw(ERR_LINE_OFFSET, 0, "ARP ignored");
		ret = 1;
		break;
	case ETHERTYPE_LLDP:
		mvprintw(ERR_LINE_OFFSET, 0, "%80s", " ");
		mvprintw(ERR_LINE_OFFSET, 0, "LLDP ignored");
		ret = 1;
		break;
	default:
		/* we don't know how to decode other types right now. */
		mvprintw(ERR_LINE_OFFSET, 0, "%80s", " ");
		mvprintw(ERR_LINE_OFFSET, 0, "EtherType [0x%04x] ignored",
		         ntohs(ethernet->type));
		ret = 1;
		break;
	}
	return ret;
}

void decode_ip6(const uint8_t *packet, struct pkt_record *pkt)
{
	const void *next = (uint8_t *)packet + sizeof(struct hdr_ipv6);
	const struct hdr_ipv6 *ip6_packet = (const struct hdr_ipv6 *)packet;

	pkt->flow.ethertype = ETHERTYPE_IPV6;
	pkt->flow.src_ip6 = (ip6_packet->ip6_src);
	pkt->flow.dst_ip6 = (ip6_packet->ip6_dst);

	/* Transport proto TCP/UDP/ICMP */
	switch (ip6_packet->next_hdr) {
	case IPPROTO_TCP:
		decode_tcp(next, pkt);
		break;
	case IPPROTO_UDP:
		decode_udp(next, pkt);
		break;
	case IPPROTO_ICMP:
		decode_icmp(next, pkt);
		break;
	case IPPROTO_IGMP:
		decode_igmp(next, pkt);
		break;
	case IPPROTO_ICMPV6:
		decode_icmp6(next, pkt);
		break;
	default:
		mvprintw(ERR_LINE_OFFSET, 0, "%80s", " ");
		mvprintw(ERR_LINE_OFFSET, 0, "*** Protocol [0x%02x] unknown",
		         ip6_packet->next_hdr);
		break;
	}
}

void decode_ip4(const uint8_t *packet, struct pkt_record *pkt)
{
	const void *next;
	const struct hdr_ipv4 *ip4_packet = (const struct hdr_ipv4 *)packet;
	unsigned int size_ip = IP_HL(ip4_packet) * 4;
	if (size_ip < 20) {
		mvprintw(ERR_LINE_OFFSET, 0, "%80s", " ");
		mvprintw(ERR_LINE_OFFSET, 0,
		         "*** Invalid IP header length: %u bytes", size_ip);
		return;
	}
	next = ((uint8_t *)ip4_packet + size_ip);

	pkt->flow.ethertype = ETHERTYPE_IP;
	pkt->flow.src_ip = (ip4_packet->ip_src);
	pkt->flow.dst_ip = (ip4_packet->ip_dst);

	/* IP proto TCP/UDP/ICMP */
	switch (ip4_packet->ip_p) {
	case IPPROTO_TCP:
		decode_tcp(next, pkt);
		break;
	case IPPROTO_UDP:
		decode_udp(next, pkt);
		break;
	case IPPROTO_ICMP:
		decode_icmp(next, pkt);
		break;
	case IPPROTO_IGMP:
		decode_igmp(next, pkt);
		break;
	default:
		mvprintw(ERR_LINE_OFFSET, 0, "%80s", " ");
		mvprintw(ERR_LINE_OFFSET, 0, "*** Protocol [0x%02x] unknown",
		         ip4_packet->ip_p);
		break;
	}
}

void decode_tcp(const struct hdr_tcp *packet, struct pkt_record *pkt)
{
	unsigned int size_tcp = (TH_OFF(packet) * 4);

	if (size_tcp < 20) {
		mvprintw(ERR_LINE_OFFSET, 0, "%80s", " ");
		mvprintw(ERR_LINE_OFFSET, 0,
		         "*** Invalid TCP header length: %u bytes", size_tcp);
		return;
	}

	pkt->flow.proto = IPPROTO_TCP;
	pkt->flow.sport = ntohs(packet->sport);
	pkt->flow.dport = ntohs(packet->dport);
}

void decode_udp(const struct hdr_udp *packet, struct pkt_record *pkt)
{
	pkt->flow.proto = IPPROTO_UDP;
	pkt->flow.sport = ntohs(packet->sport);
	pkt->flow.dport = ntohs(packet->dport);
}

void decode_icmp(const struct hdr_icmp *packet, struct pkt_record *pkt)
{
	pkt->flow.proto = IPPROTO_ICMP;
	/* ICMP doesn't have ports, but we depend on that for the flow */
	pkt->flow.sport = 0;
	pkt->flow.dport = 0;
}

void decode_igmp(const struct hdr_icmp *packet, struct pkt_record *pkt)
{
	pkt->flow.proto = IPPROTO_IGMP;
	/* IGMP doesn't have ports, but we depend on that for the flow */
	pkt->flow.sport = 0;
	pkt->flow.dport = 0;
}

void decode_icmp6(const struct hdr_icmp *packet, struct pkt_record *pkt)
{
	pkt->flow.proto = IPPROTO_ICMPV6;
	pkt->flow.sport = 0;
	pkt->flow.dport = 0;
}
