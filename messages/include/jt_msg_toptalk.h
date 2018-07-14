#ifndef JT_MSG_TOPTALK_H
#define JT_MSG_TOPTALK_H

int jt_toptalk_packer(void *data, char **out);
int jt_toptalk_unpacker(json_t *root, void **data);
int jt_toptalk_printer(void *data, char *out, int len);
int jt_toptalk_free(void *data);
const char *jt_toptalk_test_msg_get();

#define MAX_FLOWS 20
#define ADDR_LEN 40
#define PROTO_LEN 5

struct jt_msg_toptalk
{
	struct timespec timestamp;
	uint64_t interval_ns;
	uint32_t tflows;
	int64_t tbytes;
	int64_t tpackets;
	struct {
		int64_t bytes;
		int64_t packets;
		uint16_t sport;
		uint16_t dport;
		char src[ADDR_LEN];
		char dst[ADDR_LEN];
		char proto[PROTO_LEN];
	} flows[MAX_FLOWS];
};

#endif
