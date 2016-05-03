#define _GNU_SOURCE
#include <signal.h>
#include <poll.h>
#include <stdio.h>
#include <pcap.h>
#include <time.h>
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <ncurses.h>
#include <pthread.h>
#include <errno.h>

#include "utlist.h"
#include "uthash.h"
#include "flow.h"
#include "decode.h"
#include "timeywimey.h"
#include "intervals.h"

#define handle_error_en(en, msg) \
	do {                                                                   \
		errno = en;                                                    \
		perror(msg);                                                   \
		exit(EXIT_FAILURE);                                            \
	} while (0)

pthread_t thread_id;
pthread_attr_t attr;

struct pcap_info {
	pcap_t *handle;
	int selectable_fd;
};

struct pcap_handler_result {
	int err;
	char errstr[DECODE_ERRBUF_SIZE];
};

struct thread_info {
	char *dev;
	struct top_flows *t5;
	unsigned int decode_errors;
};

static pthread_mutex_t t5_mutex;

static char const * const protos[IPPROTO_MAX] = {[IPPROTO_TCP] = "TCP",
	                                        [IPPROTO_UDP] = "UDP",
	                                        [IPPROTO_ICMP] = "ICMP",
	                                        [IPPROTO_ICMPV6] = "ICMP6",
	                                        [IPPROTO_IP] = "IP",
	                                        [IPPROTO_IGMP] = "IGMP" };

enum speeds { BPS, KBPS, MBPS, GBPS };
enum intervals { MILLISECONDS, SECONDS };

static char const * const byteunits[] = {
	[BPS]  = "B/s",
	[KBPS] = "kB/s",
	[MBPS] = "MB/s",
	[GBPS] = "GB/s"
};

static char * const intervalunits[] = {
	[MILLISECONDS] = "ms",
	[SECONDS]      = "s "
};

#define ERR_LINE_OFFSET 2
#define DEBUG_LINE_OFFSET 3
#define TOP_N_LINE_OFFSET 5
#define TP1_COL 47
#define TP2_COL 59

/* two displayed intervals */
int interval1 = 4, interval2 = 3;

void update_interval(int updown)
{
	if ((0 > updown) && (interval2 > 0)) {
		interval1--;
		interval2--;
	} else if ((0 < updown) && (interval1 < INTERVAL_COUNT -1)) {
		interval1++;
		interval2++;
	} else {
		mvprintw(ERR_LINE_OFFSET, 1, "Requested interval invalid.");
	}
	update_ref_window_size(intervals[interval1]);
}

int print_hdrs(int tp1, struct timeval interval1, int tp2,
                  struct timeval interval2)
{
	char const * byteunit;
	int div;

	float dt1 = interval1.tv_sec + (float)interval1.tv_usec / (float)1E6;
	float dt2 = interval2.tv_sec + (float)interval2.tv_usec / (float)1E6;

	if (tp1 > 1E9) {
		byteunit = byteunits[GBPS];
		div = 1E9;
	} else if (tp1 > 1E6) {
		byteunit = byteunits[MBPS];
		div = 1E6;
	} else if (tp1 > 1E3) {
		byteunit = byteunits[KBPS];
		div = 1E3;
	} else {
		byteunit = byteunits[BPS];
		div = 1;
	}

#if DEBUG
	mvprintw(DEBUG_LINE_OFFSET, 1,
	         "tp1: %d byteunit:%s div:%d",
	         tp1, byteunit, div);
#endif

	attron(A_BOLD);
	mvprintw(TOP_N_LINE_OFFSET, 1, "%51s", "Source|SPort|Proto");
	mvprintw(TOP_N_LINE_OFFSET + 1, 1, "%46s", "Destination|DPort|");

	if (dt1 > 1) {
		mvprintw(TOP_N_LINE_OFFSET + 1, TP1_COL,
		         "%4s @%3.f%s|%4s @%3.f%2s",
		         byteunit, dt1, intervalunits[SECONDS],
		         byteunit, dt2, intervalunits[SECONDS]);
	} else {
		mvprintw(TOP_N_LINE_OFFSET + 1, TP1_COL,
		         "%4s @%3.f%s|%4s @%3.f%2s",
		         byteunit, dt1 * 1E3, intervalunits[MILLISECONDS],
		         byteunit, dt2 * 1E3, intervalunits[MILLISECONDS]);
	}

	attroff(A_BOLD);
	return div;
}

void print_top_n(int stop, struct top_flows *t5)
{
	int row = 3;
	char ip_src[16];
	char ip_dst[16];
	char ip6_src[40];
	char ip6_dst[40];

	mvprintw(0, 50, "%5d active flows", t5->count);

	/* Clear the table */
	for (int i = TOP_N_LINE_OFFSET + row;
	     i <= TOP_N_LINE_OFFSET + row + 3 * stop; i++) {
		mvprintw(i, 0, "%80s", " ");
	}

	for (int i = 0; i < t5->count && i < 5; i++) {
		int div;
		struct flow_record *fte1 = &(t5->flow[i][interval1]);
		struct flow_record *fte2 = &(t5->flow[i][interval2]);

		sprintf(ip_src, "%s", inet_ntoa(fte1->flow.src_ip));
		sprintf(ip_dst, "%s", inet_ntoa(fte1->flow.dst_ip));
		inet_ntop(AF_INET6, &(fte1->flow.src_ip6), ip6_src,
		          sizeof(ip6_src));
		inet_ntop(AF_INET6, &(fte1->flow.dst_ip6), ip6_dst,
		          sizeof(ip6_dst));

		if (0 == i) {
			div = print_hdrs(fte1->size, intervals[interval1],
			                 fte2->size, intervals[interval2]);
		}

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
			         fte1->size / div, fte2->size / div);
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
			         fte1->size / div, fte2->size / div);
			mvprintw(TOP_N_LINE_OFFSET + row + 2, 0, "%80s", " ");
			row += 3;
			break;
		default:
			mvprintw(ERR_LINE_OFFSET, 0, "%80s", " ");
			mvprintw(ERR_LINE_OFFSET, 0, "Unknown ethertype: %d",
			         fte1->flow.ethertype);
		}
	}
}



void handle_packet(uint8_t *user, const struct pcap_pkthdr *pcap_hdr,
                   const uint8_t *wirebits)
{
	struct pcap_handler_result *result = (struct pcap_handler_result *)user;
	static const struct flow_pkt zp = { 0 };
	struct flow_pkt *pkt;
	char errstr[DECODE_ERRBUF_SIZE];

	pkt = malloc(sizeof(struct flow_pkt));
	*pkt = zp;

	if (0 == decode_ethernet(pcap_hdr, wirebits, pkt, errstr)) {
		update_stats_tables(pkt);
		result->err = 0;
	} else {
		result->err = -1;
		snprintf(result->errstr, DECODE_ERRBUF_SIZE-1, "%s", errstr);
	}

	free(pkt);
}



void handle_io(char *dev, struct top_flows *t5)
{
	struct timespec print_timeout = {.tv_sec = 0, .tv_nsec = 1E8 };
	struct timespec now;
	int ch, stop = 0;

	initscr();            /* Start curses mode              */
	raw();                /* Line buffering disabled        */
	keypad(stdscr, TRUE); /* We get F1, F2 etc..            */
	noecho();             /* Don't echo() while we do getch */
	nodelay(stdscr, TRUE);

	mvprintw(0, 0, "Device:");
	attron(A_BOLD);
	mvprintw(0, 10, "%s\n", dev);
	attroff(A_BOLD);

	while (!stop) {
		clock_gettime(CLOCK_REALTIME, &now);
		mvprintw(DEBUG_LINE_OFFSET, 0, "%20d", now.tv_sec);

		pthread_mutex_lock(&t5_mutex);
		print_top_n(5, t5);
		pthread_mutex_unlock(&t5_mutex);

		refresh(); /* ncurses screen update */

		if ((ch = getch()) == ERR) {
			/* normal case - no input */
			;
		} else {
			switch (ch) {
			case 'q':
				stop = 1;
				break;
			case '-':
				update_interval(-1);
				break;
			case '=':
			case '+':
				update_interval(1);
				break;
			}
		}

		nanosleep (&print_timeout, NULL);
		void *ret;
		if (EBUSY != pthread_tryjoin_np(thread_id, &ret)) {
			mvprintw(ERR_LINE_OFFSET, 0, "%20s",
			         "Interval thread died.");
			stop = 1;
		}
	}

	refresh();

	/* End curses mode */
	endwin();

	pthread_cancel(thread_id);
}

int init_pcap(char **dev, struct pcap_info *pi)
{
	char errbuf[PCAP_ERRBUF_SIZE];

	if (*dev == NULL) {
		*dev = pcap_lookupdev(errbuf);
	}

	if (*dev == NULL) {
		fprintf(stderr, "Couldn't find default device: %s\n", errbuf);
		return (2);
	}

	pi->handle = pcap_open_live(*dev, BUFSIZ, 1, 0, errbuf);
	if (pi->handle == NULL) {
		fprintf(stderr, "Couldn't open device %s: %s\n", *dev, errbuf);
		return (2);
	}

	if (pcap_datalink(pi->handle) != DLT_EN10MB) {
		fprintf(stderr, "Device %s doesn't provide Ethernet headers - "
		                "not supported\n",
		        *dev);
		return (2);
	}

	if (pcap_setnonblock(pi->handle, 1, errbuf) != 0) {
		fprintf(stderr, "Non-blocking mode failed: %s\n", errbuf);
		return (2);
	}

	pi->selectable_fd = pcap_get_selectable_fd(pi->handle);
	if (-1 == pi->selectable_fd) {
		fprintf(stderr, "pcap handle not selectable.\n");
		return (2);
	}
	return 0;
}

void *thread_run(void *p)
{
	int err;
	struct pcap_info pi;
	struct pcap_handler_result result;
	struct thread_info *ti = (struct thread_info *)p;
	struct timespec poll_timeout = {.tv_sec = 0, .tv_nsec = 1E8 };

	err = init_pcap(&(ti->dev), &pi);
	if (err) {
		handle_error_en(err, "pcap init");
	}
	assert(pi.handle);
	assert(pi.selectable_fd);

	struct pollfd fds[] = {
		{.fd = pi.selectable_fd, .events = POLLIN, .revents = 0 }
	};

	init_intervals((struct timeval){.tv_sec = 3, .tv_usec = 0}, ti->t5);

	while (1) {
		if (ppoll(fds, 1, &poll_timeout, NULL)) {
			int cnt, max=100000;
			cnt = pcap_dispatch(pi.handle, max, handle_packet,
			                    (u_char *)&result);
			if (cnt && result.err) {
				/* FIXME: think of an elegant way to
				 * get the errors out of this thread. */
				ti->decode_errors++;
			}
		} else {
			/* poll timeout */
			if (fds[0].revents) {
				fprintf(stderr, "error. revents: %x\n",
				        fds[0].revents);
			}
		}
		pthread_mutex_lock(&t5_mutex);
		get_top5(ti->t5);
		pthread_mutex_unlock(&t5_mutex);
	}

	/* close the pcap session */
	pcap_close(pi.handle);
	return NULL;
}

void init_thread(struct thread_info *ti)
{
	int err;

	ti->t5 = malloc(sizeof(struct top_flows));
	memset(ti->t5, 0, sizeof(struct top_flows));

	err = pthread_attr_init(&attr);
	if (err) {
		handle_error_en(err, "pthread_attr_init");
	}

	err = pthread_create(&thread_id, &attr, thread_run, ti);
	if (err) {
		handle_error_en(err, "pthread_create");
	}
}

int main(int argc, char *argv[])
{
	struct thread_info ti = {0};

	if (argc == 2) {
		ti.dev = argv[1];
	} else {
		ti.dev = NULL;
	}

	pthread_mutex_init(&(t5_mutex), NULL);

	/* start & run thread for capture and interval processing */
	init_thread(&ti);

	/* print top flows and handle user input */
	handle_io(ti.dev, ti.t5);

	void *res;
	pthread_join(thread_id, &res);

	free(ti.t5);

	return 0;
}
