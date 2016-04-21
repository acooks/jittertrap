#ifndef INTERVALS_H
#define INTERVALS_H

#define INTERVAL_COUNT 8

/* sliding window reference interval in microseconds */
#define REF_INTERVAL 500E3

/* microseconds */
extern const int intervals[INTERVAL_COUNT];

struct top_flows {
	struct flow_record flow[5][INTERVAL_COUNT];
};

struct flow_hash {
        struct flow_record f;
	union {
	        UT_hash_handle r_hh; /* sliding window reference table */
		UT_hash_handle ts_hh[INTERVAL_COUNT]; /* time series tables */
	};
};

struct flow_pkt_list {
        struct flow_pkt pkt;
        struct flow_pkt_list *next, *prev;
};

void update_stats_tables(struct flow_pkt *pkt);
void get_top5(struct top_flows *t5);
int get_flow_count();

#endif
