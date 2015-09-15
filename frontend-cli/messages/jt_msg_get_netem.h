#ifndef JT_MSG_GET_NETEM_H
#define JT_MSG_GET_NETEM_H

int jt_get_netem_unpacker(json_t *root, void **data);
int jt_get_netem_consumer(void *data);

char (*iface)[MAX_IFACE_LEN];

#endif
