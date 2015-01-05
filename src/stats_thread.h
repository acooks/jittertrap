#ifndef STATS_THREAD_H
#define STATS_THREAD_H

#ifndef SAMPLE_PERIOD_MS
#define SAMPLE_PERIOD_MS 100
#endif

struct byte_counts {
	long long timestamp;
	long long rx_bytes;
	long long tx_bytes;
	int rx_bytes_delta;
	int tx_bytes_delta;
};

int stats_thread_init(void (*stats_handler) (struct byte_counts * counts));
void stats_monitor_iface(const char *_iface);

#endif
