#ifndef MQ_MSG_TT_H
#define MQ_MSG_TT_H

#define NS(name) PRIMITIVE_CAT(mq_tt_, name)
#define PRIMITIVE_CAT(a, ...) a##__VA_ARGS__

#define json_t void
#include "jt_msg_toptalk.h"


struct NS(msg) {
	struct jt_msg_toptalk m;
};

#include "mq_generic.h"

#undef NS
#endif
