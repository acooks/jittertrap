#ifndef JT_MSG_PGAP_DISTR_H
#define JT_MSG_PGAP_DISTR_H

#define PGAP_DISTR_BINS 200


int jt_pgap_distr_packer(void *data, char **out);
int jt_pgap_distr_unpacker(json_t *root, void **data);
int jt_pgap_distr_printer(void *data);
int jt_pgap_distr_free(void *data);
const char *jt_pgap_distr_test_msg_get();

struct jt_msg_pgap_distr
{
	struct timespec timestamp;
	uint64_t interval_ns;
	uint8_t pgap_distr[PGAP_DISTR_BINS];
	char iface[MAX_IFACE_LEN];
};

#endif
