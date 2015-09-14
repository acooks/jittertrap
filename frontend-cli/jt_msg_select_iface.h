#ifndef JT_MSG_SELECT_IFACE_H
#define JT_MSG_SELECT_IFACE_H

int jt_select_iface_unpacker(json_t *root, void **data);
int jt_select_iface_consumer(void *data);


char (*iface)[MAX_IFACE_LEN];

#endif
