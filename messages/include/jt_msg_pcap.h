/*
 * jt_msg_pcap.h - PCAP buffer message type definitions
 */

#ifndef JT_MSG_PCAP_H
#define JT_MSG_PCAP_H

#include <stdint.h>
#include <jansson.h>

/* Maximum lengths for string fields */
#define PCAP_MSG_REASON_LEN   128
#define PCAP_MSG_FILENAME_LEN 256

/*
 * PCAP Configuration message (bidirectional)
 * C2S: Client requests configuration change
 * S2C: Server confirms current configuration
 */
struct jt_msg_pcap_config {
	uint32_t enabled;           /* 0=disabled, 1=enabled */
	uint32_t max_memory_mb;     /* Maximum memory in MB */
	uint32_t duration_sec;      /* Rolling window duration */
	uint32_t pre_trigger_sec;   /* Seconds to keep before trigger */
	uint32_t post_trigger_sec;  /* Seconds to record after trigger */
};

/*
 * PCAP Status message (S2C only)
 * Sent periodically to update client on buffer state
 */
struct jt_msg_pcap_status {
	uint32_t state;             /* pcap_buf_state_t */
	uint64_t total_packets;     /* Packets currently in buffer */
	uint64_t total_bytes;       /* Bytes currently in buffer */
	uint64_t dropped_packets;   /* Packets dropped due to memory */
	uint32_t current_memory_mb; /* Current memory usage in MB */
	uint32_t buffer_percent;    /* How full the time window is (0-100) */
	uint32_t oldest_age_sec;    /* Age of oldest packet in seconds */
};

/*
 * PCAP Trigger message (C2S only)
 * Client requests capture trigger
 */
struct jt_msg_pcap_trigger {
	char reason[PCAP_MSG_REASON_LEN];  /* Trigger reason for metadata */
};

/*
 * PCAP Ready message (S2C only)
 * Sent when pcap file is ready for download
 */
struct jt_msg_pcap_ready {
	char filename[PCAP_MSG_FILENAME_LEN];  /* Download URL path */
	uint64_t file_size;                    /* File size in bytes */
	uint32_t packet_count;                 /* Number of packets in file */
	uint32_t duration_sec;                 /* Actual duration captured */
};

/* PCAP Config message functions */
int jt_pcap_config_packer(void *data, char **out);
int jt_pcap_config_unpacker(json_t *root, void **data);
int jt_pcap_config_printer(void *data, char *out, int len);
int jt_pcap_config_free(void *data);
const char *jt_pcap_config_test_msg_get(void);

/* PCAP Status message functions */
int jt_pcap_status_packer(void *data, char **out);
int jt_pcap_status_unpacker(json_t *root, void **data);
int jt_pcap_status_printer(void *data, char *out, int len);
int jt_pcap_status_free(void *data);
const char *jt_pcap_status_test_msg_get(void);

/* PCAP Trigger message functions */
int jt_pcap_trigger_packer(void *data, char **out);
int jt_pcap_trigger_unpacker(json_t *root, void **data);
int jt_pcap_trigger_printer(void *data, char *out, int len);
int jt_pcap_trigger_free(void *data);
const char *jt_pcap_trigger_test_msg_get(void);

/* PCAP Ready message functions */
int jt_pcap_ready_packer(void *data, char **out);
int jt_pcap_ready_unpacker(json_t *root, void **data);
int jt_pcap_ready_printer(void *data, char *out, int len);
int jt_pcap_ready_free(void *data);
const char *jt_pcap_ready_test_msg_get(void);

#endif /* JT_MSG_PCAP_H */
