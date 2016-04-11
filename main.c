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
#include "flow.h"
#include "decode.h"

static const char const *protos[IPPROTO_MAX] = {[IPPROTO_TCP] = "TCP",
	                                        [IPPROTO_UDP] = "UDP",
	                                        [IPPROTO_ICMP] = "ICMP",
	                                        [IPPROTO_ICMPV6] = "ICMP6",
	                                        [IPPROTO_IP] = "IP",
	                                        [IPPROTO_IGMP] = "IGMP" };

/* hash table for stats */
struct pkt_record *flow_table = NULL;

void print_pkt(struct pkt_record *pkt)
{
	mvprintw(0, 20, "%d.%06d,  %4d, %5s", pkt->ts_sec, pkt->ts_usec,
	         pkt->len, protos[pkt->flow.proto]);
}

#define ERR_LINE_OFFSET 2

int bytes_cmp(struct pkt_record *p1, struct pkt_record *p2)
{
	return (p2->len - p1->len);
}

#define TOP_N_LINE_OFFSET 5
void print_top_n(int stop)
{
	struct pkt_record *r;
	int row = 0, rowcnt = stop;
	char ip_src[16];
	char ip_dst[16];
	char ip6_src[40];
	char ip6_dst[40];

#if 0
	mvprintw(TOP_N_LINE_OFFSET, 0,
	         "%15s:%-6s %15s    %15s:%-6s  %10s",
	         "Source", "port", "bytes", "Destination", "port", "proto");
#endif
	mvprintw(TOP_N_LINE_OFFSET + row++, 0, "Top Flows:");

	for (row = 1, r = flow_table; r != NULL && rowcnt--; r = r->hh.next) {
		sprintf(ip_src, "%s", inet_ntoa(r->flow.src_ip));
		sprintf(ip_dst, "%s", inet_ntoa(r->flow.dst_ip));
		inet_ntop(AF_INET6, &(r->flow.src_ip6), ip6_src,
		          sizeof(ip6_src));
		inet_ntop(AF_INET6, &(r->flow.dst_ip6), ip6_dst,
		          sizeof(ip6_dst));

		switch (r->flow.ethertype) {
		case ETHERTYPE_IP:
			mvprintw(TOP_N_LINE_OFFSET + row++, 0, "%39s->%-39s",
			         ip_src, ip_dst);
			mvprintw(TOP_N_LINE_OFFSET + row++, 0,
			         "%-5s %15d %10s %6d->%-6d",
			         protos[r->flow.proto], r->len, " ",
			         r->flow.sport, r->flow.dport);
			mvprintw(TOP_N_LINE_OFFSET + row++, 0, "%80s", " ");
			break;

		case ETHERTYPE_IPV6:
			mvprintw(TOP_N_LINE_OFFSET + row++, 0, "%39s->%-39s",
			         ip6_src, ip6_dst);
			mvprintw(TOP_N_LINE_OFFSET + row++, 0,
			         "%-5s %15d %10s %6d->%-6d",
			         protos[r->flow.proto], r->len, " ",
			         r->flow.sport, r->flow.dport);
			mvprintw(TOP_N_LINE_OFFSET + row++, 0, "%80s", " ");
			break;
		default:
			mvprintw(ERR_LINE_OFFSET, 0, "%80s", " ");
			mvprintw(ERR_LINE_OFFSET + row++, 0,
			         "%15d Unknown ethertype: %d",
			         r->flow.ethertype);
		}
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
		table_entry =
		    (struct pkt_record *)malloc(sizeof(struct pkt_record));
		memset(table_entry, 0, sizeof(struct pkt_record));
		memcpy(table_entry, pkt, sizeof(struct pkt_record));
		HASH_ADD(hh, flow_table, flow, sizeof(struct flow),
		         table_entry);
	} else {
		table_entry->len += pkt->len;
	}

	HASH_SORT(flow_table, bytes_cmp);
}

void handle_packet(uint8_t *user, const struct pcap_pkthdr *pcap_hdr,
                   const uint8_t *wirebits)
{
	static const struct pkt_record ZeroPkt = { 0 };
	struct pkt_record *pkt;
	char errstr[DECODE_ERRBUF_SIZE];

	pkt = malloc(sizeof(struct pkt_record));
	*pkt = ZeroPkt;

	if (0 == decode_ethernet(pcap_hdr, wirebits, pkt, errstr)) {
		update_stats_tables(pkt);
	} else {
		mvprintw(ERR_LINE_OFFSET, 0, "%-80s", errstr);
	}

	free(pkt);
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
			pcap_dispatch(handle, 100, handle_packet, NULL);
		}

		if ((ch = getch()) == ERR) {
			/* normal case - no input */
			;
		} else {
			switch (ch) {
			case 'q':
				endwin(); /* End curses mode */
				return;
			}
		}
		print_top_n(5);
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
