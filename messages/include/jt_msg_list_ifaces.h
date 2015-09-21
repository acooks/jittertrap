#ifndef JT_MSG_LIST_IFACES_H
#define JT_MSG_LIST_IFACES_H

int jt_iface_list_packer(void *data, char **out);
int jt_iface_list_unpacker(json_t *root, void **data);
int jt_iface_list_printer(void *data);
int jt_iface_list_free(void *data);
const char *jt_iface_list_test_msg_get(void);

struct jt_iface_list
{
	int count;
	char (*ifaces)[MAX_IFACE_LEN];
};

#endif
