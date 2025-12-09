/* Tier 4 WebSocket message queue - 50ms interval messages
 * ~40 msgs/sec (20 stats + 20 toptalk) */
#include "mq_msg_ws_4.h"

#define MAX_CONSUMERS 8
#define MAX_Q_DEPTH 64

#define NS(name) PRIMITIVE_CAT(mq_ws_4_, name)
#define PRIMITIVE_CAT(a, ...) a##__VA_ARGS__

#include "mq_generic.c"

#undef NS
