#ifndef MQ_MSG_STATS_H
#define MQ_MSG_STATS_H

#define NS(name) PRIMITIVE_CAT(mq_stats_, name)
#define PRIMITIVE_CAT(a, ...) a##__VA_ARGS__

#define MAX_CONSUMERS 32
#define MAX_Q_DEPTH 32

#if FIXME_FIXED_LATER
struct NS(msg) {
	struct timespec timestamp;
	uint64_t interval_ns;
	uint32_t whoosh_err_mean;
	uint32_t whoosh_err_max;
	uint32_t whoosh_err_sd;
	uint64_t mean_rx_bytes;
	uint64_t mean_tx_bytes;
	uint64_t min_rx_bytes;
	uint64_t min_tx_bytes;
	uint64_t max_rx_bytes;
	uint64_t max_tx_bytes;
	uint32_t mean_rx_packets;
	uint32_t mean_tx_packets;
	uint32_t min_rx_packets;
	uint32_t min_tx_packets;
	uint32_t max_rx_packets;
	uint32_t max_tx_packets;
	char iface[MAX_IFACE_LEN];
};

#else

/* copy iface stats until the mq mechanics work */
#include <pthread.h>
#include "jittertrap.h"
struct NS(sample) {
	struct timespec timestamp;
	int64_t whoosh_error_ns;
	int64_t rx_bytes;
	int64_t tx_bytes;
	int64_t rx_bytes_delta;
	int64_t tx_bytes_delta;
	int64_t rx_packets;
	int64_t rx_packets_delta;
	int64_t tx_packets;
	int64_t tx_packets_delta;
};

struct NS(msg) {
	uint32_t sample_period_us;
	char iface[MAX_IFACE_LEN];
	struct NS(sample) samples[SAMPLES_PER_FRAME];
	uint64_t whoosh_err_mean;
	uint64_t whoosh_err_max;
	uint64_t whoosh_err_sd;
};

#endif

#include "mq_generic.h"

#undef NS
#endif
