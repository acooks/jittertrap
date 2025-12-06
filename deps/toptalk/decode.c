#define _GNU_SOURCE
#include <pcap.h>
#include <time.h>
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <pcap/sll.h>

#include "flow.h"
#include "decode.h"

int decode_ethernet(const struct pcap_pkthdr *h, const uint8_t *wirebits,
                    struct flow_pkt *pkt, char *errstr)
{
	const struct hdr_ethernet *ethernet;
	const uint8_t *end_of_packet = wirebits + h->caplen;
	int ret;

	pkt->timestamp.tv_sec = h->ts.tv_sec;
	pkt->timestamp.tv_usec = h->ts.tv_usec;
	pkt->flow_rec.bytes = h->len;
	pkt->flow_rec.packets = 1;

	ethernet = (struct hdr_ethernet *)wirebits;

	switch (ntohs(ethernet->type)) {
	case ETHERTYPE_IP:
		ret = decode_ip4(wirebits + HDR_LEN_ETHER, end_of_packet, pkt,
		                 errstr);
		break;
	case ETHERTYPE_VLAN:
		ret = decode_ethernet(h, wirebits + HDR_LEN_ETHER_VLAN, pkt,
		                      errstr);
		break;
	case ETHERTYPE_IPV6:
		ret = decode_ip6(wirebits + HDR_LEN_ETHER, end_of_packet, pkt,
		                 errstr);
		break;
	case ETHERTYPE_ARP:
		snprintf(errstr, DECODE_ERRBUF_SIZE, "%s", "ARP ignored");
		ret = -1;
		break;
	case ETHERTYPE_LLDP:
		snprintf(errstr, DECODE_ERRBUF_SIZE, "%s", "LLDP ignored");
		ret = -1;
		break;
	default:
		/* we don't know how to decode other types right now. */
		snprintf(errstr, DECODE_ERRBUF_SIZE,
		         "EtherType [0x%04x] ignored", ntohs(ethernet->type));
		ret = -1;
		break;
	}
	return ret;
}

int decode_linux_sll(const struct pcap_pkthdr *h, const uint8_t *wirebits,
                     struct flow_pkt *pkt, char *errstr)
{
	const struct sll_header *sll;
	const uint8_t *end_of_packet = wirebits + h->caplen;
	int ret;

	pkt->timestamp.tv_sec = h->ts.tv_sec;
	pkt->timestamp.tv_usec = h->ts.tv_usec;
	pkt->flow_rec.bytes = h->len;
	pkt->flow_rec.packets = 1;

	sll = (struct sll_header *)wirebits;
	switch (ntohs(sll->sll_protocol)) {
	case ETHERTYPE_IP:
		ret = decode_ip4(wirebits + SLL_HDR_LEN, end_of_packet, pkt,
		                 errstr);
		break;
	case ETHERTYPE_IPV6:
		ret = decode_ip6(wirebits + SLL_HDR_LEN, end_of_packet, pkt,
		                 errstr);
		break;
	default:
		snprintf(errstr, DECODE_ERRBUF_SIZE,
		         "sll proto: %x. Linux 'cooked' decoding is TODO",
		         ntohs(sll->sll_protocol));
		ret = -1;
	}
	return ret;
}

int decode_ip6(const uint8_t *packet, const uint8_t *end_of_packet,
               struct flow_pkt *pkt, char *errstr)
{
	int ret;
	const void *next = (uint8_t *)packet + sizeof(struct hdr_ipv6);
	const struct hdr_ipv6 *ip6_packet = (const struct hdr_ipv6 *)packet;
	uint8_t next_hdr, hdr_len;
	size_t total_ext_len;
	int is_ext_header = 1;

	pkt->flow_rec.flow.ethertype = ETHERTYPE_IPV6;
	pkt->flow_rec.flow.src_ip6 = (ip6_packet->ip6_src);
	pkt->flow_rec.flow.dst_ip6 = (ip6_packet->ip6_dst);
	pkt->flow_rec.flow.tclass = (htonl(ip6_packet->vcf) & 0x0fc00000) >> 20;

	next_hdr = ip6_packet->next_hdr;
	while (is_ext_header) {

		/* Optional headers */
		switch (next_hdr) {
		case IPPROTO_HOPOPTS:
		case IPPROTO_ROUTING:
		case IPPROTO_FRAGMENT:
		case IPPROTO_DSTOPTS: /* IPv6 Destination Options */
			hdr_len = *((uint8_t *)next + 1);
			total_ext_len = (hdr_len + 1) * 8;

			if ((const uint8_t *)next + total_ext_len >
			    end_of_packet) {
				snprintf(
				    errstr, DECODE_ERRBUF_SIZE,
				    "*** Invalid IPv6 extension header length");
				return -1; // Packet is malformed
			}
			next = (uint8_t *)next + total_ext_len;
			next_hdr = *((uint8_t *)next);
			break;
		default:
			is_ext_header = 0;
			break;
		}
	}

	if ((const uint8_t *)next + 8 >
	    end_of_packet) { // 8 is the size of the smallest L4 header (UDP)
		snprintf(errstr, DECODE_ERRBUF_SIZE,
		         "*** Truncated IPv6 packet");
		return -1;
	}

	/* Transport proto TCP/UDP/ICMP */
	switch (next_hdr) {
	case IPPROTO_TCP:
		ret = decode_tcp(next, pkt, errstr);
		break;
	case IPPROTO_UDP:
		ret = decode_udp(next, pkt, errstr);
		break;
	case IPPROTO_ICMP:
		ret = decode_icmp(next, pkt, errstr);
		break;
	case IPPROTO_IGMP:
		ret = decode_igmp(next, pkt, errstr);
		break;
	case IPPROTO_ICMPV6:
		ret = decode_icmp6(next, end_of_packet, pkt, errstr);
		break;
	case IPPROTO_ESP:
		ret = decode_esp(next, pkt, errstr);
		break;
	default:
		snprintf(errstr, DECODE_ERRBUF_SIZE,
		         "*** Protocol [0x%02x] unknown", ip6_packet->next_hdr);
		ret = -1;
		break;
	}
	return ret;
}

int decode_ip4(const uint8_t *packet, const uint8_t *end_of_packet,
               struct flow_pkt *pkt, char *errstr)
{
	int ret;
	const void *next;
	const struct hdr_ipv4 *ip4_packet = (const struct hdr_ipv4 *)packet;
	unsigned int size_ip = IP_HL(ip4_packet) * 4;
	if (size_ip < 20) {
		snprintf(errstr, DECODE_ERRBUF_SIZE,
		         "*** Invalid IP header length: %u bytes", size_ip);
		return -1;
	}
	next = ((uint8_t *)ip4_packet + size_ip);

	pkt->flow_rec.flow.ethertype = ETHERTYPE_IP;
	pkt->flow_rec.flow.src_ip = (ip4_packet->ip_src);
	pkt->flow_rec.flow.dst_ip = (ip4_packet->ip_dst);
	pkt->flow_rec.flow.tclass = IPTOS_DSCP(ip4_packet->ip_tos);

	/* IP proto TCP/UDP/ICMP */
	switch (ip4_packet->ip_p) {
	case IPPROTO_TCP:
		ret = decode_tcp(next, pkt, errstr);
		break;
	case IPPROTO_UDP:
		ret = decode_udp(next, pkt, errstr);
		break;
	case IPPROTO_ICMP:
		ret = decode_icmp(next, pkt, errstr);
		break;
	case IPPROTO_IGMP:
		ret = decode_igmp(next, pkt, errstr);
		break;
	case IPPROTO_ESP:
		ret = decode_esp(next, pkt, errstr);
		break;
	default:
		snprintf(errstr, DECODE_ERRBUF_SIZE,
		         "*** Protocol [0x%02x] unknown", ip4_packet->ip_p);
		ret = -1;
		break;
	}
	return ret;
}

int decode_tcp(const struct hdr_tcp *packet, struct flow_pkt *pkt, char *errstr)
{
	unsigned int size_tcp = (TH_OFF(packet) * 4);

	if (size_tcp < 20) {
		snprintf(errstr, DECODE_ERRBUF_SIZE,
		         "*** Invalid TCP header length: %u bytes", size_tcp);
		return -1;
	}

	pkt->flow_rec.flow.proto = IPPROTO_TCP;
	pkt->flow_rec.flow.sport = ntohs(packet->sport);
	pkt->flow_rec.flow.dport = ntohs(packet->dport);
	return 0;
}

int decode_tcp_extended(const struct hdr_tcp *packet,
                        const uint8_t *end_of_packet,
                        struct flow_pkt_tcp *pkt,
                        char *errstr)
{
	unsigned int size_tcp = (TH_OFF(packet) * 4);

	if (size_tcp < 20) {
		snprintf(errstr, DECODE_ERRBUF_SIZE,
		         "*** Invalid TCP header length: %u bytes", size_tcp);
		return -1;
	}

	pkt->base.flow_rec.flow.proto = IPPROTO_TCP;
	pkt->base.flow_rec.flow.sport = ntohs(packet->sport);
	pkt->base.flow_rec.flow.dport = ntohs(packet->dport);

	/* Extract TCP-specific fields for RTT/window tracking */
	pkt->seq = ntohl(packet->seq);
	pkt->ack = ntohl(packet->ack);
	pkt->flags = packet->flags;
	pkt->window = ntohs(packet->win);

	/* Calculate payload length: end of packet - start of payload */
	const uint8_t *tcp_start = (const uint8_t *)packet;
	const uint8_t *payload_start = tcp_start + size_tcp;
	if (payload_start <= end_of_packet) {
		pkt->payload_len = end_of_packet - payload_start;
	} else {
		pkt->payload_len = 0;
	}

	return 0;
}

int decode_udp(const struct hdr_udp *packet, struct flow_pkt *pkt, char *errstr)
{
	(void)errstr;
	pkt->flow_rec.flow.proto = IPPROTO_UDP;
	pkt->flow_rec.flow.sport = ntohs(packet->sport);
	pkt->flow_rec.flow.dport = ntohs(packet->dport);
	return 0;
}

int decode_icmp(const struct hdr_icmp *packet, struct flow_pkt *pkt,
                char *errstr)
{
	(void)errstr;
	(void)packet;
	pkt->flow_rec.flow.proto = IPPROTO_ICMP;

	/* ICMP doesn't have ports, but we depend on that for the flow  */
	if (packet->type == ICMP_ECHO) {
		pkt->flow_rec.flow.dport = (ICMP_ECHO << 8) | packet->code;
		pkt->flow_rec.flow.sport = ntohl(packet->hdr_data) >> 16;
	} else if (packet->type == ICMP_ECHOREPLY) {
		pkt->flow_rec.flow.sport = (ICMP_ECHO << 8) | packet->code;
		pkt->flow_rec.flow.dport = ntohl(packet->hdr_data) >> 16;
	} else {
		pkt->flow_rec.flow.sport = packet->type;
		pkt->flow_rec.flow.dport = packet->hdr_data;
	}
	return 0;
}

int decode_igmp(const struct hdr_icmp *packet, struct flow_pkt *pkt,
                char *errstr)
{
	(void)errstr;
	(void)packet;
	pkt->flow_rec.flow.proto = IPPROTO_IGMP;
	/* IGMP doesn't have ports, but we depend on that for the flow */
	pkt->flow_rec.flow.sport = 0;
	pkt->flow_rec.flow.dport = 0;
	return 0;
}

int decode_icmp6(const struct hdr_icmp *packet, const uint8_t *end_of_packet,
                 struct flow_pkt *pkt, char *errstr)
{
	(void)end_of_packet;
	(void)errstr;

	pkt->flow_rec.flow.proto = IPPROTO_ICMPV6;

	/* Create unique flows for Echo Request (128) and Echo Reply (129) */
	if (packet->type == ICMP6_ECHO_REQUEST) {
		/* Use the ICMP identifier to create a unique flow "port" */
		pkt->flow_rec.flow.dport =
		    (ICMP6_ECHO_REQUEST << 8) | packet->code;
		pkt->flow_rec.flow.sport =
		    ntohs(*(const uint16_t *)&(packet->hdr_data));
	} else if (packet->type == ICMP6_ECHO_REPLY) {
		pkt->flow_rec.flow.sport =
		    (ICMP6_ECHO_REPLY << 8) | packet->code;
		pkt->flow_rec.flow.dport =
		    ntohs(*(const uint16_t *)&(packet->hdr_data));
	} else {
		/* Aggregate all other ICMPv6 types */
		pkt->flow_rec.flow.sport = packet->type;
		pkt->flow_rec.flow.dport = 0;
	}
	return 0;
}

int decode_esp(const struct hdr_esp *packet, struct flow_pkt *pkt, char *errstr)
{
	(void)errstr;
	(void)packet;
	pkt->flow_rec.flow.proto = IPPROTO_ESP;
	pkt->flow_rec.flow.sport = 0;
	pkt->flow_rec.flow.dport = 0;
	return 0;
}
