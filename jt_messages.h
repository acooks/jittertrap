#ifndef JT_MESSAGES_H
#define JT_MESSAGES_H

#include "jt_message_types.h"
#include "jt_msg_stats.h"

static const struct jt_msg_type jt_messages[] =
    {[JT_MSG_STATS_V1] = {
	     .type = JT_MSG_STATS_V1,
	     .key = "stats",
	     .unpack = jt_stats_unpacker,
	     .consume = jt_stats_consumer,
     },
     [JT_MSG_END] = {
	     .type = JT_MSG_END, .key = NULL, .unpack = NULL, .consume = NULL
     } };

/* try to parse message and consume data if possible */
int jt_msg_handler(char *input);

#endif
