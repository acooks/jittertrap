#ifndef SAMPLE_BUF_H
#define SAMPLE_BUF_H

void raw_sample_buf_init();
struct iface_stats* raw_sample_buf_produce_next();
struct iface_stats* raw_sample_buf_consume_next();

#endif
