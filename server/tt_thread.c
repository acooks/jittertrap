#include <net/ethernet.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sched.h>
#include <errno.h>
#include <pthread.h>
#include <syslog.h>
#include <stdatomic.h>
#include <string.h>

#include <jansson.h>

#include "jt_message_types.h"
#include "jt_messages.h"

#include "mq_msg_tt.h"

#include "flow.h"
#include "intervals.h"

#include "tt_thread.h"
#include "pcap_buffer.h"
#ifdef ENABLE_WEBRTC_PLAYBACK
#include "webrtc_bridge.h"
#endif

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

int is_valid_dscp(int potential_dscp)
{

	switch (potential_dscp) {
	case IPTOS_DSCP_AF11:
	case IPTOS_DSCP_AF12:
	case IPTOS_DSCP_AF13:
	case IPTOS_DSCP_AF21:
	case IPTOS_DSCP_AF22:
	case IPTOS_DSCP_AF23:
	case IPTOS_DSCP_AF31:
	case IPTOS_DSCP_AF32:
	case IPTOS_DSCP_AF33:
	case IPTOS_DSCP_AF41:
	case IPTOS_DSCP_AF42:
	case IPTOS_DSCP_AF43:
	case IPTOS_DSCP_EF:
	case IPTOS_CLASS_CS0:
	case IPTOS_CLASS_CS1:
	case IPTOS_CLASS_CS2:
	case IPTOS_CLASS_CS3:
	case IPTOS_CLASS_CS4:
	case IPTOS_CLASS_CS5:
	case IPTOS_CLASS_CS6:
	case IPTOS_CLASS_CS7:
		return 1;
	default:
		return 0;
	}
}

int is_valid_proto(int potential_proto)
{
	switch (potential_proto) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_ICMP:
	case IPPROTO_ICMPV6:
	case IPPROTO_IP:
	case IPPROTO_IGMP:
	case IPPROTO_ESP:
		return 1;
	default:
		return 0;
	}
}

/* Callback wrapper for pcap buffer packet storage */
static void pcap_store_cb(const struct pcap_pkthdr *hdr, const uint8_t *data)
{
	pcap_buf_store_packet(hdr, data);
	/* Video playback now uses its own pcap handle with BPF filter */
}

/* Callback for interface/datalink changes */
static void pcap_iface_cb(int dlt)
{
	pcap_buf_set_datalink(dlt);
	pcap_buf_clear();
}

#ifdef ENABLE_WEBRTC_PLAYBACK
/* Callback wrapper for WebRTC video forwarding */
static void rtp_forward_cb(const struct flow *f, const uint8_t *rtp_data, size_t rtp_len)
{
	/* Extract SSRC from RTP header (bytes 8-11, big-endian) */
	if (rtp_len >= 12) {
		uint32_t ssrc = ((uint32_t)rtp_data[8] << 24) |
		                ((uint32_t)rtp_data[9] << 16) |
		                ((uint32_t)rtp_data[10] << 8) |
		                ((uint32_t)rtp_data[11]);
		webrtc_bridge_forward_rtp(rtp_data, rtp_len, f, ssrc);
	}
}
#endif

int tt_thread_restart(char *iface)
{
	int err;
	void *res;
	static int callbacks_registered = 0;

	/* Register callbacks once */
	if (!callbacks_registered) {
		tt_set_pcap_callback(pcap_store_cb, pcap_iface_cb);
#ifdef ENABLE_WEBRTC_PLAYBACK
		tt_set_rtp_forward_callback(rtp_forward_cb);
#endif
		callbacks_registered = 1;
	}

	if (ti.thread_id) {
		pthread_cancel(ti.thread_id);
		pthread_join(ti.thread_id, &res);

		/* After join, writer thread is dead. Reset the atomic pointer
		 * so readers see NULL until new data is published.
		 * Buffers are static in ti, so no free needed.
		 */
		atomic_store(&ti.t5, NULL);

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

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

/* Convert from a struct tt_top_flows to a struct mq_tt_msg */
static int m2m(struct tt_top_flows *ttf, struct mq_tt_msg *msg, int interval)
{
	struct jt_msg_toptalk *m = &msg->m;
	char s_addr_str[INET6_ADDRSTRLEN] = { 0 };
	char d_addr_str[INET6_ADDRSTRLEN] = { 0 };

	// Determine the number of flows to send, capped by the message capacity.
	// This relies on the compile-time MAX_FLOW_COUNT being >= MAX_FLOWS.
	int flow_count = MIN(ttf->flow_count, MAX_FLOWS);

	m->timestamp.tv_sec = ttf->timestamp.tv_sec;
	m->timestamp.tv_nsec = ttf->timestamp.tv_usec * 1000;

	m->interval_ns = tt_intervals[interval].tv_sec * 1E9 +
	                 tt_intervals[interval].tv_usec * 1E3;

	m->tflows = flow_count;
	m->tbytes = ttf->total_bytes;
	m->tpackets = ttf->total_packets;

	for (int f = 0; f < flow_count; f++) {
		struct flow_record *fr = &ttf->flow[f][interval];

		m->flows[f].bytes = fr->bytes;
		m->flows[f].packets = fr->packets;
		m->flows[f].sport = fr->flow.sport;
		m->flows[f].dport = fr->flow.dport;

		/*
		 * Use cached RTT info from flow_record.
		 * This data was populated by fill_short_int_flows() in the
		 * writer thread, eliminating race conditions with hash table
		 * access from this reader thread.
		 */
		m->flows[f].rtt_us = fr->rtt.rtt_us;
		m->flows[f].tcp_state = fr->rtt.tcp_state;
		m->flows[f].saw_syn = fr->rtt.saw_syn;

		/* Use cached window info from flow_record */
		m->flows[f].rwnd_bytes = fr->window.rwnd_bytes;
		m->flows[f].window_scale = fr->window.window_scale;
		m->flows[f].zero_window_cnt = fr->window.zero_window_cnt;
		m->flows[f].dup_ack_cnt = fr->window.dup_ack_cnt;
		m->flows[f].retransmit_cnt = fr->window.retransmit_cnt;
		m->flows[f].ece_cnt = fr->window.ece_cnt;
		m->flows[f].recent_events = fr->window.recent_events;

		/* Use cached health info from flow_record */
		memcpy(m->flows[f].health_rtt_hist, fr->health.rtt_hist,
		       sizeof(m->flows[f].health_rtt_hist));
		m->flows[f].health_rtt_samples = fr->health.rtt_samples;
		m->flows[f].health_status = fr->health.health_status;
		m->flows[f].health_flags = fr->health.health_flags;

		/* Use cached IPG histogram from flow_record (all flows) */
		memcpy(m->flows[f].ipg_hist, fr->ipg.ipg_hist,
		       sizeof(m->flows[f].ipg_hist));
		m->flows[f].ipg_samples = fr->ipg.ipg_samples;
		m->flows[f].ipg_mean_us = fr->ipg.ipg_mean_us;

		/* Use cached frame size histogram from flow_record */
		memcpy(m->flows[f].frame_size_hist, fr->pkt_size.frame_hist,
		       sizeof(m->flows[f].frame_size_hist));
		m->flows[f].frame_size_samples = fr->pkt_size.frame_samples;
		m->flows[f].frame_size_min = fr->pkt_size.frame_min;
		m->flows[f].frame_size_max = fr->pkt_size.frame_max;
		if (fr->pkt_size.frame_samples > 0) {
			uint32_t mean = fr->pkt_size.frame_sum / fr->pkt_size.frame_samples;
			m->flows[f].frame_size_mean = mean;
			/* Variance = E[X^2] - E[X]^2 */
			uint64_t mean_sq = fr->pkt_size.frame_sum_sq / fr->pkt_size.frame_samples;
			m->flows[f].frame_size_variance = (uint32_t)(mean_sq - (uint64_t)mean * mean);
		} else {
			m->flows[f].frame_size_mean = 0;
			m->flows[f].frame_size_variance = 0;
		}

		/* Use cached PPS histogram from flow_record */
		memcpy(m->flows[f].pps_hist, fr->pps.pps_hist,
		       sizeof(m->flows[f].pps_hist));
		m->flows[f].pps_samples = fr->pps.pps_samples;
		if (fr->pps.pps_samples > 0) {
			uint32_t mean = fr->pps.pps_sum / fr->pps.pps_samples;
			m->flows[f].pps_mean = mean;
			/* Variance = E[X^2] - E[X]^2 */
			uint64_t mean_sq = fr->pps.pps_sum_sq / fr->pps.pps_samples;
			m->flows[f].pps_variance = (uint32_t)(mean_sq - (uint64_t)mean * mean);
		} else {
			m->flows[f].pps_mean = 0;
			m->flows[f].pps_variance = 0;
		}

		/* Use cached video stream info from flow_record
		 * Only set video_type if there's an actual video codec detected,
		 * not just for any RTP stream (audio-only streams should not
		 * have video_type set).
		 */
		if (fr->video.stream_type == 1) { /* VIDEO_STREAM_RTP */
			/* Only set video_type if there's a video codec, not audio-only */
			m->flows[f].video_type = (fr->video.rtp.codec != 0) ? 1 : 0;
			m->flows[f].video_codec = fr->video.rtp.codec;
			m->flows[f].video_jitter_us = fr->video.rtp.jitter_us;
			memcpy(m->flows[f].video_jitter_hist, fr->video.rtp.jitter_hist,
			       sizeof(m->flows[f].video_jitter_hist));
			m->flows[f].video_seq_loss = fr->video.rtp.seq_loss;
			m->flows[f].video_ssrc = fr->video.rtp.ssrc;
			m->flows[f].video_cc_errors = 0;
			/* Extended telemetry fields */
			m->flows[f].video_codec_source = fr->video.rtp.codec_source;
			m->flows[f].video_width = fr->video.rtp.width;
			m->flows[f].video_height = fr->video.rtp.height;
			m->flows[f].video_profile = fr->video.rtp.profile_idc;
			m->flows[f].video_level = fr->video.rtp.level_idc;
			m->flows[f].video_fps_x100 = fr->video.rtp.fps_x100;
			m->flows[f].video_bitrate_kbps = fr->video.rtp.bitrate_kbps;
			m->flows[f].video_gop_frames = fr->video.rtp.gop_frames;
			m->flows[f].video_keyframes = fr->video.rtp.keyframe_count;
			m->flows[f].video_frames = fr->video.rtp.frame_count;
		} else if (fr->video.stream_type == 2) { /* VIDEO_STREAM_MPEG_TS */
			m->flows[f].video_type = 2;  /* MPEG-TS always has video */
			m->flows[f].video_codec = fr->video.mpegts.codec;
			m->flows[f].video_cc_errors = fr->video.mpegts.cc_errors;
			m->flows[f].video_jitter_us = 0;
			memset(m->flows[f].video_jitter_hist, 0,
			       sizeof(m->flows[f].video_jitter_hist));
			m->flows[f].video_seq_loss = 0;
			m->flows[f].video_ssrc = 0;
			/* Clear extended fields for MPEG-TS (not yet implemented) */
			m->flows[f].video_codec_source = 0;
			m->flows[f].video_width = 0;
			m->flows[f].video_height = 0;
			m->flows[f].video_profile = 0;
			m->flows[f].video_level = 0;
			m->flows[f].video_fps_x100 = 0;
			m->flows[f].video_bitrate_kbps = 0;
			m->flows[f].video_gop_frames = 0;
			m->flows[f].video_keyframes = 0;
			m->flows[f].video_frames = 0;
		} else {
			m->flows[f].video_type = 0;  /* Not a video stream */
			m->flows[f].video_codec = 0;
			m->flows[f].video_jitter_us = 0;
			memset(m->flows[f].video_jitter_hist, 0,
			       sizeof(m->flows[f].video_jitter_hist));
			m->flows[f].video_seq_loss = 0;
			m->flows[f].video_cc_errors = 0;
			m->flows[f].video_ssrc = 0;
			/* Clear extended fields */
			m->flows[f].video_codec_source = 0;
			m->flows[f].video_width = 0;
			m->flows[f].video_height = 0;
			m->flows[f].video_profile = 0;
			m->flows[f].video_level = 0;
			m->flows[f].video_fps_x100 = 0;
			m->flows[f].video_bitrate_kbps = 0;
			m->flows[f].video_gop_frames = 0;
			m->flows[f].video_keyframes = 0;
			m->flows[f].video_frames = 0;
		}

		/* Copy cached audio stream info from flow_record */
		if (fr->video.rtp.audio_codec != 0) {
			m->flows[f].audio_type = 1;  /* JT_AUDIO_TYPE_RTP */
			m->flows[f].audio_codec = fr->video.rtp.audio_codec;
			m->flows[f].audio_sample_rate = fr->video.rtp.sample_rate_khz;
			m->flows[f].audio_jitter_us = fr->video.rtp.jitter_us;
			m->flows[f].audio_seq_loss = fr->video.rtp.seq_loss;
			m->flows[f].audio_ssrc = fr->video.rtp.ssrc;
			m->flows[f].audio_bitrate_kbps = fr->video.rtp.bitrate_kbps;
		} else {
			m->flows[f].audio_type = 0;
			m->flows[f].audio_codec = 0;
			m->flows[f].audio_sample_rate = 0;
			m->flows[f].audio_jitter_us = 0;
			m->flows[f].audio_seq_loss = 0;
			m->flows[f].audio_ssrc = 0;
			m->flows[f].audio_bitrate_kbps = 0;
		}

		if (is_valid_proto(fr->flow.proto)) {
			snprintf(m->flows[f].proto, PROTO_LEN, "%s",
			         protos[fr->flow.proto]);
		} else {
			snprintf(m->flows[f].proto, PROTO_LEN, "%s", "__");
		}
		if (fr->flow.ethertype == ETHERTYPE_IP) {
			inet_ntop(AF_INET, &(fr->flow.src_ip),
			          s_addr_str, INET6_ADDRSTRLEN);
			inet_ntop(AF_INET, &(fr->flow.dst_ip),
			          d_addr_str, INET6_ADDRSTRLEN);
		} else if (fr->flow.ethertype == ETHERTYPE_IPV6) {
			inet_ntop(AF_INET6, &(fr->flow.src_ip6),
			          s_addr_str, INET6_ADDRSTRLEN);
			inet_ntop(AF_INET6, &(fr->flow.dst_ip6),
			          d_addr_str, INET6_ADDRSTRLEN);
		} else {
			snprintf(s_addr_str, INET6_ADDRSTRLEN,
				 "Ethertype 0x%x\n", fr->flow.ethertype);
		}
		snprintf(m->flows[f].src, ADDR_LEN, "%s", s_addr_str);
		snprintf(m->flows[f].dst, ADDR_LEN, "%s", d_addr_str);

		if (is_valid_dscp(fr->flow.tclass)) {
			snprintf(m->flows[f].tclass, TCLASS_LEN, "%s",
			         dscpvalues[fr->flow.tclass]);
		} else {
			snprintf(m->flows[f].tclass, TCLASS_LEN, "%s", "__");
		}
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
	int cb_err;

	/* Lock-free read: atomically load the published buffer pointer.
	 * The writer publishes a fully-populated buffer before updating the
	 * pointer, so we always read consistent data (at most 1 tick stale).
	 */
	struct tt_top_flows *t5 = atomic_load_explicit(&ti.t5,
	                                               memory_order_acquire);
	if (t5) {
		m2m(t5, &msg, interval);
		mq_tt_produce(message_producer, &msg, &cb_err);
	}
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
	int ret;

	memset(&schedparm, 0, sizeof(schedparm));
	schedparm.sched_priority = iti.thread_prio;
	ret = sched_setscheduler(0, SCHED_FIFO, &schedparm);
	if (ret != 0) {
		syslog(LOG_WARNING,
		       "[%s] Failed to set SCHED_FIFO priority %d: %s. "
		       "Running without real-time scheduling. "
		       "Grant CAP_SYS_NICE for RT priority.",
		       iti.thread_name, iti.thread_prio,
		       strerror(errno));
	}
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
