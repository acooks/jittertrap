#ifndef PROTO_DINC_H
#define PROTO_DINC_H

#include <time.h>

/* jittertrap protocol */

/*
 * one of these is auto-created for each connection and a pointer to the
 * appropriate instance is passed to the callback in the user parameter
 *
 * Per-consumer state for multi-queue subscription and adaptive tier logic.
 * Each consumer subscribes to 5 tiered queues and can unsubscribe from
 * faster tiers when falling behind.
 */

#define MQ_WS_NUM_TIERS 5

/* Adaptive tier parameters */
#define TIER_WINDOW_SECONDS 5     /* Seconds before making tier decisions */
#define TIER_HIGH_WATERMARK 0.10  /* 10% drops -> degrade to slower tier */
#define TIER_LOW_WATERMARK 0.02   /* 2% drops -> try upgrading to faster tier */

struct per_session_data__jittertrap {
	unsigned long consumer_id[MQ_WS_NUM_TIERS];  /* per-tier IDs, 0 if unsubscribed */
	int current_min_tier;          /* 1-5, lowest tier subscribed (1=fastest) */
	unsigned int drops_window;     /* drops in current window */
	unsigned int delivered_window; /* delivered in current window */
	time_t window_start;           /* window start time */
};

int callback_jittertrap(struct lws *wsi, enum lws_callback_reasons reason,
                        void *user, void *in, size_t len);
#endif
