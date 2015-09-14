#ifndef STATS_THREAD_H
#define STATS_THREAD_H

int stats_thread_init(void (*stats_handler) (struct iface_stats * counts));
void stats_monitor_iface(const char *_iface);

/* microseconds */
void set_sample_period(int sample_period_us);
int get_sample_period();

#endif
