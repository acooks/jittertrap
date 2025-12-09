/* Tier 5 WebSocket message queue - 100ms+ interval messages and config
 * This is the guaranteed minimum tier that is never unsubscribed. */
#ifndef MQ_MSG_WS_5_H
#define MQ_MSG_WS_5_H

#define NS(name) PRIMITIVE_CAT(mq_ws_5_, name)
#define PRIMITIVE_CAT(a, ...) a##__VA_ARGS__

struct NS(msg) {
	char m[MAX_JSON_MSG_LEN];
};

#include "mq_generic.h"

#undef NS

#endif
