#ifndef MQ_MSG_WS_H
#define MQ_MSG_WS_H

#define NS(name) PRIMITIVE_CAT(mq_ws_, name)
#define PRIMITIVE_CAT(a, ...) a ## __VA_ARGS__

#define MAX_CONSUMERS 32
#define MAX_Q_DEPTH 32
#define MAX_JSON_MSG_LEN 3000

struct NS(msg) {
        char m[MAX_JSON_MSG_LEN];
};

#include "mq_generic.h"

#undef NS

#endif
