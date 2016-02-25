#include <time.h>
#include <inttypes.h>

#include "mq_msg_stats.h"

#define MAX_CONSUMERS 1
#define MAX_Q_DEPTH 8

#define NS(name) PRIMITIVE_CAT(mq_stats_, name)
#define PRIMITIVE_CAT(a, ...) a##__VA_ARGS__
#include "mq_generic.c"
#undef NS
