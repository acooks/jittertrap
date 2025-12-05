#ifndef INTERVALS_H
#define INTERVALS_H

#include <pcap.h>
#include "utlist.h"
#include "uthash.h"
#include "decode.h"


/* ================================= NOTE: ===================================
 * INTERVAL_COUNT and MAX_FLOW_COUNT must be defined by the user at compile
 * time using CFLAGS like -DINTERVAL_COUNT=nnn and -DMAX_FLOW_COUNT=mmm
 */

/* intvervals[] must be defined in intervals_user.c */
extern struct timeval const tt_intervals[INTERVAL_COUNT];

struct tt_top_flows {
	struct timeval timestamp;
	int64_t flow_count;
	int64_t total_bytes;
	int64_t total_packets;
	struct flow_record flow[MAX_FLOW_COUNT][INTERVAL_COUNT];
};

/* forward declaration; definition and use is internal to tt thread */
struct tt_thread_private;

struct tt_thread_info {
	pthread_t thread_id;
	pthread_attr_t attr;
        char *dev;
        struct tt_top_flows *t5;
	pthread_mutex_t t5_mutex;
	unsigned int decode_errors;
	const char * const thread_name;
	const int thread_prio;
	int thread_state;
	struct tt_thread_private *priv;
};

void tt_update_ref_window_size(struct tt_thread_info *ti, struct timeval t);
int tt_get_flow_count(void);
void *tt_intervals_run(void *p);
int tt_intervals_init(struct tt_thread_info *ti);
int tt_intervals_free(struct tt_thread_info *ti);

/* Optional callbacks for packet capture integration */
void tt_set_pcap_callback(void (*store_cb)(const struct pcap_pkthdr *, const uint8_t *),
                          void (*iface_cb)(int dlt));

#define handle_error_en(en, msg) \
        do {                                                                   \
                errno = en;                                                    \
                perror(msg);                                                   \
                exit(EXIT_FAILURE);                                            \
        } while (0)

#endif
