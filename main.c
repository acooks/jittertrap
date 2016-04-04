#define _GNU_SOURCE
#include <signal.h>
#include <poll.h>
#include <stdio.h>
#include <pcap.h>
#include <time.h>

#include <net/ethernet.h>
#include <arpa/inet.h>

#include "decode.h"

const char *protos[IPPROTO_MAX] = {
	[IPPROTO_TCP]  = "TCP",
	[IPPROTO_UDP]  = "UDP",
	[IPPROTO_ICMP] = "ICMP",
	[IPPROTO_IP]   = "IP"
};

struct pkt_record {
	uint32_t ts_sec;
	uint32_t ts_usec;
	uint16_t len;
	struct in_addr src_ip;
	struct in_addr dst_ip;
	uint16_t proto;
	uint16_t sport;
	uint16_t dport;
};

struct pkt_record pkt;

#define zero_pkt(p)                                                            \
	do {                                                                   \
		p.ts_sec = 0;                                                  \
		p.ts_usec = 0;                                                 \
		p.len = 0;                                                     \
		p.proto = 0;                                                   \
		p.sport = 0;                                                   \
		p.dport = 0;                                                   \
	} while (0);

void print_pkt()
{
	char ip_src[16];
	char ip_dst[16];

	sprintf(ip_src, "%s", inet_ntoa(pkt.src_ip));
	sprintf(ip_dst, "%s", inet_ntoa(pkt.dst_ip));

	printf("%d.%06d,  %4d, %15s, %15s, %4s %6d, %6d\n",
	       pkt.ts_sec, pkt.ts_usec, pkt.len, ip_src, ip_dst,
	       protos[pkt.proto], pkt.sport, pkt.dport);
}

void decode_tcp(const struct hdr_tcp *packet)
{
	unsigned int size_tcp = (TH_OFF(packet) * 4);

	if (size_tcp < 20) {
		printf(" *** Invalid TCP header length: %u bytes\n", size_tcp);
		return;
	}

	pkt.proto = IPPROTO_TCP;
	pkt.sport = ntohs(packet->th_sport);
	pkt.dport = ntohs(packet->th_dport);
}

void decode_udp(const struct hdr_udp *packet)
{
	pkt.proto = IPPROTO_UDP;
}

void decode_icmp(const struct hdr_icmp *packet)
{
	pkt.proto = IPPROTO_ICMP;
}

void decode_ip(const struct hdr_ip *packet)
{
	const void *next; /* IP Payload */
	unsigned int size_ip;

	size_ip = IP_HL(packet) * 4;
	if (size_ip < 20) {
		fprintf(stderr, " *** Invalid IP header length: %u bytes\n",
		        size_ip);
		return;
	}
	next = ((uint8_t *)packet + size_ip);

	pkt.src_ip = (packet->ip_src);
	pkt.dst_ip = (packet->ip_dst);

	/* IP proto TCP/UDP/ICMP */
	switch (packet->ip_p) {
	case IPPROTO_TCP:
		decode_tcp(next);
		break;
	case IPPROTO_UDP:
		decode_udp(next);
		break;
	case IPPROTO_ICMP:
		decode_icmp(next);
		break;
	default:
		fprintf(stderr, " *** Protocol [0x%x] unknown\n", packet->ip_p);
		break;
	}
}

void decode_packet(uint8_t *user, const struct pcap_pkthdr *h,
                   const uint8_t *packet)
{
	const struct hdr_ethernet *ethernet;
	const struct hdr_ip *ip; /* The IP header */

	u_int size_ether;

	zero_pkt(pkt);
	pkt.ts_sec = h->ts.tv_sec;
	pkt.ts_usec = h->ts.tv_usec;
	pkt.len = h->len;

	/* Ethernet header */
	ethernet = (struct hdr_ethernet *)packet;

	switch (ntohs(ethernet->type)) {
	case ETHERTYPE_IP:
		size_ether = HDR_LEN_ETHER;
		break;
	case ETHERTYPE_VLAN:
		size_ether = HDR_LEN_ETHER_VLAN;
		break;
	case ETHERTYPE_IPV6:
		printf("IPv6 ignored\n");
		return;
	case ETHERTYPE_ARP:
		printf("ARP ignored\n");
		return;
	default:
		/* we don't know how to decode other types right now. */
		fprintf(stderr, "EtherType [0x%04x] ignored\n",
		        ntohs(ethernet->type));
		return;
	}

	/* IP header */
	ip = (struct hdr_ip *)(packet + size_ether);
	decode_ip(ip);
	print_pkt();
}

int grab_packets(int fd, pcap_t *handle)
{
	struct timespec timeout_ts = {.tv_sec = 0, .tv_nsec = 1E8 };
	struct pollfd fds[] = {
		{.fd = fd, .events = POLLIN, .revents = POLLHUP }
	};

	while (1) {
		if (ppoll(fds, 1, &timeout_ts, NULL)) {
			pcap_dispatch(handle, 0, decode_packet, NULL);
		}
	}
	return 0;
}

int main(int argc, char *argv[])
{
	char *dev, errbuf[PCAP_ERRBUF_SIZE];
	pcap_t *handle;
	int selectable_fd;

	if (argc == 2) {
		dev = argv[1];
	} else {
		dev = pcap_lookupdev(errbuf);
	}

	if (dev == NULL) {
		fprintf(stderr, "Couldn't find default device: %s\n", errbuf);
		return (2);
	}
	printf("Device: %s\n", dev);

	handle = pcap_open_live(dev, BUFSIZ, 1, 0, errbuf);
	if (handle == NULL) {
		fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
		return (2);
	}

	if (pcap_datalink(handle) != DLT_EN10MB) {
		fprintf(stderr, "Device %s doesn't provide Ethernet headers - "
		                "not supported\n",
		        dev);
		return (2);
	}

	if (pcap_setnonblock(handle, 1, errbuf) != 0) {
		fprintf(stderr, "Non-blocking mode failed: %s\n", errbuf);
		return (2);
	}

	selectable_fd = pcap_get_selectable_fd(handle);
	if (-1 == selectable_fd) {
		fprintf(stderr, "pcap handle not selectable.\n");
		return (2);
	}

	grab_packets(selectable_fd, handle);
	/* And close the session */
	pcap_close(handle);
	return 0;
}
