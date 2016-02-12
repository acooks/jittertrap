#ifndef MQ_MSG_STATS_H
#define MQ_MSG_STATS_H

#define NS(name) PRIMITIVE_CAT(mq_stats_, name)
#define PRIMITIVE_CAT(a, ...) a ## __VA_ARGS__

#define MAX_CONSUMERS 32
#define MAX_Q_DEPTH 32

struct NS(msg) {
        struct timespec timestamp;
        int64_t window;
        int64_t mean_rx_bytes;
        int64_t mean_tx_bytes;
        int64_t mean_rx_packets;
        int64_t mean_tx_packets;
        int64_t min_rx_bytes;
        int64_t min_tx_bytes;
        int64_t min_rx_packets;
        int64_t min_tx_packets;
        int64_t max_rx_bytes;
        int64_t max_tx_bytes;
        int64_t max_rx_packets;
        int64_t max_tx_packets;
};

#include "mq_generic.h"

#undef NS
#endif
