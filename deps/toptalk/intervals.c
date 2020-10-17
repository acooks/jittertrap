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

/* initialise interval start and end times */
static void init_intervals(struct timeval now)
{
	for (int i = 0; i < INTERVAL_COUNT; i++) {
		interval_start[i] = now;
		interval_end[i] = tv_add(interval_start[i], tt_intervals[i]);
	}
}

static void expire_old_interval_tables(struct timeval now)
{
	for (int i = 0; i < INTERVAL_COUNT; i++) {
		struct timeval interval = tt_intervals[i];

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

/* t1 is the packet timestamp; deadline is the end of the current tick */
static int has_aged(struct timeval t1, struct timeval deadline)
{
	struct timeval expiretime = tv_add(t1, ref_window_size);

	return (tv_cmp(expiretime, deadline) < 0);
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
		free(fte);
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
 *
 * NB: this must be called in both the packet receive and stats calculation
 * paths, because the total bytes/packets depend on the pkt_list and we don't
 * want to walk the whole list and redo the sum on every tick.
 */
static void expire_old_packets(struct timeval deadline)
{
	struct flow_pkt_list *tmp, *iter;

	DL_FOREACH_SAFE(pkt_list_ref_head, iter, tmp)
	{
		if (has_aged(iter->pkt.timestamp, deadline)) {
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
	/*
	 * expire the old packets in the receive path
	 * NB: must be called in stats path as well.
	 */
	expire_old_packets(pkt->timestamp);

	add_flow_to_ref_table(pkt);

	for (int i = 0; i < INTERVAL_COUNT; i++) {
		add_flow_to_interval(pkt, i);
	}
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

static void tt_get_top5(struct tt_top_flows *t5, struct timeval deadline)
{
	struct flow_hash *rfti; /* reference flow table iter */

	/* sort the flow reference table */
	HASH_SRT(r_hh, flow_ref_table, bytes_cmp);

	/*
	 * expire old packets in the output path
	 * NB: must be called in packet receive path as well.
	 */
	expire_old_packets(deadline);

	/* check if the interval is complete and then rotate tables */
	expire_old_interval_tables(deadline);

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
	t5->timestamp = deadline;

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
		snprintf(cbdata->result.errstr, DECODE_ERRBUF_SIZE, "%s", errstr);
	}
}

static int init_pcap(char **dev, struct pcap_info *pi)
{
	char errbuf[PCAP_ERRBUF_SIZE];
	int dlt; /* pcap data link type */
	pcap_if_t *alldevs;

	if (!*dev) {
		int err = pcap_findalldevs(&alldevs, errbuf);
		if (err) {
			fprintf(stderr, "Couldn't list devices: %s\n", errbuf);
			return 1;
		}

		if (!alldevs) {
			fprintf(stderr,
			        "No devices available. Check permissions.\n");
			return 1;
		}

		*dev = strdup(alldevs->name);
		pcap_freealldevs(alldevs);
	}

	if (*dev == NULL) {
		fprintf(stderr, "Couldn't find default device: %s\n", errbuf);
		return 1;
	}

	pi->handle = pcap_open_live(*dev, BUFSIZ, 1, 3, errbuf);
	if (pi->handle == NULL) {
		fprintf(stderr, "Couldn't open device %s\n", errbuf);
		return 1;
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
		return 1;
	}

	if (pcap_setnonblock(pi->handle, 1, errbuf) != 0) {
		fprintf(stderr, "Non-blocking mode failed: %s\n", errbuf);
		return 1;
	}

	pi->selectable_fd = pcap_get_selectable_fd(pi->handle);
	if (-1 == pi->selectable_fd) {
		fprintf(stderr, "pcap handle not selectable.\n");
		return 1;
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

static struct timeval ts_to_tv(struct timespec t_in)
{
	struct timeval t_out;

	t_out.tv_sec = t_in.tv_sec;
	t_out.tv_usec = t_in.tv_nsec / 1000;
	return t_out;
}

void *tt_intervals_run(void *p)
{
	struct pcap_handler_user *cbdata;
	struct tt_thread_info *ti = (struct tt_thread_info *)p;

	assert(ti);

	init_realtime(ti);

	assert(ti->priv);
	assert(ti->priv->pi.handle);
	assert(ti->priv->pi.selectable_fd);
	assert(ti->priv->pi.decoder_cbdata.decoder);

	cbdata = &ti->priv->pi.decoder_cbdata;

	struct timespec deadline;
	struct timespec interval = { .tv_sec = 0, .tv_nsec = 1E6 };

	clock_gettime(CLOCK_MONOTONIC, &deadline);
	init_intervals(ts_to_tv(deadline));

	/*
	 * We need the 1ms tick for the stats update!
	 * The 1ms has to be split between receiving packets and
	 * stats calculations, as follows:
	 *
	 * max pcap_dispatch + tt_get_top5 + mutex contention < 1ms tick
	 *            ~500us +      ~500ms +              ??? < 1ms
	 *
	 * If pcap_dispatch or tt_get_top5 takes too long,
	 * the deadline will be missed and the stats will be wrong.
	 *
	 * 1Gbps Ethernet line rate is 1.5Mpps - 666ns/pkt
	 * Say the time budget for processing packets is
	 * roughly 500ns/pkt (should be less, but unknown and
	 * machine dependent), then to cap the processing to
	 * 500us total is 1000pkts.
	 */
	int cnt, max = 1000;

	while (1) {
		deadline = ts_add(deadline, interval);

		pthread_mutex_lock(&ti->t5_mutex);
		tt_get_top5(ti->t5, ts_to_tv(deadline));
		pthread_mutex_unlock(&ti->t5_mutex);

		cnt = pcap_dispatch(ti->priv->pi.handle, max,
		                    handle_packet, (u_char *)cbdata);
		if (cnt && cbdata->result.err) {
			/* FIXME: think of an elegant way to
			 * get the errors out of this thread. */
			ti->decode_errors++;
		}
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
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
	if (err)
		goto cleanup;

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
