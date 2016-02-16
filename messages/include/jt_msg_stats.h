#ifndef JT_MSG_STATS_H
#define JT_MSG_STATS_H

int jt_stats_packer(void *data, char **out);
int jt_stats_unpacker(json_t *root, void **data);
int jt_stats_printer(void *data);
int jt_stats_free(void *data);
const char *jt_stats_test_msg_get();

struct jt_msg_stats
{
	struct timespec timestamp;
	uint64_t interval_ns;

	uint64_t min_rx_bytes;
	uint64_t max_rx_bytes;
	uint64_t mean_rx_bytes;
	uint32_t sd_rx_bytes;

	uint64_t min_tx_bytes;
	uint64_t max_tx_bytes;
	uint64_t mean_tx_bytes;
	uint32_t sd_tx_bytes;

	uint32_t min_rx_packets;
	uint32_t max_rx_packets;
	uint32_t mean_rx_packets;
	uint32_t sd_rx_packets;

	uint32_t min_tx_packets;
	uint32_t max_tx_packets;
	uint32_t mean_tx_packets;
	uint32_t sd_tx_packets;

	uint32_t max_whoosh;
	uint32_t mean_whoosh;
	uint32_t sd_whoosh;

	char iface[MAX_IFACE_LEN];
};

#endif
