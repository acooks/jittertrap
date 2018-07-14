#ifndef JT_MSG_NETEM_PARAMS_H
#define JT_MSG_NETEM_PARAMS_H

int jt_netem_params_packer(void *data, char **out);
int jt_netem_params_unpacker(json_t *root, void **data);
int jt_netem_params_printer(void *data, char *out, int len);
int jt_netem_params_free(void *data);
const char *jt_netem_params_test_msg_get(void);

struct jt_msg_netem_params
{
	char iface[MAX_IFACE_LEN];
	int delay;
	int jitter;
	int loss;
};

#endif
