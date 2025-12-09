/* Tier 1 WebSocket message queue - 5ms interval messages
 * ~400 msgs/sec (200 stats + 200 toptalk) */
#include "mq_msg_ws_1.h"

#define MAX_CONSUMERS 8
#define MAX_Q_DEPTH 64

#define NS(name) PRIMITIVE_CAT(mq_ws_1_, name)
#define PRIMITIVE_CAT(a, ...) a##__VA_ARGS__

#include "mq_generic.c"

#undef NS
