#ifndef JT_MSG_SAMPLE_PERIOD_H
#define JT_MSG_SAMPLE_PERIOD_H

int jt_sample_period_packer(void *data, char **out);
int jt_sample_period_unpacker(json_t *root, void **data);
int jt_sample_period_printer(void *data, char *out, int len);
int jt_sample_period_free(void *data);
const char *jt_sample_period_msg_get(void);

int sample_period;

#endif
