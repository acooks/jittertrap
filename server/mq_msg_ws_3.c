/* Tier 3 WebSocket message queue - 20ms interval messages
 * ~100 msgs/sec (50 stats + 50 toptalk) */
#include "mq_msg_ws_3.h"

#define MAX_CONSUMERS 8
#define MAX_Q_DEPTH 64

#define NS(name) PRIMITIVE_CAT(mq_ws_3_, name)
#define PRIMITIVE_CAT(a, ...) a##__VA_ARGS__

#include "mq_generic.c"

#undef NS
