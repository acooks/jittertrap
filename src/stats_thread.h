#ifndef STATS_THREAD_H
#define STATS_THREAD_H

#define MAX_IFACE_LEN 16

struct iface_stats {
	uint32_t sample_period_us;
	struct timespec timestamp;
	uint64_t rx_bytes;
	uint64_t tx_bytes;
	uint32_t rx_bytes_delta;
	uint32_t tx_bytes_delta;
	uint32_t rx_packets;
	uint32_t rx_packets_delta;
	uint32_t tx_packets;
	uint32_t tx_packets_delta;
	char	iface[MAX_IFACE_LEN];
};

int stats_thread_init(void (*stats_handler) (struct iface_stats * counts));
void stats_monitor_iface(const char *_iface);
void set_sample_period(int sample_period_ms);
int get_sample_period();

#endif
