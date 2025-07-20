#include <net/ethernet.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sched.h>
#include <errno.h>
#include <pthread.h>
#include <syslog.h>

#include <jansson.h>

#include "jt_message_types.h"
#include "jt_messages.h"

#include "mq_msg_tt.h"

#include "flow.h"
#include "intervals.h"

#include "tt_thread.h"

struct tt_thread_info ti = {
	0,
	.thread_name = "jt-toptalk",
	.thread_prio = 3
};

struct {
	pthread_t thread_id;
	pthread_attr_t thread_attr;
	const char * const thread_name;
	const int thread_prio;
} iti = {
	0,
	.thread_name = "jt-intervals",
	.thread_prio = 2
};

static char const *const protos[IPPROTO_MAX] = {
	[IPPROTO_TCP] = "TCP",   [IPPROTO_UDP] = "UDP",
	[IPPROTO_ICMP] = "ICMP", [IPPROTO_ICMPV6] = "ICMP6",
	[IPPROTO_IP] = "IP",     [IPPROTO_IGMP] = "IGMP",
	[IPPROTO_ESP] = "ESP"
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

int tt_thread_restart(char * iface)
{
	int err;
	void *res;

	if (ti.thread_id) {
		pthread_cancel(ti.thread_id);
		pthread_join(ti.thread_id, &res);
		free(ti.t5);
		free(ti.dev);
	}

	ti.dev = malloc(MAX_IFACE_LEN);
	snprintf(ti.dev, MAX_IFACE_LEN, "%s", iface);

	/* start & run thread for capture and interval processing */
	tt_intervals_init(&ti);

	err = pthread_attr_init(&ti.attr);
	assert(!err);

	err = pthread_create(&ti.thread_id, &ti.attr, tt_intervals_run, &ti);
	assert(!err);
        pthread_setname_np(ti.thread_id, ti.thread_name);

	tt_update_ref_window_size(&ti, tt_intervals[0]);
	tt_update_ref_window_size(&ti, tt_intervals[INTERVAL_COUNT - 1]);

	return 0;
}

/* Convert from a struct tt_top_flows to a struct mq_tt_msg */
static int
m2m(struct tt_top_flows *ttf, struct mq_tt_msg *msg, int interval)
{
	struct jt_msg_toptalk *m = &msg->m;

	m->timestamp.tv_sec = ttf->timestamp.tv_sec;
	m->timestamp.tv_nsec = ttf->timestamp.tv_usec * 1000;

	m->interval_ns = tt_intervals[interval].tv_sec * 1E9
		+ tt_intervals[interval].tv_usec * 1E3;

	m->tflows = ttf->flow_count;
	m->tbytes = ttf->total_bytes;
	m->tpackets = ttf->total_packets;

	for (int f = 0; f < MAX_FLOWS; f++) {
		m->flows[f].bytes = ttf->flow[f][interval].bytes;
		m->flows[f].packets = ttf->flow[f][interval].packets;
		m->flows[f].sport = ttf->flow[f][interval].flow.sport;
		m->flows[f].dport = ttf->flow[f][interval].flow.dport;
		snprintf(m->flows[f].proto, PROTO_LEN, "%s",
				protos[ttf->flow[f][interval].flow.proto]);
		snprintf(m->flows[f].src, ADDR_LEN, "%s",
				inet_ntoa(ttf->flow[f][interval].flow.src_ip));
		snprintf(m->flows[f].dst, ADDR_LEN, "%s",
				inet_ntoa(ttf->flow[f][interval].flow.dst_ip));
		snprintf(m->flows[f].tclass, TCLASS_LEN, "%s",
		         dscpvalues[ttf->flow[f][interval].flow.tclass]);
	}
	return 0;
}

inline static int message_producer(struct mq_tt_msg *m, void *data)
{

	memcpy(m, (struct mq_tt_msg *)data, sizeof(struct mq_tt_msg));
	return 0;
}

int queue_tt_msg(int interval)
{
	struct mq_tt_msg msg;
	struct tt_top_flows *t5 = ti.t5;
	int cb_err;

	pthread_mutex_lock(&ti.t5_mutex);
	{
		m2m(t5, &msg, interval);
		mq_tt_produce(message_producer, &msg, &cb_err);
	}
	pthread_mutex_unlock(&ti.t5_mutex);
	return 0;
}


/* TODO: calculate the GCD of tt_intervals
 * updates output var intervals
 * returns GCD nanoseconds*/
static uint32_t calc_intervals(uint32_t intervals[INTERVAL_COUNT])
{
	uint64_t t0_us = tt_intervals[0].tv_sec * 1E6 + tt_intervals[0].tv_usec;

	for (int i = INTERVAL_COUNT - 1; i >= 0; i--) {
		uint64_t t_us = tt_intervals[i].tv_sec * 1E6
		                + tt_intervals[i].tv_usec;
		intervals[i] = t_us / t0_us;

		/* FIXME: for now, t0_us is the GCD of tt_intervals */
		assert(0 == t_us % t0_us);
	}
	return 1E3 * tt_intervals[0].tv_usec + 1E9 * tt_intervals[0].tv_sec;
}

static void set_affinity(void)
{
	int s, j;
	cpu_set_t cpuset;
	pthread_t thread;
	thread = pthread_self();
	CPU_ZERO(&cpuset);
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

	char buff[64] = {0};
	char *offset = buff;
	int blen = sizeof(buff);
	for (j = 0; j < CPU_SETSIZE; j++) {
		if (CPU_ISSET(j, &cpuset)) {
			snprintf(offset, blen, "CPU%d ", j);
			blen -= strlen(offset);
			offset += strlen(offset);
		}
	}

	syslog(LOG_DEBUG, "[RT thread %s] priority [%d] CPU affinity: %s",
		iti.thread_name, iti.thread_prio, buff);
}

static int init_realtime(void)
{
	struct sched_param schedparm;
	memset(&schedparm, 0, sizeof(schedparm));
	schedparm.sched_priority = iti.thread_prio;
	sched_setscheduler(0, SCHED_FIFO, &schedparm);
	set_affinity();
	return 0;
}

static void *intervals_run(void *data)
{
	(void)data; /* unused */
	struct timespec deadline;

	uint32_t tick = 0;
	/* integer multiple of gcd in interval */
	uint32_t imuls[INTERVAL_COUNT];
	uint32_t sleep_time_ns = calc_intervals(imuls);

	init_realtime();

	clock_gettime(CLOCK_MONOTONIC, &deadline);

	for (;;) {

		for (int i = 0; i < INTERVAL_COUNT; i++) {
			assert(imuls[i]);
			if (0 == (tick % imuls[i])) {
				queue_tt_msg(i);
			}
		}

		/* increment / wrap tick */
		tick = (imuls[INTERVAL_COUNT-1] == tick) ? 1 : tick + 1;

		deadline.tv_nsec += sleep_time_ns;

		/* Second boundary */
		if (deadline.tv_nsec >= 1E9) {
			deadline.tv_nsec -= 1E9;
			deadline.tv_sec++;
		}

		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline,
		                NULL);
	}
	return NULL;
}

int intervals_thread_init(void)
{
	int err;
	void *res;

	if (iti.thread_id) {
		pthread_cancel(iti.thread_id);
		pthread_join(iti.thread_id, &res);
	}

	err = pthread_attr_init(&iti.thread_attr);
	assert(!err);

	err = pthread_create(&iti.thread_id, &iti.thread_attr, intervals_run,
	                     NULL);
	assert(!err);
	pthread_setname_np(iti.thread_id, iti.thread_name);

	return 0;
}

