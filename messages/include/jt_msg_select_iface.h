#ifndef JT_MSG_SELECT_IFACE_H
#define JT_MSG_SELECT_IFACE_H

int jt_select_iface_packer(void *data, char **out);
int jt_select_iface_unpacker(json_t *root, void **data);
int jt_select_iface_consumer(void *data);
int jt_select_iface_free(void *data);
const char *jt_select_iface_test_msg_get(void);

char (*iface)[MAX_IFACE_LEN];

#endif
