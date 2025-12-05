/*
 * pcap_buffer.h - Rolling packet capture buffer with trigger-based download
 *
 * Provides a memory-efficient ring buffer for storing raw packets with
 * configurable time window and memory limits. Supports manual and
 * threshold-based triggers for capturing packets to pcap files.
 */

#ifndef PCAP_BUFFER_H
#define PCAP_BUFFER_H

#include <stdint.h>
#include <sys/time.h>
#include <pcap/pcap.h>
#include <pthread.h>

/* Configuration constants */
#define PCAP_BUF_DEFAULT_DURATION_SEC   30
#define PCAP_BUF_DEFAULT_PRE_TRIGGER    25
#define PCAP_BUF_DEFAULT_POST_TRIGGER   5
#define PCAP_BUF_DEFAULT_MAX_MEM_MB     256
#define PCAP_BUF_MIN_MEM_MB             16
#define PCAP_BUF_MAX_MEM_MB             2048
#define PCAP_BUF_TRIGGER_REASON_LEN     256
#define PCAP_BUF_FILEPATH_LEN           256
#define PCAP_BUF_PCAP_DIR               "/tmp/jittertrap/pcap"

/* Buffer states */
typedef enum {
	PCAP_BUF_STATE_DISABLED = 0,
	PCAP_BUF_STATE_RECORDING,
	PCAP_BUF_STATE_TRIGGERED,
	PCAP_BUF_STATE_WRITING
} pcap_buf_state_t;

/* Packet entry in ring buffer - index into data pool */
struct pcap_buf_entry {
	struct timeval ts;       /* Timestamp */
	uint32_t caplen;         /* Captured length */
	uint32_t len;            /* Original length on wire */
	uint32_t data_offset;    /* Offset into data pool */
};

/* Buffer configuration */
struct pcap_buf_config {
	uint32_t max_memory_bytes;   /* Maximum memory usage */
	uint32_t duration_sec;       /* Rolling window duration */
	uint32_t pre_trigger_sec;    /* Seconds to keep before trigger */
	uint32_t post_trigger_sec;   /* Seconds to record after trigger */
	int datalink_type;           /* pcap datalink type (DLT_*) */
	uint32_t snaplen;            /* Snapshot length */
};

/* Buffer statistics */
struct pcap_buf_stats {
	uint64_t total_packets;      /* Total packets currently stored */
	uint64_t total_bytes;        /* Total bytes currently stored */
	uint64_t dropped_packets;    /* Packets dropped due to memory */
	uint64_t oldest_ts_sec;      /* Oldest packet timestamp (seconds) */
	uint64_t newest_ts_sec;      /* Newest packet timestamp (seconds) */
	uint32_t current_memory;     /* Current memory usage in bytes */
	uint32_t buffer_percent;     /* How full the time window is (0-100) */
	pcap_buf_state_t state;      /* Current buffer state */
};

/* Result of a trigger operation */
struct pcap_buf_trigger_result {
	char filepath[PCAP_BUF_FILEPATH_LEN];
	uint64_t file_size;
	uint32_t packet_count;
	uint32_t duration_sec;
	int success;
};

/*
 * Initialize the pcap buffer with given configuration.
 * Must be called before any other pcap_buf_* functions.
 *
 * Returns 0 on success, -1 on failure.
 */
int pcap_buf_init(struct pcap_buf_config *config);

/*
 * Destroy the pcap buffer and free all resources.
 */
void pcap_buf_destroy(void);

/*
 * Enable packet recording to the buffer.
 * Returns 0 on success, -1 on failure.
 */
int pcap_buf_enable(void);

/*
 * Disable packet recording. Buffer contents are preserved.
 * Returns 0 on success, -1 on failure.
 */
int pcap_buf_disable(void);

/*
 * Store a packet in the ring buffer.
 * Called from the packet capture thread - must be fast!
 * Uses spinlock for minimal latency.
 *
 * If memory limit is reached, oldest packets are dropped.
 * If state is not RECORDING or TRIGGERED, packet is ignored.
 *
 * Returns 0 on success, -1 on failure.
 */
int pcap_buf_store_packet(const struct pcap_pkthdr *hdr,
                          const uint8_t *data);

/*
 * Trigger a capture. Records the trigger time and reason.
 * If post_trigger_sec > 0, continues recording until window is filled.
 *
 * Returns 0 on success, -1 on failure.
 */
int pcap_buf_trigger(const char *trigger_reason);

/*
 * Write buffered packets to a pcap file.
 * Uses pre/post trigger configuration to determine time window.
 *
 * result: Output structure with file info
 *
 * Returns 0 on success, -1 on failure.
 */
int pcap_buf_write_file(struct pcap_buf_trigger_result *result);

/*
 * Check if post-trigger recording is complete.
 * Returns 1 if complete, 0 if still recording.
 */
int pcap_buf_post_trigger_complete(void);

/*
 * Update buffer configuration.
 * Some changes may require reinitializing the buffer.
 *
 * Returns 0 on success, -1 on failure.
 */
int pcap_buf_set_config(struct pcap_buf_config *config);

/*
 * Get current buffer configuration.
 * Returns 0 on success, -1 on failure.
 */
int pcap_buf_get_config(struct pcap_buf_config *config);

/*
 * Get current buffer statistics.
 * Returns 0 on success, -1 on failure.
 */
int pcap_buf_get_stats(struct pcap_buf_stats *stats);

/*
 * Get current buffer state.
 */
pcap_buf_state_t pcap_buf_get_state(void);

/*
 * Clear all packets from buffer without changing state.
 * Useful when switching interfaces.
 */
void pcap_buf_clear(void);

/*
 * Set the datalink type (called when interface changes).
 */
void pcap_buf_set_datalink(int dlt);

/* Memory management helpers */

/*
 * Query available system memory.
 * Returns available memory in bytes.
 */
uint64_t pcap_buf_get_available_memory(void);

/*
 * Calculate optimal buffer size for given parameters.
 *
 * duration_sec: Desired rolling window duration
 * avg_pkt_size: Estimated average packet size (use 500 if unknown)
 * bitrate_bps: Estimated interface bitrate in bits per second
 *
 * Returns recommended max_memory_bytes.
 */
uint32_t pcap_buf_calculate_optimal_size(uint32_t duration_sec,
                                         uint32_t avg_pkt_size,
                                         uint64_t bitrate_bps);

/*
 * Ensure the pcap output directory exists.
 * Returns 0 on success, -1 on failure.
 */
int pcap_buf_ensure_directory(void);

#endif /* PCAP_BUFFER_H */
