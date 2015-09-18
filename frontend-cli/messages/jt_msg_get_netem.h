#ifndef JT_MSG_GET_NETEM_H
#define JT_MSG_GET_NETEM_H

/* layer api */
int jt_get_netem_packer(void *data, char **out);
int jt_get_netem_unpacker(json_t *root, void **data);
int jt_get_netem_consumer(void *data);
const char *jt_get_netem_test_msg_get(void);

/* internal storage */
char (*iface)[MAX_IFACE_LEN];

#endif
