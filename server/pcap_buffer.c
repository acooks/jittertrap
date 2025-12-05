/*
 * pcap_buffer.c - Rolling packet capture buffer implementation
 *
 * Uses a ring buffer with contiguous memory pool for efficient packet storage.
 * Spinlock protects the hot-path packet store, mutex for config/file operations.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <assert.h>

#include "pcap_buffer.h"

/* Internal buffer structure */
struct pcap_buffer {
	/* Entry ring buffer (index array) */
	struct pcap_buf_entry *entries;
	uint32_t entry_capacity;     /* Maximum number of entries */
	uint32_t entry_count;        /* Current number of entries */
	uint32_t write_idx;          /* Next write position */
	uint32_t read_idx;           /* Oldest entry position */

	/* Data pool (contiguous memory for packet data) */
	uint8_t *data_pool;
	uint32_t data_pool_size;
	uint32_t data_write_pos;     /* Next write position in data pool */

	/* Synchronization */
	pthread_spinlock_t fast_lock;   /* For packet store (hot path) */
	pthread_mutex_t mutex;          /* For config/file operations */

	/* State and configuration */
	pcap_buf_state_t state;
	struct pcap_buf_config config;
	struct pcap_buf_stats stats;

	/* Trigger state */
	struct timeval trigger_time;
	char trigger_reason[PCAP_BUF_TRIGGER_REASON_LEN];
	struct timeval post_trigger_deadline;

	/* Initialization flag */
	int initialized;
};

/* Global buffer instance */
static struct pcap_buffer buf = { 0 };

/* Forward declarations */
static void evict_oldest_packet_unlocked(void);
static void expire_old_packets_unlocked(struct timeval now);
static int store_packet_unlocked(const struct pcap_pkthdr *hdr,
                                 const uint8_t *data);
static struct timeval tv_add_sec(struct timeval tv, uint32_t sec);
static int tv_cmp(struct timeval a, struct timeval b);

/*
 * Query available system memory from /proc/meminfo or sysinfo
 */
uint64_t pcap_buf_get_available_memory(void)
{
	struct sysinfo si;
	uint64_t available = 0;
	FILE *f;
	char line[256];

	/* Try /proc/meminfo first for MemAvailable */
	f = fopen("/proc/meminfo", "r");
	if (f) {
		while (fgets(line, sizeof(line), f)) {
			if (strncmp(line, "MemAvailable:", 13) == 0) {
				unsigned long mem_kb;
				if (sscanf(line + 13, "%lu", &mem_kb) == 1) {
					available = mem_kb * 1024ULL;
				}
				break;
			}
		}
		fclose(f);
	}

	/* Fallback to sysinfo if /proc/meminfo didn't work */
	if (available == 0) {
		if (sysinfo(&si) == 0) {
			available = ((uint64_t)si.freeram + si.bufferram) *
			            si.mem_unit;
		}
	}

	return available;
}

/*
 * Calculate optimal buffer size for given parameters
 */
uint32_t pcap_buf_calculate_optimal_size(uint32_t duration_sec,
                                         uint32_t avg_pkt_size,
                                         uint64_t bitrate_bps)
{
	uint64_t bytes_per_sec;
	uint32_t pkts_per_sec;
	uint64_t total_pkts;
	uint32_t mem_per_pkt;
	uint64_t total_mem;

	if (avg_pkt_size == 0)
		avg_pkt_size = 500;

	bytes_per_sec = bitrate_bps / 8;
	pkts_per_sec = (uint32_t)(bytes_per_sec / avg_pkt_size);
	total_pkts = (uint64_t)pkts_per_sec * duration_sec;

	/* Memory per packet = data + entry overhead */
	mem_per_pkt = avg_pkt_size + sizeof(struct pcap_buf_entry);

	/* Total memory needed + 20% overhead */
	total_mem = (total_pkts * mem_per_pkt * 12) / 10;

	/* Cap at uint32_t max */
	if (total_mem > UINT32_MAX)
		return UINT32_MAX;

	return (uint32_t)total_mem;
}

/*
 * Ensure pcap output directory exists
 */
int pcap_buf_ensure_directory(void)
{
	struct stat st;
	int err;

	/* Check if directory exists */
	if (stat(PCAP_BUF_PCAP_DIR, &st) == 0) {
		if (S_ISDIR(st.st_mode))
			return 0;
		/* Exists but not a directory */
		return -1;
	}

	/* Create parent directory first */
	err = mkdir("/tmp/jittertrap", 0755);
	if (err && errno != EEXIST)
		return -1;

	/* Create pcap directory */
	err = mkdir(PCAP_BUF_PCAP_DIR, 0755);
	if (err && errno != EEXIST)
		return -1;

	return 0;
}

/*
 * Initialize the pcap buffer
 */
int pcap_buf_init(struct pcap_buf_config *config)
{
	uint32_t entry_mem;
	uint32_t data_mem;
	uint64_t available;
	uint32_t max_mem;

	if (buf.initialized) {
		/* Already initialized - destroy first */
		pcap_buf_destroy();
	}

	memset(&buf, 0, sizeof(buf));

	/* Initialize locks */
	if (pthread_spin_init(&buf.fast_lock, PTHREAD_PROCESS_PRIVATE) != 0) {
		fprintf(stderr, "pcap_buffer: failed to init spinlock\n");
		return -1;
	}

	if (pthread_mutex_init(&buf.mutex, NULL) != 0) {
		pthread_spin_destroy(&buf.fast_lock);
		fprintf(stderr, "pcap_buffer: failed to init mutex\n");
		return -1;
	}

	/* Set configuration with defaults */
	if (config) {
		buf.config = *config;
	} else {
		/* Use defaults */
		available = pcap_buf_get_available_memory();
		max_mem = (uint32_t)(available / 10); /* 10% of available */

		/* Clamp to reasonable range */
		if (max_mem < PCAP_BUF_MIN_MEM_MB * 1024 * 1024)
			max_mem = PCAP_BUF_MIN_MEM_MB * 1024 * 1024;
		if (max_mem > PCAP_BUF_DEFAULT_MAX_MEM_MB * 1024 * 1024)
			max_mem = PCAP_BUF_DEFAULT_MAX_MEM_MB * 1024 * 1024;

		buf.config.max_memory_bytes = max_mem;
		buf.config.duration_sec = PCAP_BUF_DEFAULT_DURATION_SEC;
		buf.config.pre_trigger_sec = PCAP_BUF_DEFAULT_PRE_TRIGGER;
		buf.config.post_trigger_sec = PCAP_BUF_DEFAULT_POST_TRIGGER;
		buf.config.datalink_type = DLT_EN10MB;
		buf.config.snaplen = BUFSIZ;
	}

	/* Allocate entry array - estimate max packets */
	/* Assume minimum 64 bytes per packet for entry count calculation */
	buf.entry_capacity = buf.config.max_memory_bytes / 64;
	if (buf.entry_capacity < 1000)
		buf.entry_capacity = 1000;

	entry_mem = buf.entry_capacity * sizeof(struct pcap_buf_entry);
	buf.entries = calloc(buf.entry_capacity, sizeof(struct pcap_buf_entry));
	if (!buf.entries) {
		fprintf(stderr, "pcap_buffer: failed to allocate entry array\n");
		goto cleanup;
	}

	/* Allocate data pool - remaining memory after entry array */
	if (entry_mem >= buf.config.max_memory_bytes) {
		fprintf(stderr, "pcap_buffer: max_memory too small\n");
		goto cleanup;
	}

	data_mem = buf.config.max_memory_bytes - entry_mem;
	buf.data_pool = malloc(data_mem);
	if (!buf.data_pool) {
		fprintf(stderr, "pcap_buffer: failed to allocate data pool\n");
		goto cleanup;
	}
	buf.data_pool_size = data_mem;

	/* Ensure output directory exists */
	pcap_buf_ensure_directory();

	buf.state = PCAP_BUF_STATE_DISABLED;
	buf.initialized = 1;

	fprintf(stderr, "pcap_buffer: initialized with %u MB max memory, "
	        "%u entry capacity\n",
	        buf.config.max_memory_bytes / (1024 * 1024),
	        buf.entry_capacity);

	return 0;

cleanup:
	if (buf.entries) {
		free(buf.entries);
		buf.entries = NULL;
	}
	pthread_mutex_destroy(&buf.mutex);
	pthread_spin_destroy(&buf.fast_lock);
	return -1;
}

/*
 * Destroy the pcap buffer
 */
void pcap_buf_destroy(void)
{
	if (!buf.initialized)
		return;

	pthread_mutex_lock(&buf.mutex);
	pthread_spin_lock(&buf.fast_lock);

	if (buf.entries) {
		free(buf.entries);
		buf.entries = NULL;
	}

	if (buf.data_pool) {
		free(buf.data_pool);
		buf.data_pool = NULL;
	}

	buf.initialized = 0;

	pthread_spin_unlock(&buf.fast_lock);
	pthread_mutex_unlock(&buf.mutex);

	pthread_spin_destroy(&buf.fast_lock);
	pthread_mutex_destroy(&buf.mutex);
}

/*
 * Enable packet recording
 */
int pcap_buf_enable(void)
{
	if (!buf.initialized)
		return -1;

	pthread_mutex_lock(&buf.mutex);
	if (buf.state == PCAP_BUF_STATE_DISABLED) {
		buf.state = PCAP_BUF_STATE_RECORDING;
	}
	pthread_mutex_unlock(&buf.mutex);

	return 0;
}

/*
 * Disable packet recording
 */
int pcap_buf_disable(void)
{
	if (!buf.initialized)
		return -1;

	pthread_mutex_lock(&buf.mutex);
	buf.state = PCAP_BUF_STATE_DISABLED;
	pthread_mutex_unlock(&buf.mutex);

	return 0;
}

/*
 * Clear all packets from buffer
 */
void pcap_buf_clear(void)
{
	if (!buf.initialized)
		return;

	pthread_spin_lock(&buf.fast_lock);

	buf.entry_count = 0;
	buf.write_idx = 0;
	buf.read_idx = 0;
	buf.data_write_pos = 0;

	buf.stats.total_packets = 0;
	buf.stats.total_bytes = 0;
	buf.stats.dropped_packets = 0;
	buf.stats.oldest_ts_sec = 0;
	buf.stats.newest_ts_sec = 0;
	buf.stats.current_memory = 0;

	pthread_spin_unlock(&buf.fast_lock);
}

/*
 * Set datalink type
 */
void pcap_buf_set_datalink(int dlt)
{
	pthread_mutex_lock(&buf.mutex);
	buf.config.datalink_type = dlt;
	pthread_mutex_unlock(&buf.mutex);
}

/*
 * Compare two timevals
 * Returns: -1 if a < b, 0 if a == b, 1 if a > b
 */
static int tv_cmp(struct timeval a, struct timeval b)
{
	if (a.tv_sec < b.tv_sec)
		return -1;
	if (a.tv_sec > b.tv_sec)
		return 1;
	if (a.tv_usec < b.tv_usec)
		return -1;
	if (a.tv_usec > b.tv_usec)
		return 1;
	return 0;
}

/*
 * Add seconds to timeval
 */
static struct timeval tv_add_sec(struct timeval tv, uint32_t sec)
{
	tv.tv_sec += sec;
	return tv;
}

/*
 * Subtract seconds from timeval
 */
static struct timeval tv_sub_sec(struct timeval tv, uint32_t sec)
{
	if (tv.tv_sec >= (time_t)sec) {
		tv.tv_sec -= sec;
	} else {
		tv.tv_sec = 0;
		tv.tv_usec = 0;
	}
	return tv;
}

/*
 * Evict the oldest packet from the buffer (unlocked)
 */
static void evict_oldest_packet_unlocked(void)
{
	struct pcap_buf_entry *entry;

	if (buf.entry_count == 0)
		return;

	entry = &buf.entries[buf.read_idx];

	/* Update stats */
	buf.stats.total_packets--;
	buf.stats.total_bytes -= entry->caplen;
	buf.stats.current_memory -= entry->caplen;
	buf.stats.dropped_packets++;

	/* Advance read index */
	buf.read_idx = (buf.read_idx + 1) % buf.entry_capacity;
	buf.entry_count--;

	/* Update oldest timestamp */
	if (buf.entry_count > 0) {
		buf.stats.oldest_ts_sec = buf.entries[buf.read_idx].ts.tv_sec;
	}
}

/*
 * Expire packets older than duration window (unlocked)
 */
static void expire_old_packets_unlocked(struct timeval now)
{
	struct timeval cutoff;
	struct pcap_buf_entry *entry;

	cutoff = tv_sub_sec(now, buf.config.duration_sec);

	while (buf.entry_count > 0) {
		entry = &buf.entries[buf.read_idx];
		if (tv_cmp(entry->ts, cutoff) >= 0)
			break;

		/* This packet is too old, evict it */
		buf.stats.total_packets--;
		buf.stats.total_bytes -= entry->caplen;
		buf.stats.current_memory -= entry->caplen;

		buf.read_idx = (buf.read_idx + 1) % buf.entry_capacity;
		buf.entry_count--;
	}

	/* Update oldest timestamp */
	if (buf.entry_count > 0) {
		buf.stats.oldest_ts_sec = buf.entries[buf.read_idx].ts.tv_sec;
	}
}

/*
 * Store a packet in the buffer (unlocked)
 */
static int store_packet_unlocked(const struct pcap_pkthdr *hdr,
                                 const uint8_t *data)
{
	struct pcap_buf_entry *entry;
	uint32_t required_space;
	uint32_t available_space;

	/* Check if we have entry capacity */
	if (buf.entry_count >= buf.entry_capacity) {
		/* Evict oldest to make room */
		evict_oldest_packet_unlocked();
	}

	/* Calculate required data space */
	required_space = hdr->caplen;

	/* Check data pool space - use simple linear allocation
	 * When write_pos wraps or runs out of space, evict old packets */
	available_space = buf.data_pool_size - buf.data_write_pos;

	while (available_space < required_space && buf.entry_count > 0) {
		/* Not enough contiguous space, evict oldest packets */
		evict_oldest_packet_unlocked();

		/* If we've evicted all packets, reset write position */
		if (buf.entry_count == 0) {
			buf.data_write_pos = 0;
			available_space = buf.data_pool_size;
		} else {
			/* Recalculate available space based on oldest packet */
			/* For simplicity, just wrap around */
			buf.data_write_pos = 0;
			available_space = buf.data_pool_size;
		}
	}

	if (required_space > buf.data_pool_size) {
		/* Packet too large for data pool */
		buf.stats.dropped_packets++;
		return -1;
	}

	/* Store the packet */
	entry = &buf.entries[buf.write_idx];
	entry->ts = hdr->ts;
	entry->caplen = hdr->caplen;
	entry->len = hdr->len;
	entry->data_offset = buf.data_write_pos;

	memcpy(buf.data_pool + buf.data_write_pos, data, hdr->caplen);

	/* Update positions and counts */
	buf.data_write_pos += hdr->caplen;
	buf.write_idx = (buf.write_idx + 1) % buf.entry_capacity;
	buf.entry_count++;

	/* Update stats */
	buf.stats.total_packets++;
	buf.stats.total_bytes += hdr->caplen;
	buf.stats.current_memory += hdr->caplen;
	buf.stats.newest_ts_sec = hdr->ts.tv_sec;

	if (buf.stats.oldest_ts_sec == 0) {
		buf.stats.oldest_ts_sec = hdr->ts.tv_sec;
	}

	return 0;
}

/*
 * Store a packet in the ring buffer (public API)
 * This is the hot path - uses spinlock for minimal latency
 */
int pcap_buf_store_packet(const struct pcap_pkthdr *hdr,
                          const uint8_t *data)
{
	int result = 0;
	struct timeval now;

	if (!buf.initialized)
		return -1;

	/* Reject NULL pointers */
	if (!hdr || (!data && hdr->caplen > 0))
		return -1;

	pthread_spin_lock(&buf.fast_lock);

	/* Check state */
	if (buf.state != PCAP_BUF_STATE_RECORDING &&
	    buf.state != PCAP_BUF_STATE_TRIGGERED) {
		pthread_spin_unlock(&buf.fast_lock);
		return 0;
	}

	/* Get current time for expiration */
	now = hdr->ts;

	/* Expire old packets based on duration */
	expire_old_packets_unlocked(now);

	/* Evict packets if memory limit reached */
	while (buf.stats.current_memory + hdr->caplen >
	       buf.config.max_memory_bytes && buf.entry_count > 0) {
		evict_oldest_packet_unlocked();
	}

	/* Store the packet */
	result = store_packet_unlocked(hdr, data);

	/* Check if post-trigger recording is complete */
	if (buf.state == PCAP_BUF_STATE_TRIGGERED &&
	    buf.config.post_trigger_sec > 0) {
		if (tv_cmp(now, buf.post_trigger_deadline) >= 0) {
			/* Post-trigger period complete - stay triggered
			 * until file is written */
		}
	}

	/* Update buffer fullness percentage */
	if (buf.stats.newest_ts_sec > 0 && buf.stats.oldest_ts_sec > 0) {
		uint32_t actual_duration = buf.stats.newest_ts_sec -
		                           buf.stats.oldest_ts_sec;
		buf.stats.buffer_percent = (actual_duration * 100) /
		                           buf.config.duration_sec;
		if (buf.stats.buffer_percent > 100)
			buf.stats.buffer_percent = 100;
	}

	pthread_spin_unlock(&buf.fast_lock);

	return result;
}

/*
 * Trigger a capture
 */
int pcap_buf_trigger(const char *trigger_reason)
{
	if (!buf.initialized)
		return -1;

	pthread_mutex_lock(&buf.mutex);

	if (buf.state != PCAP_BUF_STATE_RECORDING) {
		pthread_mutex_unlock(&buf.mutex);
		return -1;
	}

	gettimeofday(&buf.trigger_time, NULL);
	buf.post_trigger_deadline = tv_add_sec(buf.trigger_time,
	                                       buf.config.post_trigger_sec);

	if (trigger_reason) {
		snprintf(buf.trigger_reason, sizeof(buf.trigger_reason),
		         "%s", trigger_reason);
	} else {
		snprintf(buf.trigger_reason, sizeof(buf.trigger_reason),
		         "Manual trigger");
	}

	buf.state = PCAP_BUF_STATE_TRIGGERED;

	pthread_mutex_unlock(&buf.mutex);

	fprintf(stderr, "pcap_buffer: triggered - %s\n", buf.trigger_reason);

	return 0;
}

/*
 * Check if post-trigger recording is complete
 */
int pcap_buf_post_trigger_complete(void)
{
	struct timeval now;
	int complete = 0;

	if (!buf.initialized)
		return 1;

	pthread_spin_lock(&buf.fast_lock);

	if (buf.state != PCAP_BUF_STATE_TRIGGERED) {
		pthread_spin_unlock(&buf.fast_lock);
		return 1;
	}

	gettimeofday(&now, NULL);

	if (buf.config.post_trigger_sec == 0 ||
	    tv_cmp(now, buf.post_trigger_deadline) >= 0) {
		complete = 1;
	}

	pthread_spin_unlock(&buf.fast_lock);

	return complete;
}

/*
 * Write buffered packets to a pcap file
 */
int pcap_buf_write_file(struct pcap_buf_trigger_result *result)
{
	pcap_t *pd;
	pcap_dumper_t *dumper;
	struct timeval start_time, end_time;
	uint32_t idx, count;
	uint32_t packets_written = 0;
	struct stat st;

	if (!buf.initialized || !result)
		return -1;

	memset(result, 0, sizeof(*result));

	pthread_mutex_lock(&buf.mutex);

	if (buf.state != PCAP_BUF_STATE_TRIGGERED) {
		pthread_mutex_unlock(&buf.mutex);
		return -1;
	}

	buf.state = PCAP_BUF_STATE_WRITING;

	/* Calculate time window */
	start_time = tv_sub_sec(buf.trigger_time, buf.config.pre_trigger_sec);
	end_time = tv_add_sec(buf.trigger_time, buf.config.post_trigger_sec);

	/* Generate filename */
	snprintf(result->filepath, sizeof(result->filepath),
	         "%s/capture_%ld.pcap", PCAP_BUF_PCAP_DIR,
	         (long)buf.trigger_time.tv_sec);

	/* Open pcap for writing */
	pd = pcap_open_dead(buf.config.datalink_type, buf.config.snaplen);
	if (!pd) {
		fprintf(stderr, "pcap_buffer: pcap_open_dead failed\n");
		buf.state = PCAP_BUF_STATE_RECORDING;
		pthread_mutex_unlock(&buf.mutex);
		return -1;
	}

	dumper = pcap_dump_open(pd, result->filepath);
	if (!dumper) {
		fprintf(stderr, "pcap_buffer: pcap_dump_open failed: %s\n",
		        pcap_geterr(pd));
		pcap_close(pd);
		buf.state = PCAP_BUF_STATE_RECORDING;
		pthread_mutex_unlock(&buf.mutex);
		return -1;
	}

	/* Lock spinlock to safely iterate buffer */
	pthread_spin_lock(&buf.fast_lock);

	/* Iterate through ring buffer */
	idx = buf.read_idx;
	count = buf.entry_count;

	for (uint32_t i = 0; i < count; i++) {
		struct pcap_buf_entry *entry = &buf.entries[idx];

		/* Check if packet is within time window */
		if (tv_cmp(entry->ts, start_time) >= 0 &&
		    tv_cmp(entry->ts, end_time) <= 0) {
			struct pcap_pkthdr hdr = {
				.ts = entry->ts,
				.caplen = entry->caplen,
				.len = entry->len
			};

			pcap_dump((u_char *)dumper, &hdr,
			          buf.data_pool + entry->data_offset);
			packets_written++;
		}

		idx = (idx + 1) % buf.entry_capacity;
	}

	pthread_spin_unlock(&buf.fast_lock);

	/* Close pcap file */
	pcap_dump_close(dumper);
	pcap_close(pd);

	/* Get file size */
	if (stat(result->filepath, &st) == 0) {
		result->file_size = st.st_size;
	}

	result->packet_count = packets_written;
	result->duration_sec = buf.config.pre_trigger_sec +
	                       buf.config.post_trigger_sec;
	result->success = 1;

	fprintf(stderr, "pcap_buffer: wrote %u packets to %s (%lu bytes)\n",
	        packets_written, result->filepath,
	        (unsigned long)result->file_size);

	/* Return to recording state */
	buf.state = PCAP_BUF_STATE_RECORDING;

	pthread_mutex_unlock(&buf.mutex);

	return 0;
}

/*
 * Update buffer configuration
 */
int pcap_buf_set_config(struct pcap_buf_config *config)
{
	if (!buf.initialized || !config)
		return -1;

	pthread_mutex_lock(&buf.mutex);

	/* Update configuration - note: max_memory change requires reinit */
	buf.config.duration_sec = config->duration_sec;
	buf.config.pre_trigger_sec = config->pre_trigger_sec;
	buf.config.post_trigger_sec = config->post_trigger_sec;

	if (config->datalink_type != 0) {
		buf.config.datalink_type = config->datalink_type;
	}

	if (config->snaplen != 0) {
		buf.config.snaplen = config->snaplen;
	}

	/* If max_memory changed significantly, reinitialize */
	if (config->max_memory_bytes != 0 &&
	    config->max_memory_bytes != buf.config.max_memory_bytes) {
		struct pcap_buf_config new_config = buf.config;
		new_config.max_memory_bytes = config->max_memory_bytes;
		pthread_mutex_unlock(&buf.mutex);

		/* Reinitialize with new memory size */
		pcap_buf_destroy();
		return pcap_buf_init(&new_config);
	}

	pthread_mutex_unlock(&buf.mutex);

	return 0;
}

/*
 * Get current configuration
 */
int pcap_buf_get_config(struct pcap_buf_config *config)
{
	if (!buf.initialized || !config)
		return -1;

	pthread_mutex_lock(&buf.mutex);
	*config = buf.config;
	pthread_mutex_unlock(&buf.mutex);

	return 0;
}

/*
 * Get current statistics
 */
int pcap_buf_get_stats(struct pcap_buf_stats *stats)
{
	if (!buf.initialized || !stats)
		return -1;

	pthread_spin_lock(&buf.fast_lock);
	*stats = buf.stats;
	stats->state = buf.state;
	pthread_spin_unlock(&buf.fast_lock);

	return 0;
}

/*
 * Get current state
 */
pcap_buf_state_t pcap_buf_get_state(void)
{
	pcap_buf_state_t state;

	if (!buf.initialized)
		return PCAP_BUF_STATE_DISABLED;

	pthread_spin_lock(&buf.fast_lock);
	state = buf.state;
	pthread_spin_unlock(&buf.fast_lock);

	return state;
}
