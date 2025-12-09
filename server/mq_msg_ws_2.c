/* Tier 2 WebSocket message queue - 10ms interval messages
 * ~200 msgs/sec (100 stats + 100 toptalk) */
#include "mq_msg_ws_2.h"

#define MAX_CONSUMERS 8
#define MAX_Q_DEPTH 64

#define NS(name) PRIMITIVE_CAT(mq_ws_2_, name)
#define PRIMITIVE_CAT(a, ...) a##__VA_ARGS__

#include "mq_generic.c"

#undef NS
