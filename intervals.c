#define _GNU_SOURCE
#include <time.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <sched.h>
#include <pcap.h>
#include <pthread.h>
#include <poll.h>
#include <errno.h>

#include "utlist.h"
#include "uthash.h"

#include "flow.h"
#include "decode.h"
#include "timeywimey.h"

#include "intervals.h"

struct flow_hash {
	struct flow_record f;
	union {
		UT_hash_handle r_hh;  /* sliding window reference table */
		UT_hash_handle ts_hh; /* time series tables */
	};
};

struct flow_pkt_list {
	struct flow_pkt pkt;
	struct flow_pkt_list *next, *prev;
};


typedef int (*pcap_decoder)(const struct pcap_pkthdr *h,
                            const uint8_t *wirebits,
                            struct flow_pkt *pkt,
                            char *errstr);

/* userdata for callback used in pcap_dispatch */
struct pcap_handler_user {
	pcap_decoder decoder; /* callback / function pointer */
	struct {
		int err;
		char errstr[DECODE_ERRBUF_SIZE];
	} result;
};

struct pcap_info {
	pcap_t *handle;
	int selectable_fd;
	struct pcap_handler_user decoder_cbdata;
};

struct tt_thread_private {
	struct pcap_info pi;
};

/* long, continuous sliding window tracking top flows */
static struct flow_hash *flow_ref_table = NULL;

/* packet list enables removing expired packets from flow table */
static struct flow_pkt_list *pkt_list_ref_head = NULL;

/* flows recorded as period-on-period intervals */
static struct flow_hash *incomplete_flow_tables[INTERVAL_COUNT] = { NULL };
static struct flow_hash *complete_flow_tables[INTERVAL_COUNT] = { NULL };

static struct timeval interval_end[INTERVAL_COUNT] = { 0 };
static struct timeval interval_start[INTERVAL_COUNT] = { 0 };

static struct timeval ref_window_size;

static struct {
	int64_t bytes;
	int64_t packets;
} totals;

static void clear_table(int table_idx)
{
	struct flow_hash *table, *iter, *tmp;

	/* clear the complete table */
	table = complete_flow_tables[table_idx];
	HASH_ITER(ts_hh, table, iter, tmp)
	{
		HASH_DELETE(ts_hh, table, iter);
		free(iter);
	}
	assert(0 == HASH_CNT(ts_hh, table));
	complete_flow_tables[table_idx] = NULL;

	/* copy incomplete to complete */
	HASH_ITER(ts_hh, incomplete_flow_tables[table_idx], iter, tmp)
	{
		/* TODO: copy and insert */
		struct flow_hash *n = malloc(sizeof(struct flow_hash));
		memcpy(n, iter, sizeof(struct flow_hash));
		HASH_ADD(ts_hh, complete_flow_tables[table_idx], f.flow,
		         sizeof(struct flow), n);
	}
	assert(HASH_CNT(ts_hh, complete_flow_tables[table_idx])
	       == HASH_CNT(ts_hh, incomplete_flow_tables[table_idx]));

	/* clear the incomplete table */
	table = incomplete_flow_tables[table_idx];
	HASH_ITER(ts_hh, table, iter, tmp)
	{
		HASH_DELETE(ts_hh, table, iter);
		free(iter);
	}
	assert(0 == HASH_CNT(ts_hh, table));
	incomplete_flow_tables[table_idx] = NULL;
}

static void expire_old_interval_tables(struct timeval now)
{
	struct timeval tz = { 0 };

	for (int i = 0; i < INTERVAL_COUNT; i++) {
		struct timeval interval = tt_intervals[i];

		/* at start-up, end is still zero. initialise it. */
		if (0 == tv_cmp(tz, interval_end[i])) {
			interval_start[i] = now;
			interval_end[i] = tv_add(interval_start[i], interval);
		}

		/* interval elapsed? */
		if (0 < tv_cmp(now, interval_end[i])) {

			/* clear the hash table */
			clear_table(i);
			interval_start[i] = interval_end[i];
			interval_end[i] = tv_add(interval_end[i], interval);
		}
	}
}

static int bytes_cmp(struct flow_hash *f1, struct flow_hash *f2)
{
	return (f2->f.bytes - f1->f.bytes);
}

static int has_aged(struct timeval t1, struct timeval now)
{
	struct timeval expiretime = tv_add(t1, ref_window_size);

	return (tv_cmp(expiretime, now) < 0);
}

static void delete_pkt_from_ref_table(struct flow_record *fr)
{
	struct flow_hash *fte;

	HASH_FIND(r_hh, flow_ref_table,
	          &(fr->flow),
	          sizeof(struct flow), fte);
	assert(fte);

	fte->f.bytes -= fr->bytes;
	fte->f.packets -= fr->packets;

	assert(fte->f.bytes >= 0);
	assert(fte->f.packets >= 0);

	if (0 == fte->f.bytes) {
		HASH_DELETE(r_hh, flow_ref_table, fte);
	}
}

/* remove pkt from the sliding window packet list as well as reference table */
static void delete_pkt(struct flow_pkt_list *le)
{
	delete_pkt_from_ref_table(&le->pkt.flow_rec);

	totals.bytes -= le->pkt.flow_rec.bytes;
	totals.packets -= le->pkt.flow_rec.packets;

	assert(totals.bytes >= 0);
	assert(totals.packets >= 0);

	DL_DELETE(pkt_list_ref_head, le);
	free(le);
}

/*
 * remove the expired packets from the flow reference table,
 * and update totals for the sliding window reference interval
 */
static void expire_old_packets()
{
	struct flow_pkt_list *tmp, *iter;
	struct timeval now;

	gettimeofday(&now, NULL);

	DL_FOREACH_SAFE(pkt_list_ref_head, iter, tmp)
	{
		if (has_aged(iter->pkt.timestamp, now)) {
			delete_pkt(iter);
		} else {
			break;
		}
	}
}


static void clear_ref_table(void)
{
	struct flow_pkt_list *tmp, *iter;

	DL_FOREACH_SAFE(pkt_list_ref_head, iter, tmp)
	{
		delete_pkt(iter);
	}

	assert(totals.packets == 0);
	assert(totals.bytes == 0);
}

/*
 * Clear all the flow tables (reference table and interval tables), to purge
 * stale flows when restarting the thread (eg. switching interfaces)
 */
void clear_all_tables(void)
{
	/* clear ref table */
	clear_ref_table();

	/* clear interval tables */
	for (int i = 0; i < INTERVAL_COUNT; i++)
		clear_table(i);
}

/*
 * add the packet to the flow reference table.
 *
 * The reference table stores all the flows observed in a sliding window that
 * is as long as the longest of the period-on-period-type intervals.
 */
static void add_flow_to_ref_table(struct flow_pkt *pkt)
{
	struct flow_hash *fte;
	struct flow_pkt_list *ple;

	/* keep a list of packets, used for sliding window byte counts */
	ple = malloc(sizeof(struct flow_pkt_list));
	ple->pkt = *pkt;
	DL_APPEND(pkt_list_ref_head, ple);

	/* Update the flow accounting table */
	/* id already in the hash? */
	HASH_FIND(r_hh, flow_ref_table, &(pkt->flow_rec.flow),
	          sizeof(struct flow), fte);
	if (!fte) {
		fte = (struct flow_hash *)malloc(sizeof(struct flow_hash));
		memset(fte, 0, sizeof(struct flow_hash));
		memcpy(&(fte->f), &(pkt->flow_rec), sizeof(struct flow_record));
		HASH_ADD(r_hh, flow_ref_table, f.flow, sizeof(struct flow),
		         fte);
	} else {
		fte->f.bytes += pkt->flow_rec.bytes;
		fte->f.packets += pkt->flow_rec.packets;
	}

	totals.bytes += pkt->flow_rec.bytes;
	assert(totals.bytes >= 0);
	totals.packets += pkt->flow_rec.packets;
	assert(totals.packets >= 0);
}

/*
 * add the packet to the period-on-period interval table for the selected
 * time series / interval.
 */
static void add_flow_to_interval(struct flow_pkt *pkt, int time_series)
{
	struct flow_hash *fte;

	/* Update the flow accounting table */
	/* id already in the hash? */
	HASH_FIND(ts_hh, incomplete_flow_tables[time_series],
	          &(pkt->flow_rec.flow), sizeof(struct flow), fte);
	if (!fte) {
		fte = (struct flow_hash *)malloc(sizeof(struct flow_hash));
		memset(fte, 0, sizeof(struct flow_hash));
		memcpy(&(fte->f), &(pkt->flow_rec), sizeof(struct flow_record));
		HASH_ADD(ts_hh, incomplete_flow_tables[time_series], f.flow,
		         sizeof(struct flow), fte);
	} else {
		fte->f.bytes += pkt->flow_rec.bytes;
		fte->f.packets += pkt->flow_rec.packets;
	}
}

static inline unsigned int rate_calc(struct timeval interval, int bytes)
{
	double dt = interval.tv_sec + interval.tv_usec * 1E-6;
	return (unsigned int)((float)bytes / dt);
}

static void fill_short_int_flows(struct flow_record st_flows[INTERVAL_COUNT],
                                 const struct flow_hash *ref_flow)
{
	struct flow_hash *fti; /* flow table iter (short-interval tables */
	struct flow_hash *te;  /* flow table entry */

	/* for each table in all time intervals.... */
	for (int i = INTERVAL_COUNT - 1; i >= 0; i--) {
		fti = complete_flow_tables[i];
		memcpy(&st_flows[i], &(ref_flow->f),
		       sizeof(struct flow_record));

		if (!fti) {
			/* table doesn't have anything in it yet */
			st_flows[i].bytes = 0;
			st_flows[i].packets = 0;
			continue;
		}

		/* try to find the reference flow in the short flow table */
		HASH_FIND(ts_hh, fti, &(ref_flow->f.flow),
		          sizeof(struct flow), te);

		st_flows[i].bytes = te ? te->f.bytes : 0;
		st_flows[i].packets = te ? te->f.packets : 0;

		/* convert to bytes per second */
		st_flows[i].bytes =
		    rate_calc(tt_intervals[i], st_flows[i].bytes);
		/* convert to packets per second */
		st_flows[i].packets =
		    rate_calc(tt_intervals[i], st_flows[i].packets);
	}
}

static void update_stats_tables(struct flow_pkt *pkt)
{
	expire_old_packets();
	add_flow_to_ref_table(pkt);

	for (int i = 0; i < INTERVAL_COUNT; i++) {
		add_flow_to_interval(pkt, i);
	}
	expire_old_interval_tables(pkt->timestamp);
}

#define DEBUG 1
#if DEBUG
static void dbg_per_second(struct tt_top_flows *t5)
{
	double dt = ref_window_size.tv_sec + ref_window_size.tv_usec * 1E-6;

	printf("\rref window: %f, flows:  %ld total bytes:   %ld, Bps: %lu total packets: %ld, pps: %lu\n",
	dt, t5->flow_count, totals.bytes, t5->total_bytes, totals.packets, t5->total_packets);
}
#endif

void tt_get_top5(struct tt_top_flows *t5)
{
	struct timeval now;
	struct flow_hash *rfti; /* reference flow table iter */

	/* sort the flow reference table */
	HASH_SRT(r_hh, flow_ref_table, bytes_cmp);

	gettimeofday(&now, NULL);
	expire_old_packets();
	expire_old_interval_tables(now);

	/* for each of the top 5 flow in the reference table,
	 * fill the counts from the short-interval flow tables */
	rfti = flow_ref_table;

	for (int i = 0; i < MAX_FLOW_COUNT && rfti; i++) {
		fill_short_int_flows(t5->flow[i], rfti);
		rfti = rfti->r_hh.next;
	}
	t5->flow_count = HASH_CNT(r_hh, flow_ref_table);

	t5->total_bytes = rate_calc(ref_window_size, totals.bytes);
	t5->total_packets = rate_calc(ref_window_size, totals.packets);

#if DEBUG
	if (t5->flow_count == 0 &&
	    (t5->total_bytes > 0 || t5->total_packets > 0)) {
		fprintf(stderr, "logic error in %s. flows is 0, but bytes and packets are > 0\n", __func__);
		dbg_per_second(t5);
		assert(0);
	} else if (t5->flow_count > 0 && totals.bytes == 0) {
		fprintf(stderr, "logic error in %s. flows is >0, but bytes are 0\n", __func__);
		dbg_per_second(t5);
		assert(0);
	} else if (t5->flow_count > 0 && totals.packets == 0) {
		fprintf(stderr, "logic error in %s. flows is >0, but packets are 0\n", __func__);
		dbg_per_second(t5);
		assert(0);
	}
#endif
}

int tt_get_flow_count()
{
	return HASH_CNT(r_hh, flow_ref_table);
}

void tt_update_ref_window_size(struct tt_thread_info *ti, struct timeval t)
{
	pthread_mutex_lock(&ti->t5_mutex);
	ref_window_size = t;
	pthread_mutex_unlock(&ti->t5_mutex);
}

static void handle_packet(uint8_t *user, const struct pcap_pkthdr *pcap_hdr,
                          const uint8_t *wirebits)
{
	struct pcap_handler_user *cbdata = (struct pcap_handler_user *)user;
	char errstr[DECODE_ERRBUF_SIZE];
	struct flow_pkt pkt = { 0 };

	if (0 == cbdata->decoder(pcap_hdr, wirebits, &pkt, errstr)) {
		update_stats_tables(&pkt);
		cbdata->result.err = 0;
	} else {
		cbdata->result.err = -1;
		snprintf(cbdata->result.errstr, DECODE_ERRBUF_SIZE - 1, "%s", errstr);
	}
}

static int init_pcap(char **dev, struct pcap_info *pi)
{
	char errbuf[PCAP_ERRBUF_SIZE];
	int dlt; /* pcap data link type */

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

	dlt = pcap_datalink(pi->handle);
	switch (dlt) {
	case DLT_EN10MB:
		pi->decoder_cbdata.decoder = decode_ethernet;
		break;
	case DLT_LINUX_SLL:
		pi->decoder_cbdata.decoder = decode_linux_sll;
		break;
	default:
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

static int free_pcap(struct pcap_info *pi)
{
	pcap_close(pi->handle);
	return 0;
}

static void set_affinity(struct tt_thread_info *ti)
{
	int s, j;
	cpu_set_t cpuset;
	pthread_t thread;
	thread = pthread_self();

	/* Set affinity mask to include CPUs 1 only */
	CPU_ZERO(&cpuset);
#ifndef RT_CPU
#define RT_CPU 0
#endif
	CPU_SET(RT_CPU, &cpuset);
	s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (s != 0) {
		handle_error_en(s, "pthread_setaffinity_np");
	}

	/* Check the actual affinity mask assigned to the thread */
	s = pthread_getaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (s != 0) {
		handle_error_en(s, "pthread_getaffinity_np");
	}

	printf("RT thread [%s] priority [%d] CPU affinity: ",
	       ti->thread_name,
	       ti->thread_prio);
	for (j = 0; j < CPU_SETSIZE; j++) {
		if (CPU_ISSET(j, &cpuset)) {
			printf(" CPU%d", j);
		}
	}
	printf("\n");
}

static int init_realtime(struct tt_thread_info *ti)
{
	struct sched_param schedparm;
	memset(&schedparm, 0, sizeof(schedparm));
	schedparm.sched_priority = ti->thread_prio;
	sched_setscheduler(0, SCHED_FIFO, &schedparm);
	set_affinity(ti);
	return 0;
}

void *tt_intervals_run(void *p)
{
	struct pcap_handler_user *cbdata;
	struct tt_thread_info *ti = (struct tt_thread_info *)p;
	struct timespec poll_timeout = {.tv_sec = 0, .tv_nsec = 1E8 };

	assert(ti);

	init_realtime(ti);

	assert(ti->priv);
	assert(ti->priv->pi.handle);
	assert(ti->priv->pi.selectable_fd);
	assert(ti->priv->pi.decoder_cbdata.decoder);

	cbdata = &ti->priv->pi.decoder_cbdata;

	struct pollfd fds[] = {
		{ .fd = ti->priv->pi.selectable_fd,
		  .events = POLLIN,
		  .revents = 0
		}
	};

	while (1) {
		if (ppoll(fds, 1, &poll_timeout, NULL)) {
			int cnt, max = 100000;
			cnt = pcap_dispatch(ti->priv->pi.handle, max,
			                    handle_packet, (u_char *)cbdata);
			if (cnt && cbdata->result.err) {
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
		pthread_mutex_lock(&ti->t5_mutex);
		tt_get_top5(ti->t5);
		pthread_mutex_unlock(&ti->t5_mutex);
	}

	/* close the pcap session */
	pcap_close(ti->priv->pi.handle);
	return NULL;
}

int tt_intervals_init(struct tt_thread_info *ti)
{
	int err;
	pthread_mutex_init(&(ti->t5_mutex), NULL);

	ref_window_size = (struct timeval){.tv_sec = 3, .tv_usec = 0 };
	flow_ref_table = NULL;
	pkt_list_ref_head = NULL;

	ti->t5 = calloc(1, sizeof(struct tt_top_flows));
	if (!ti->t5) { return 1; }

	ti->priv = calloc(1, sizeof(struct tt_thread_private));
	if (!ti->priv) { goto cleanup1; }

	err = init_pcap(&(ti->dev), &(ti->priv->pi));
	if (err) {
		errno = err;
		perror("init_pcap failed");
		goto cleanup;
	}

	return 0;

cleanup:
	free(ti->priv);
cleanup1:
	free(ti->t5);
	return 1;
}

int tt_intervals_free(struct tt_thread_info *ti)
{
	assert(ti);
	assert(ti->priv);
	assert(ti->t5);

	clear_all_tables();

	free_pcap(&(ti->priv->pi));
	free(ti->priv);
	free(ti->t5);
	return 0;
}
