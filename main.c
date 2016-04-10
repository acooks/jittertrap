#define _GNU_SOURCE
#include <signal.h>
#include <poll.h>
#include <stdio.h>
#include <pcap.h>
#include <time.h>
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <ncurses.h>

#include "uthash.h"
#include "decode.h"

const char *protos[IPPROTO_MAX] = {
	[IPPROTO_TCP]  = "TCP",
	[IPPROTO_UDP]  = "UDP",
	[IPPROTO_ICMP] = "ICMP",
	[IPPROTO_IP]   = "IP",
	[IPPROTO_IGMP] = "IGMP"
};

struct flow {
	struct in_addr src_ip;
	uint16_t sport;
	struct in_addr dst_ip;
	uint16_t dport;
	uint16_t proto;
};

struct pkt_record {
	uint32_t ts_sec;
	uint32_t ts_usec;
	uint32_t len;              /* this is cumulative in tables */
	struct flow flow;
	UT_hash_handle hh;         /* makes this structure hashable */
};


/* hash tables for stats */
struct pkt_record *flow_table = NULL;

void print_pkt(struct pkt_record *pkt)
{
	char ip_src[16];
	char ip_dst[16];

	sprintf(ip_src, "%s", inet_ntoa(pkt->flow.src_ip));
	sprintf(ip_dst, "%s", inet_ntoa(pkt->flow.dst_ip));

	mvprintw(1, 0, "%d.%06d,  %4d, %15s, %15s, %4s %6d, %6d\n",
	       pkt->ts_sec, pkt->ts_usec, pkt->len, ip_src, ip_dst,
	       protos[pkt->flow.proto], pkt->flow.sport, pkt->flow.dport);
}

void decode_tcp(const struct hdr_tcp *packet, struct pkt_record *pkt)
{
	unsigned int size_tcp = (TH_OFF(packet) * 4);

	if (size_tcp < 20) {
		printf(" *** Invalid TCP header length: %u bytes\n", size_tcp);
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

void decode_ip4(const struct hdr_ipv4 *packet, struct pkt_record *pkt)
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

	pkt->flow.src_ip = (packet->ip_src);
	pkt->flow.dst_ip = (packet->ip_dst);

	/* IP proto TCP/UDP/ICMP */
	switch (packet->ip_p) {
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
		fprintf(stderr, " *** Protocol [0x%x] unknown\n", packet->ip_p);
		break;
	}
}

int bytes_cmp(struct pkt_record *p1, struct pkt_record *p2)
{
	return (p2->len - p1->len);
}

#define TOP_N_LINE_OFFSET 5
void print_top_n(int stop)
{
	struct pkt_record *r;
	int row, rowcnt = stop;
	char ip_src[16];
	char ip_dst[16];

	mvprintw(TOP_N_LINE_OFFSET, 0,
	         "%15s:%-6s %15s    %15s:%-6s",
	         "Source", "port", "bytes", "Destination", "port");

	for(row = 1, r = flow_table; r != NULL && rowcnt--; r = r->hh.next) {
		sprintf(ip_src, "%s", inet_ntoa(r->flow.src_ip));
		sprintf(ip_dst, "%s", inet_ntoa(r->flow.dst_ip));

		mvprintw(TOP_N_LINE_OFFSET + row++, 0,
		         "%15s:%-6d %15d    %15s:%-6d",
		         ip_src, r->flow.sport, r->len,
		         ip_dst, r->flow.dport);
	}
}

void update_stats_tables(struct pkt_record *pkt)
{
	struct pkt_record *table_entry;

	print_pkt(pkt);

	/* Update the flow accounting table */
	/* id already in the hash? */
	HASH_FIND(hh, flow_table, &(pkt->flow), sizeof(struct flow),
	          table_entry);
	if (!table_entry) {
		table_entry = (struct pkt_record*)malloc(sizeof(struct pkt_record));
		memset(table_entry, 0, sizeof(struct pkt_record));
		memcpy(table_entry, pkt, sizeof(struct pkt_record));
		HASH_ADD(hh, flow_table, flow, sizeof(struct flow), table_entry);
	} else {
		table_entry->len += pkt->len;
	}

	HASH_SORT(flow_table, bytes_cmp);
}

void decode_packet(uint8_t *user, const struct pcap_pkthdr *h,
                   const uint8_t *packet)
{
	const struct hdr_ethernet *ethernet;
	const struct hdr_ipv4 *ip4; /* The IP header */
	uint32_t size_ether;
	static const struct pkt_record ZeroPkt = { 0 };
	struct pkt_record *pkt;

	pkt = malloc(sizeof(struct pkt_record));
	*pkt = ZeroPkt;

	pkt->ts_sec = h->ts.tv_sec;
	pkt->ts_usec = h->ts.tv_usec;
	pkt->len = h->len;

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
	ip4 = (struct hdr_ipv4 *)(packet + size_ether);

	decode_ip4(ip4, pkt);

	update_stats_tables(pkt);

	free(pkt);

	print_top_n(5);
}

void grab_packets(int fd, pcap_t *handle)
{
	struct timespec timeout_ts = {.tv_sec = 0, .tv_nsec = 1E8 };
	struct pollfd fds[] = {
		{.fd = fd, .events = POLLIN, .revents = POLLHUP }
	};

	int ch;

	while (1) {
		if (ppoll(fds, 1, &timeout_ts, NULL)) {
			pcap_dispatch(handle, 100, decode_packet, NULL);
		}

		if ((ch = getch()) == ERR) {
			/* normal case - no input */
			;
		}
		else {
			switch (ch) {
			case 'q':
				endwin();  /* End curses mode */
				return;
			}
		}
		refresh(); /* ncurses screen update */
	}
}

void init_curses()
{
	initscr();            /* Start curses mode              */
	raw();                /* Line buffering disabled        */
	keypad(stdscr, TRUE); /* We get F1, F2 etc..            */
	noecho();             /* Don't echo() while we do getch */
	nodelay(stdscr, TRUE);
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

	init_curses();
	mvprintw(0, 0, "Device: %s\n", dev);

	grab_packets(selectable_fd, handle);

	/* And close the session */
	pcap_close(handle);
	return 0;
}
