#ifndef JT_MSG_SET_NETEM_H
#define JT_MSG_SET_NETEM_H

int jt_set_netem_packer(void *data, char **out);
int jt_set_netem_unpacker(json_t *root, void **data);
int jt_set_netem_consumer(void *data);
int jt_set_netem_free(void *data);
const char *jt_set_netem_test_msg_get(void);

#endif
