#ifndef MQ_MSG_STATS_H
#define MQ_MSG_STATS_H

#define NS(name) PRIMITIVE_CAT(mq_stats_, name)
#define PRIMITIVE_CAT(a, ...) a##__VA_ARGS__


struct NS(msg) {
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

	uint32_t min_rx_packet_gap;
	uint32_t max_rx_packet_gap;
	uint32_t mean_rx_packet_gap;
	uint32_t sd_rx_packet_gap;

	uint32_t min_tx_packet_gap;
	uint32_t max_tx_packet_gap;
	uint32_t mean_tx_packet_gap;
	uint32_t sd_tx_packet_gap;

	char iface[MAX_IFACE_LEN];
};

#include "mq_generic.h"

#undef NS
#endif
