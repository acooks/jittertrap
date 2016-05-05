#ifndef INTERVALS_H
#define INTERVALS_H

#include <pcap.h>
#include "utlist.h"
#include "uthash.h"
#include "decode.h"


/* INTERVAL_COUNT must be defined in intervals_user.h */
/* intvervals[] must be defined in intervals_user.c */
extern struct timeval const intervals[INTERVAL_COUNT];

struct top_flows {
	int count;
	struct flow_record flow[5][INTERVAL_COUNT];
};

struct flow_hash {
        struct flow_record f;
	union {
	        UT_hash_handle r_hh; /* sliding window reference table */
		UT_hash_handle ts_hh; /* time series tables */
	};
};

struct flow_pkt_list {
        struct flow_pkt pkt;
        struct flow_pkt_list *next, *prev;
};

struct thread_info {
	pthread_t thread_id;
	pthread_attr_t attr;
        char *dev;
        struct top_flows *t5;
	pthread_mutex_t t5_mutex;
        unsigned int decode_errors;
};

struct pcap_info {
        pcap_t *handle;
        int selectable_fd;
};

struct pcap_handler_result {
        int err;
        char errstr[DECODE_ERRBUF_SIZE];
};

void update_ref_window_size(struct timeval t);
void get_top5(struct top_flows *t5);
int get_flow_count();
void *intervals_run(void *p);
void intervals_init(struct thread_info *ti);

#define handle_error_en(en, msg) \
        do {                                                                   \
                errno = en;                                                    \
                perror(msg);                                                   \
                exit(EXIT_FAILURE);                                            \
        } while (0)

#endif
