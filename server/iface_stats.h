#ifndef IFACE_STATS_H
#define IFACE_STATS_H

struct sample {
	struct timespec timestamp;
	uint64_t whoosh_error_ns;
	uint64_t rx_bytes;
	uint64_t tx_bytes;
	uint64_t rx_bytes_delta;
	uint64_t tx_bytes_delta;
	uint64_t rx_packets;
	uint64_t rx_packets_delta;
	uint64_t tx_packets;
	uint64_t tx_packets_delta;
};

struct iface_stats {
	uint32_t sample_period_us;
	char iface[MAX_IFACE_LEN];
	struct sample samples[SAMPLES_PER_FRAME];
	uint64_t whoosh_err_mean;
	uint64_t whoosh_err_max;
	uint64_t whoosh_err_sd;
};

#endif
