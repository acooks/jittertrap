/* Tiered WebSocket message queues for adaptive rate limiting
 *
 * Messages are routed to different queues based on their interval:
 *   Tier 1: 5ms    (~400 msgs/sec)
 *   Tier 2: 10ms   (~200 msgs/sec)
 *   Tier 3: 20ms   (~100 msgs/sec)
 *   Tier 4: 50ms   (~40 msgs/sec)
 *   Tier 5: 100ms+ (~36 msgs/sec) + config messages
 *
 * Slow consumers can unsubscribe from faster tiers while maintaining
 * coarser resolution data on slower tiers.
 */
#ifndef MQ_WS_TIERED_H
#define MQ_WS_TIERED_H

#include "mq_msg_ws_1.h"
#include "mq_msg_ws_2.h"
#include "mq_msg_ws_3.h"
#include "mq_msg_ws_4.h"
#include "mq_msg_ws_5.h"

#define MQ_WS_NUM_TIERS 5

/* Interval thresholds in nanoseconds for routing */
#define MQ_WS_TIER_1_MAX_NS   5000000UL    /* 5ms */
#define MQ_WS_TIER_2_MAX_NS  10000000UL    /* 10ms */
#define MQ_WS_TIER_3_MAX_NS  20000000UL    /* 20ms */
#define MQ_WS_TIER_4_MAX_NS  50000000UL    /* 50ms */
/* Tier 5 handles everything else (100ms, 200ms, 500ms, 1s) plus config */

/* Map interval_ns to tier (1-5), returns 5 for config (interval_ns=0) */
static inline int mq_ws_interval_to_tier(uint64_t interval_ns)
{
	if (interval_ns == 0)
		return 5;  /* Config messages */
	if (interval_ns <= MQ_WS_TIER_1_MAX_NS)
		return 1;
	if (interval_ns <= MQ_WS_TIER_2_MAX_NS)
		return 2;
	if (interval_ns <= MQ_WS_TIER_3_MAX_NS)
		return 3;
	if (interval_ns <= MQ_WS_TIER_4_MAX_NS)
		return 4;
	return 5;
}

/* Map tier (1-5) to minimum interval in milliseconds */
static inline int mq_ws_tier_to_interval_ms(int tier)
{
	switch (tier) {
	case 1: return 5;
	case 2: return 10;
	case 3: return 20;
	case 4: return 50;
	case 5: return 100;
	default: return 100;
	}
}

#endif
