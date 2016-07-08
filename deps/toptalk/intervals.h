#ifndef INTERVALS_H
#define INTERVALS_H

#include <pcap.h>
#include "utlist.h"
#include "uthash.h"
#include "decode.h"


/* INTERVAL_COUNT must be defined in intervals_user.h */
/* intvervals[] must be defined in intervals_user.c */
extern struct timeval const tt_intervals[INTERVAL_COUNT];

struct tt_top_flows {
	unsigned int flow_count;
	unsigned int total_bytes;
	unsigned int total_packets;
	struct flow_record flow[MAX_FLOW_COUNT][INTERVAL_COUNT];
};

struct tt_thread_info {
	pthread_t thread_id;
	pthread_attr_t attr;
        char *dev;
        struct tt_top_flows *t5;
	pthread_mutex_t t5_mutex;
        unsigned int decode_errors;
};

void tt_update_ref_window_size(struct timeval t);
void tt_get_top5(struct tt_top_flows *t5);
int tt_get_flow_count();
void *tt_intervals_run(void *p);
void tt_intervals_init(struct tt_thread_info *ti);

#define handle_error_en(en, msg) \
        do {                                                                   \
                errno = en;                                                    \
                perror(msg);                                                   \
                exit(EXIT_FAILURE);                                            \
        } while (0)

#endif
