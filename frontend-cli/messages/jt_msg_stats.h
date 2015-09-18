#ifndef JT_MSG_STATS_H
#define JT_MSG_STATS_H

int jt_stats_packer(void *data, char **out);
int jt_stats_unpacker(json_t *root, void **data);
int jt_stats_consumer(void *data);
const char *jt_stats_test_msg_get();

struct stats_sample
{
	int rx;
	int tx;
	int rxPkt;
	int txPkt;
};

struct stats_err
{
	int mean;
	int max;
	int sd;
};

struct jt_msg_stats
{
	char iface[MAX_IFACE_LEN];
	struct stats_err err;
	int sample_count;
	struct stats_sample *samples;
};

#endif
