#ifndef INTERVALS_H
#define INTERVALS_H

#define INTERVAL_COUNT 8

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

void update_ref_window_size(struct timeval t);
void update_stats_tables(struct flow_pkt *pkt);
void get_top5(struct top_flows *t5);
int get_flow_count();

#endif
