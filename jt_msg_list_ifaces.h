#ifndef JT_MSG_LIST_IFACES_H
#define JT_MSG_LIST_IFACES_H

int jt_iface_list_unpacker(json_t *root, void **data);
int jt_iface_list_consumer(void *data);

struct jt_iface_list
{
	int count;
	char (*ifaces)[MAX_IFACE_LEN];
};

#endif
