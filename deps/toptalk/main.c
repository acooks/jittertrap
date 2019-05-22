#define _GNU_SOURCE
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <ncurses.h>
#include <pthread.h>
#include <netinet/ip.h>
#include <errno.h>

#include "flow.h"
#include "timeywimey.h"
#include "intervals.h"

static char const *const protos[IPPROTO_MAX] = {[IPPROTO_TCP] = "TCP",
	                                        [IPPROTO_UDP] = "UDP",
	                                        [IPPROTO_ICMP] = "ICMP",
	                                        [IPPROTO_ICMPV6] = "ICMP6",
	                                        [IPPROTO_IP] = "IP",
	                                        [IPPROTO_IGMP] = "IGMP",
						[IPPROTO_ESP] = "ESP" };

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

static char const * const dscpvalues[] = {
	[IPTOS_DSCP_AF11] = "AF11",
	[IPTOS_DSCP_AF12] = "AF12",
	[IPTOS_DSCP_AF13] = "AF13",
	[IPTOS_DSCP_AF21] = "AF21",
	[IPTOS_DSCP_AF22] = "AF22",
	[IPTOS_DSCP_AF23] = "AF23",
	[IPTOS_DSCP_AF31] = "AF31",
	[IPTOS_DSCP_AF32] = "AF32",
	[IPTOS_DSCP_AF33] = "AF33",
	[IPTOS_DSCP_AF41] = "AF41",
	[IPTOS_DSCP_AF42] = "AF42",
	[IPTOS_DSCP_AF43] = "AF43",
	[IPTOS_DSCP_EF]   = "EF",
	[IPTOS_CLASS_CS0] = "CS0",
	[IPTOS_CLASS_CS1] = "CS1",
	[IPTOS_CLASS_CS2] = "CS2",
	[IPTOS_CLASS_CS3] = "CS3",
	[IPTOS_CLASS_CS4] = "CS4",
	[IPTOS_CLASS_CS5] = "CS5",
	[IPTOS_CLASS_CS6] = "CS6",
	[IPTOS_CLASS_CS7] = "CS7"
};


#define ERR_LINE_OFFSET 2
#define DEBUG_LINE_OFFSET 3
#define TOP_N_LINE_OFFSET 5
#define TP1_COL 53
#define TP2_COL 64

/* two displayed intervals */
int interval1 = 4, interval2 = 3;

void update_interval(struct tt_thread_info *ti, int updown)
{
	mvprintw(ERR_LINE_OFFSET, 0, "%50s", " ");
	if ((0 > updown) && (interval2 > 0)) {
		interval1--;
		interval2--;
	} else if ((0 < updown) && (interval1 < INTERVAL_COUNT - 1)) {
		interval1++;
		interval2++;
	} else {
		mvprintw(ERR_LINE_OFFSET, 1, "Requested interval invalid.");
	}
	tt_update_ref_window_size(ti, tt_intervals[interval1]);
}

void range(int v, int *byteunit, int *div)
{
	if (v > 1E10) {
		*byteunit = GBPS;
		*div = 1E9;
	} else if (v > 1E7) {
		*byteunit = MBPS;
		*div = 1E6;
	} else if (v > 1E4) {
		*byteunit = KBPS;
		*div = 1E3;
	} else {
		*byteunit = BPS;
		*div = 1;
	}
}

int print_hdrs(int tp1, struct timeval interval1, int tp2,
               struct timeval interval2)
{
	char const *byteunit;
	int unit, div;

	float dt1 = interval1.tv_sec + (float)interval1.tv_usec / (float)1E6;
	float dt2 = interval2.tv_sec + (float)interval2.tv_usec / (float)1E6;

	range(tp1, &unit, &div);
	byteunit = byteunits[unit];

	attron(A_BOLD);
	mvprintw(TOP_N_LINE_OFFSET, 1, "%52s", "Source|SPort|Proto|");
	mvprintw(TOP_N_LINE_OFFSET + 1, 1, "%52s", "Destination|DPort|DSCP |");
	mvaddch(TOP_N_LINE_OFFSET + 0, 40, ACS_VLINE);
	mvaddch(TOP_N_LINE_OFFSET + 1, 40, ACS_VLINE);

	mvaddch(TOP_N_LINE_OFFSET + 0, 46, ACS_VLINE);
	mvaddch(TOP_N_LINE_OFFSET + 1, 46, ACS_VLINE);
	mvaddch(TOP_N_LINE_OFFSET + 0, 52, ACS_VLINE);
	mvaddch(TOP_N_LINE_OFFSET + 1, 52, ACS_VLINE);



	if (dt1 > 1) {
		mvprintw(TOP_N_LINE_OFFSET + 1, TP1_COL,
		         "%4s @%4.f%2s|%4s @%4.f%2s",
		         byteunit, dt1, intervalunits[SECONDS],
		         byteunit, dt2, intervalunits[SECONDS]);
	} else {
		mvprintw(TOP_N_LINE_OFFSET + 1, TP1_COL,
		         "%4s @%4.f%2s|%4s @%4.f%2s",
		         byteunit, dt1 * 1E3, intervalunits[MILLISECONDS],
		         byteunit, dt2 * 1E3, intervalunits[MILLISECONDS]);
	}
	mvaddch(TOP_N_LINE_OFFSET + 1, 65, ACS_VLINE);

	attroff(A_BOLD);
	return div;
}

void print_flow(int row, char *src, char *dst, struct flow_record *fte1,
		struct flow_record *fte2, int div)
{
	mvaddch(TOP_N_LINE_OFFSET + row + 0, 0, ACS_ULCORNER);
	mvaddch(TOP_N_LINE_OFFSET + row + 1, 0, ACS_LLCORNER);
	mvprintw(TOP_N_LINE_OFFSET + row + 0, 1, "%39s",
	         src);
	mvprintw(TOP_N_LINE_OFFSET + row + 1, 1, "%39s",
	         dst);
	mvprintw(TOP_N_LINE_OFFSET + row + 0, 40, "%6d",
	         fte1->flow.sport);
	mvprintw(TOP_N_LINE_OFFSET + row + 1, 40, "%6d",
	         fte1->flow.dport);
	mvprintw(TOP_N_LINE_OFFSET + row + 0, 47, "%s",
	         protos[fte1->flow.proto]);
	mvprintw(TOP_N_LINE_OFFSET + row + 1, 47, "%03s",
	         dscpvalues[fte1->flow.tclass]);
	mvprintw(TOP_N_LINE_OFFSET + row + 1, 55, "%10d  %10d",
	         fte1->bytes / div, fte2->bytes / div);
	mvprintw(TOP_N_LINE_OFFSET + row + 2, 0, "%80s", " ");
}

void print_top_n(struct tt_top_flows *t5)
{
	int row = 3;
	char ip_src[16];
	char ip_dst[16];
	char ip6_src[40];
	char ip6_dst[40];
	int div, unit;
	char const *byteunit;

	range(t5->total_bytes, &unit, &div);
	byteunit = byteunits[unit];
	mvprintw(0, 50, "%5d active flows", t5->flow_count);
	mvprintw(1, 50, "%5d %s    ", t5->total_bytes / div, byteunit);
	mvprintw(2, 50, "%5d Pkts/s", t5->total_packets);

	/* Clear the table */
	for (int i = TOP_N_LINE_OFFSET + row;
	     i <= TOP_N_LINE_OFFSET + row + 3 * MAX_FLOW_COUNT; i++) {
		mvprintw(i, 0, "%80s", " ");
	}

	for (int i = 0; i < t5->flow_count && i < MAX_FLOW_COUNT; i++) {
		struct flow_record *fte1 = &(t5->flow[i][interval1]);
		struct flow_record *fte2 = &(t5->flow[i][interval2]);

		sprintf(ip_src, "%s", inet_ntoa(fte1->flow.src_ip));
		sprintf(ip_dst, "%s", inet_ntoa(fte1->flow.dst_ip));
		inet_ntop(AF_INET6, &(fte1->flow.src_ip6), ip6_src,
		          sizeof(ip6_src));
		inet_ntop(AF_INET6, &(fte1->flow.dst_ip6), ip6_dst,
		          sizeof(ip6_dst));

		if (0 == i) {
			div = print_hdrs(fte1->bytes, tt_intervals[interval1],
			                 fte2->bytes, tt_intervals[interval2]);
		}

		switch (fte1->flow.ethertype) {
		case ETHERTYPE_IP:
			print_flow(row, ip_src, ip_dst, fte1, fte2, div);
			row += 3;
			break;

		case ETHERTYPE_IPV6:
			print_flow(row, ip6_src, ip6_dst, fte1, fte2, div);
			row += 3;
			break;
		default:
			mvprintw(ERR_LINE_OFFSET, 0, "%50s", " ");
			mvprintw(ERR_LINE_OFFSET, 0, "Unknown ethertype: %d",
			         fte1->flow.ethertype);
		}
	}
}

void stamp_datetime(char *buf, size_t len, struct timespec t)
{
	time_t timer;
	struct tm* tm_info;

	time(&timer);
	tm_info = localtime(&timer);

	strftime(buf, len - 4, "%Y-%m-%d %H:%M:%S", tm_info);
	snprintf(buf + strlen(buf), 4, ".%02.0f", t.tv_nsec / 1E7);
}

void handle_io(struct tt_thread_info *ti)
{
	struct timespec t1;
	struct timespec print_deadline,
	                print_interval = {.tv_sec = 0, .tv_nsec = 1E8};
#if DEBUG
	struct timespec t2, td, print_deadline_ext,
	                print_gracetime = {.tv_sec = 0, .tv_nsec = 1E6};
#endif
	int ch, stop = 0;
	const char *errstr = NULL;
	char datetime[26];

	initscr();            /* Start curses mode              */
	raw();                /* Line buffering disabled        */
	keypad(stdscr, TRUE); /* We get F1, F2 etc..            */
	noecho();             /* Don't echo() while we do getch */
	nodelay(stdscr, TRUE);

	mvprintw(1, 0, "Device:");
	attron(A_BOLD);
	mvprintw(1, 10, "%s\n", ti->dev);
	attroff(A_BOLD);

	while (!stop) {
		clock_gettime(CLOCK_REALTIME, &t1);
		stamp_datetime(datetime, sizeof(datetime), t1);
		mvprintw(0, 0, "%s", datetime);

		pthread_mutex_lock(&ti->t5_mutex);
		print_top_n(ti->t5);
		pthread_mutex_unlock(&ti->t5_mutex);

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
				update_interval(ti, -1);
				break;
			case '=':
			case '+':
				update_interval(ti, 1);
				break;
			}
		}

		print_deadline = ts_add(t1, print_interval);
		clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME,
		                &print_deadline, NULL);

#if DEBUG
		clock_gettime(CLOCK_REALTIME, &t2);
		print_deadline_ext = ts_add(print_deadline, print_gracetime);
		td = ts_absdiff(t2, t1);
		if (ts_cmp(t2, print_deadline_ext) > 0)
			mvprintw(DEBUG_LINE_OFFSET, 0,
			         "screen froze for %ld.%09ld seconds\n",
			         td.tv_sec, td.tv_nsec);
#endif

		void *ret;
		if (EBUSY != pthread_tryjoin_np(ti->thread_id, &ret)) {
			errstr = "Interval thread died.";
			stop = 1;
		}
	}

	refresh();

	/* End curses mode */
	endwin();

	pthread_cancel(ti->thread_id);

	if (errstr) {
		fprintf(stderr, "%s\n", errstr);
	}
}

void init_thread(struct tt_thread_info *ti)
{
	int err;

	err = tt_intervals_init(ti);
	if (err) {
		/* pcap doesn't return a proper errno, but prints its own msg */
		exit(EXIT_FAILURE);
	}

	err = pthread_attr_init(&ti->attr);
	if (err) {
		handle_error_en(err, "pthread_attr_init");
	}

	err = pthread_create(&ti->thread_id, &ti->attr, tt_intervals_run, ti);
	if (err) {
		handle_error_en(err, "pthread_create");
	}
	err = pthread_setname_np(ti->thread_id, ti->thread_name);
	if (err) {
		handle_error_en(err, "pthread_setname_np");
	}
}

int main(int argc, char *argv[])
{
	struct tt_thread_info ti = {
		0,
		.thread_name = "tt-intervals",
		.thread_prio = 0
	};

	if (argc == 2) {
		ti.dev = argv[1];
	} else {
		ti.dev = NULL;
	}

	/* start & run thread for capture and interval processing */
	init_thread(&ti);

	/* print top flows and handle user input */
	handle_io(&ti);

	void *res;
	pthread_join(ti.thread_id, &res);

	tt_intervals_free(&ti);

	return 0;
}
