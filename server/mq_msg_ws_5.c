/* Tier 5 WebSocket message queue - 100ms, 200ms, 500ms, 1s intervals + config
 * ~36 msgs/sec - guaranteed minimum tier that is never unsubscribed */
#include "mq_msg_ws_5.h"

#define MAX_CONSUMERS 8
#define MAX_Q_DEPTH 64

#define NS(name) PRIMITIVE_CAT(mq_ws_5_, name)
#define PRIMITIVE_CAT(a, ...) a##__VA_ARGS__

#include "mq_generic.c"

#undef NS
