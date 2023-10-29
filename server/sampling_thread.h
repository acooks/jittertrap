#ifndef SAMPLING_THREAD_H
#define SAMPLING_THREAD_H

int sample_thread_init(void (*stats_handler)(struct iface_stats *counts));
void sample_iface(const char *_iface);

/* microseconds */
void set_sample_period(int sample_period_us);
int get_sample_period(void);
#endif
