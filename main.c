#define _GNU_SOURCE
#include <signal.h>
#include <poll.h>
#include <stdio.h>
#include <pcap.h>
#include <time.h>
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <ncurses.h>

#include "utlist.h"
#include "uthash.h"
#include "flow.h"
#include "decode.h"
#include "timeywimey.h"
#include "intervals.h"

static const char const *protos[IPPROTO_MAX] = {[IPPROTO_TCP] = "TCP",
	                                        [IPPROTO_UDP] = "UDP",
	                                        [IPPROTO_ICMP] = "ICMP",
	                                        [IPPROTO_ICMPV6] = "ICMP6",
	                                        [IPPROTO_IP] = "IP",
	                                        [IPPROTO_IGMP] = "IGMP" };

#define ERR_LINE_OFFSET 2
#define TOP_N_LINE_OFFSET 5

void print_top_n(int stop)
{
	int row = 0, flowcnt = stop;
	char ip_src[16];
	char ip_dst[16];
	char ip6_src[40];
	char ip6_dst[40];

	flowcnt = get_flow_count();
	mvprintw(0, 50, "%5d active flows", flowcnt);

	attron(A_BOLD);
	mvprintw(TOP_N_LINE_OFFSET + row++, 1, "%39s %5s %5s", "Source",
	         "SPort", "Proto");

	mvprintw(TOP_N_LINE_OFFSET + row++, 1, "%39s %5s %10s %10s",
	         "Destination", "DPort", "Bytes", "Bytes");
	attroff(A_BOLD);
	row++;

	/* Clear the table */
	for (int i = TOP_N_LINE_OFFSET + row;
	     i <= TOP_N_LINE_OFFSET + row + 3 * stop; i++) {
		mvprintw(i, 0, "%80s", " ");
	}

	struct top_flows *t5 = malloc(sizeof(struct top_flows));
	memset(t5, 0, sizeof(struct top_flows));
	get_top5(t5);

	for (int i = 0; i < flowcnt && i < 5; i++) {

		struct flow_record *fte1 = &(t5->flow[i][0]);
		struct flow_record *fte2 = &(t5->flow[i][3]);

		sprintf(ip_src, "%s", inet_ntoa(fte1->flow.src_ip));
		sprintf(ip_dst, "%s", inet_ntoa(fte1->flow.dst_ip));
		inet_ntop(AF_INET6, &(fte1->flow.src_ip6), ip6_src,
		          sizeof(ip6_src));
		inet_ntop(AF_INET6, &(fte1->flow.dst_ip6), ip6_dst,
		          sizeof(ip6_dst));

		switch (fte1->flow.ethertype) {
		case ETHERTYPE_IP:
			mvaddch(TOP_N_LINE_OFFSET + row + 0, 0, ACS_ULCORNER);
			mvaddch(TOP_N_LINE_OFFSET + row + 1, 0, ACS_LLCORNER);
			mvprintw(TOP_N_LINE_OFFSET + row + 0, 1, "%39s",
			         ip_src);
			mvprintw(TOP_N_LINE_OFFSET + row + 1, 1, "%39s",
			         ip_dst);
			mvprintw(TOP_N_LINE_OFFSET + row + 0, 40, "%6d",
			         fte1->flow.sport);
			mvprintw(TOP_N_LINE_OFFSET + row + 1, 40, "%6d",
			         fte1->flow.dport);
			mvprintw(TOP_N_LINE_OFFSET + row + 0, 47, "%s",
			         protos[fte1->flow.proto]);
			mvprintw(TOP_N_LINE_OFFSET + row + 1, 47, "%10d %10d",
			         fte2->size, fte1->size);
			mvprintw(TOP_N_LINE_OFFSET + row + 2, 0, "%80s", " ");
			row += 3;
			break;

		case ETHERTYPE_IPV6:
			mvaddch(TOP_N_LINE_OFFSET + row + 0, 0, ACS_ULCORNER);
			mvaddch(TOP_N_LINE_OFFSET + row + 1, 0, ACS_LLCORNER);
			mvprintw(TOP_N_LINE_OFFSET + row + 0, 1, "%39s",
			         ip6_src);
			mvprintw(TOP_N_LINE_OFFSET + row + 1, 1, "%39s",
			         ip6_dst);
			mvprintw(TOP_N_LINE_OFFSET + row + 0, 40, "%6d",
			         fte1->flow.sport);
			mvprintw(TOP_N_LINE_OFFSET + row + 1, 40, "%6d",
			         fte1->flow.dport);
			mvprintw(TOP_N_LINE_OFFSET + row + 0, 47, "%s",
			         protos[fte1->flow.proto]);
			mvprintw(TOP_N_LINE_OFFSET + row + 1, 47, "%10d %10d",
			         fte2->size, fte1->size);
			mvprintw(TOP_N_LINE_OFFSET + row + 2, 0, "%80s", " ");
			row += 3;
			break;
		default:
			mvprintw(ERR_LINE_OFFSET, 0, "%80s", " ");
			mvprintw(ERR_LINE_OFFSET, 0, "Unknown ethertype: %d",
			         fte1->flow.ethertype);
		}
	}
	free(t5);
}

void handle_packet(uint8_t *user, const struct pcap_pkthdr *pcap_hdr,
                   const uint8_t *wirebits)
{
	static const struct flow_pkt zp = { 0 };
	struct flow_pkt *pkt;
	char errstr[DECODE_ERRBUF_SIZE];

	pkt = malloc(sizeof(struct flow_pkt));
	*pkt = zp;

	if (0 == decode_ethernet(pcap_hdr, wirebits, pkt, errstr)) {
		update_stats_tables(pkt);
	} else {
		mvprintw(ERR_LINE_OFFSET, 0, "%-80s", errstr);
	}

	free(pkt);
}

void grab_packets(int fd, pcap_t *handle)
{
	struct timespec timeout_ts = {.tv_sec = 1, .tv_nsec = 0 };
	struct pollfd fds[] = {
		{.fd = fd, .events = POLLIN, .revents = POLLHUP }
	};

	int ch;

	while (1) {
		if (ppoll(fds, 1, &timeout_ts, NULL)) {
			pcap_dispatch(handle, 0, handle_packet, NULL);
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
	mvprintw(0, 0, "Device:");
	attron(A_BOLD);
	mvprintw(0, 10, "%s\n", dev);
	attroff(A_BOLD);

	grab_packets(selectable_fd, handle);

	/* And close the session */
	pcap_close(handle);
	return 0;
}
