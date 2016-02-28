#ifndef MQ_MSG_STATS_H
#define MQ_MSG_STATS_H

#define NS(name) PRIMITIVE_CAT(mq_stats_, name)
#define PRIMITIVE_CAT(a, ...) a##__VA_ARGS__

#define PGAP_DISTR_BINS 200

enum {
	MQ_STATS_MSG_STATS,
	MQ_STATS_MSG_DISTR,
};

struct NS(msg) {

#if 0
	/* FIXME: stamp this! */
	struct timespec timestamp;
#endif

	uint64_t interval_ns;

	int msg_type;

	union {
		struct {
			uint64_t min_rx_bytes;
			uint64_t max_rx_bytes;
			uint64_t mean_rx_bytes;

			uint64_t min_tx_bytes;
			uint64_t max_tx_bytes;
			uint64_t mean_tx_bytes;

			uint32_t min_rx_packets;
			uint32_t max_rx_packets;
			uint32_t mean_rx_packets;

			uint32_t min_tx_packets;
			uint32_t max_tx_packets;
			uint32_t mean_tx_packets;

			uint32_t max_whoosh;
			uint32_t mean_whoosh;
			uint32_t sd_whoosh;

			uint32_t min_rx_packet_gap;
			uint32_t max_rx_packet_gap;
			uint32_t mean_rx_packet_gap;

			uint32_t min_tx_packet_gap;
			uint32_t max_tx_packet_gap;
			uint32_t mean_tx_packet_gap;
		};

		struct {
			uint8_t nz_bins;
			uint8_t pgap_distr[PGAP_DISTR_BINS];
		};
	};

	char iface[MAX_IFACE_LEN];
};

#include "mq_generic.h"

#undef NS
#endif
