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

#include "intervals_user.h"
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
	unsigned int bytes;
	unsigned int packets;
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

static int has_aged(struct flow_pkt *new_pkt, struct flow_pkt *old_pkt)
{
	struct timeval diff;

	diff = tv_absdiff(new_pkt->timestamp, old_pkt->timestamp);

	return (0 < tv_cmp(diff, ref_window_size));
}

static void update_sliding_window_flow_ref(struct flow_pkt *pkt)
{
	struct flow_hash *fte;
	struct flow_pkt_list *ple, *tmp, *iter;

	/* keep a list of packets, used for sliding window byte counts */
	ple = malloc(sizeof(struct flow_pkt_list));
	ple->pkt = *pkt;
	DL_APPEND(pkt_list_ref_head, ple);

	/* expire packets where time diff between current (ple) and prev (iter)
	 * is more than max_age */
	DL_FOREACH_SAFE(pkt_list_ref_head, iter, tmp)
	{
		if (has_aged(&(ple->pkt), &(iter->pkt))) {
			HASH_FIND(r_hh, flow_ref_table,
			          &(iter->pkt.flow_rec.flow),
			          sizeof(struct flow), fte);
			assert(fte);
			fte->f.bytes -= iter->pkt.flow_rec.bytes;
			fte->f.packets -= iter->pkt.flow_rec.packets;

			totals.bytes -= iter->pkt.flow_rec.bytes;
			totals.packets -= iter->pkt.flow_rec.packets;

			if (0 == fte->f.bytes) {
				HASH_DELETE(r_hh, flow_ref_table, fte);
			}

			DL_DELETE(pkt_list_ref_head, iter);
			free(iter);
		} else {
			break;
		}
	}

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
	}

	totals.bytes += pkt->flow_rec.bytes;
	totals.packets += pkt->flow_rec.packets;
}

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
			continue;
		}

		/* try to find the reference flow in the short flow table */
		HASH_FIND(ts_hh, fti, &(ref_flow->f.flow),
		          sizeof(struct flow), te);

		st_flows[i].bytes = te ? te->f.bytes : 0;

		/* convert to bytes per second */
		st_flows[i].bytes =
		    rate_calc(tt_intervals[i], st_flows[i].bytes);
	}
}

static void update_stats_tables(struct flow_pkt *pkt)
{
	update_sliding_window_flow_ref(pkt);

	for (int i = 0; i < INTERVAL_COUNT; i++) {
		add_flow_to_interval(pkt, i);
	}
	expire_old_interval_tables(pkt->timestamp);
}

void tt_get_top5(struct tt_top_flows *t5)
{
	struct timeval now;
	struct flow_hash *rfti; /* reference flow table iter */

	/* sort the flow reference table */
	HASH_SRT(r_hh, flow_ref_table, bytes_cmp);

	gettimeofday(&now, NULL);
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
}

int tt_get_flow_count()
{
	return HASH_CNT(r_hh, flow_ref_table);
}

void tt_update_ref_window_size(struct timeval t)
{
	ref_window_size = t;
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

	ref_window_size = (struct timeval){.tv_sec = 3, .tv_usec = 0 };
	flow_ref_table = NULL;
	pkt_list_ref_head = NULL;

	ti->t5 = calloc(1, sizeof(struct tt_top_flows));
	if (!ti->t5) { return 1; }

	pthread_mutex_init(&(ti->t5_mutex), NULL);

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

	free_pcap(&(ti->priv->pi));
	free(ti->priv);
	free(ti->t5);
	return 0;
}
