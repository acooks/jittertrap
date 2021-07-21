#ifndef JT_MSG_HELLO_H
#define JT_MSG_HELLO_H

int jt_hello_packer(void *data, char **out);
int jt_hello_unpacker(json_t *root, void **data);
int jt_hello_printer(void *data, char *out, int len);
int jt_hello_free(void *data);
const char *jt_hello_test_msg_get(void);

#endif
