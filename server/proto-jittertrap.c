/* jittertrap protocol */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <syslog.h>
#include <libwebsockets.h>

#include "proto.h"
#include "jt_server_message_handler.h"

#include "mq_ws_tiered.h"
#include "ws_compress.h"

#include "proto-jittertrap.h"

static int consumer_count = 0;

struct cb_data {
	struct lws *wsi;
	unsigned char *buf;
};

/* Generic message writer that works with any tier's message struct.
 * All tiers use the same struct layout: char m[MAX_JSON_MSG_LEN] */
static int lws_writer_generic(const char *msg, void *data)
{
	int len, n;
	struct cb_data *d = (struct cb_data *)data;
	unsigned char *compressed = NULL;
	size_t compressed_len = 0;

	assert(d);
	len = strlen(msg);

	if (len <= 0) {
		return 0;
	}

	/* Try to compress larger messages */
	if (ws_should_compress(len)) {
		int ret = ws_compress(msg, len, &compressed, &compressed_len);
		if (ret == 0) {
			/* Compression successful - send as binary */
			memcpy(d->buf, compressed, compressed_len);
			free(compressed);
			n = lws_write(d->wsi, d->buf, compressed_len,
			              LWS_WRITE_BINARY);
			if (n < (int)compressed_len) {
				fprintf(stderr, "Short write (compressed)\n");
				return -1;
			}
			return 0;
		}
		/* Compression not beneficial (ret=1) or error (ret=-1),
		 * fall through to send uncompressed */
		if (compressed) {
			free(compressed);
		}
	}

	/* Send uncompressed as text */
	memcpy(d->buf, msg, len);
	n = lws_write(d->wsi, d->buf, len, LWS_WRITE_TEXT);
	if (n < len) {
		fprintf(stderr, "Short write :(\n");
		return -1;
	}
	return 0;
}

/* Tier-specific writers that extract the message string */
static int lws_writer_1(struct mq_ws_1_msg *m, void *data) {
	return lws_writer_generic(m->m, data);
}
static int lws_writer_2(struct mq_ws_2_msg *m, void *data) {
	return lws_writer_generic(m->m, data);
}
static int lws_writer_3(struct mq_ws_3_msg *m, void *data) {
	return lws_writer_generic(m->m, data);
}
static int lws_writer_4(struct mq_ws_4_msg *m, void *data) {
	return lws_writer_generic(m->m, data);
}
static int lws_writer_5(struct mq_ws_5_msg *m, void *data) {
	return lws_writer_generic(m->m, data);
}

/* Subscribe to a specific tier queue */
static int subscribe_tier(struct per_session_data__jittertrap *pss, int tier)
{
	int err = 0;
	switch (tier) {
	case 1: err = mq_ws_1_consumer_subscribe(&pss->consumer_id[0]); break;
	case 2: err = mq_ws_2_consumer_subscribe(&pss->consumer_id[1]); break;
	case 3: err = mq_ws_3_consumer_subscribe(&pss->consumer_id[2]); break;
	case 4: err = mq_ws_4_consumer_subscribe(&pss->consumer_id[3]); break;
	case 5: err = mq_ws_5_consumer_subscribe(&pss->consumer_id[4]); break;
	}
	return err;
}

/* Unsubscribe from a specific tier queue */
static int unsubscribe_tier(struct per_session_data__jittertrap *pss, int tier)
{
	int err = 0;
	int idx = tier - 1;
	if (pss->consumer_id[idx] == 0) return 0;  /* Already unsubscribed */

	switch (tier) {
	case 1: err = mq_ws_1_consumer_unsubscribe(pss->consumer_id[0]); break;
	case 2: err = mq_ws_2_consumer_unsubscribe(pss->consumer_id[1]); break;
	case 3: err = mq_ws_3_consumer_unsubscribe(pss->consumer_id[2]); break;
	case 4: err = mq_ws_4_consumer_unsubscribe(pss->consumer_id[3]); break;
	case 5: err = mq_ws_5_consumer_unsubscribe(pss->consumer_id[4]); break;
	}
	if (!err) pss->consumer_id[idx] = 0;
	return err;
}

/* Initial tier for new connections - start conservative and upgrade if client can handle it.
 * Tier 3 (20ms) is a reasonable starting point that most connections can handle. */
#define INITIAL_MIN_TIER 3

/* Subscribe to tiers from initial_tier to 5 */
static int subscribe_initial_tiers(struct per_session_data__jittertrap *pss)
{
	int err;
	for (int tier = INITIAL_MIN_TIER; tier <= MQ_WS_NUM_TIERS; tier++) {
		err = subscribe_tier(pss, tier);
		if (err) {
			/* Unsubscribe from any already-subscribed tiers on failure */
			for (int t = INITIAL_MIN_TIER; t < tier; t++) {
				unsubscribe_tier(pss, t);
			}
			return err;
		}
	}
	pss->current_min_tier = INITIAL_MIN_TIER;
	return 0;
}

/* Unsubscribe from all tiers */
static void unsubscribe_all_tiers(struct per_session_data__jittertrap *pss)
{
	for (int tier = 1; tier <= MQ_WS_NUM_TIERS; tier++) {
		unsubscribe_tier(pss, tier);
	}
}

/* Get drop and delivered stats for subscribed tiers, update window counters */
static void update_window_stats(struct per_session_data__jittertrap *pss)
{
	unsigned int drops, delivered;

	for (int tier = pss->current_min_tier; tier <= MQ_WS_NUM_TIERS; tier++) {
		int idx = tier - 1;
		if (pss->consumer_id[idx] == 0) continue;

		switch (tier) {
		case 1:
			drops = mq_ws_1_consumer_get_and_clear_stats(pss->consumer_id[0], &delivered);
			break;
		case 2:
			drops = mq_ws_2_consumer_get_and_clear_stats(pss->consumer_id[1], &delivered);
			break;
		case 3:
			drops = mq_ws_3_consumer_get_and_clear_stats(pss->consumer_id[2], &delivered);
			break;
		case 4:
			drops = mq_ws_4_consumer_get_and_clear_stats(pss->consumer_id[3], &delivered);
			break;
		case 5:
			drops = mq_ws_5_consumer_get_and_clear_stats(pss->consumer_id[4], &delivered);
			break;
		default:
			drops = delivered = 0;
		}
		pss->drops_window += drops;
		pss->delivered_window += delivered;
	}
}

/* Calculate drop percentage */
static float calc_drop_percentage(struct per_session_data__jittertrap *pss)
{
	unsigned int total = pss->drops_window + pss->delivered_window;
	if (total == 0) return 0.0f;
	return (float)pss->drops_window / (float)total;
}

/* Send resolution notification to client */
static void send_resolution_update(struct lws *wsi, unsigned char *p,
                                   struct per_session_data__jittertrap *pss)
{
	int min_interval_ms = mq_ws_tier_to_interval_ms(pss->current_min_tier);
	char msg[128];
	int len = snprintf(msg, sizeof(msg),
		"{\"msg\":\"resolution\",\"p\":{\"min_interval_ms\":%d}}",
		min_interval_ms);

	syslog(LOG_INFO, "[ws] resolution changed: tier %d, min_interval_ms=%d\n",
	       pss->current_min_tier, min_interval_ms);

	memcpy(p, msg, len);
	lws_write(wsi, p, len, LWS_WRITE_TEXT);
}

/* Check and apply adaptive tier logic */
static void check_tier_adaptation(struct lws *wsi, unsigned char *p,
                                  struct per_session_data__jittertrap *pss)
{
	time_t now = time(NULL);

	/* Initialize window if not set */
	if (pss->window_start == 0) {
		pss->window_start = now;
		return;
	}

	/* Check if window has elapsed */
	if (now - pss->window_start < TIER_WINDOW_SECONDS) {
		return;
	}

	/* Collect stats from all subscribed queues */
	update_window_stats(pss);

	float drop_pct = calc_drop_percentage(pss);

	if (drop_pct > TIER_HIGH_WATERMARK && pss->current_min_tier < MQ_WS_NUM_TIERS) {
		/* Degrade: unsubscribe from fastest tier */
		syslog(LOG_INFO, "[ws] degrading tier %d -> %d (drop_pct=%.2f%%)\n",
		       pss->current_min_tier, pss->current_min_tier + 1, drop_pct * 100);
		unsubscribe_tier(pss, pss->current_min_tier);
		pss->current_min_tier++;
		send_resolution_update(wsi, p, pss);
	} else if (drop_pct < TIER_LOW_WATERMARK && pss->current_min_tier > 1) {
		/* Upgrade: subscribe to next faster tier */
		int new_tier = pss->current_min_tier - 1;
		int err = subscribe_tier(pss, new_tier);
		if (!err) {
			syslog(LOG_INFO, "[ws] upgrading tier %d -> %d (drop_pct=%.2f%%)\n",
			       pss->current_min_tier, new_tier, drop_pct * 100);
			pss->current_min_tier = new_tier;
			send_resolution_update(wsi, p, pss);
		}
	}

	/* Reset window */
	pss->drops_window = 0;
	pss->delivered_window = 0;
	pss->window_start = now;
}

/* Drain messages from subscribed queues in priority order (5 first, then 4, 3, 2, 1) */
static int drain_subscribed_queues(struct lws *wsi, unsigned char *p,
                                   struct per_session_data__jittertrap *pss,
                                   int *total_consumed)
{
	int err, cb_err;
	struct cb_data cbd = { wsi, p };

	*total_consumed = 0;

	/* Drain in reverse priority order: tier 5 (config/slow) first, tier 1 last */
	for (int tier = MQ_WS_NUM_TIERS; tier >= pss->current_min_tier; tier--) {
		int idx = tier - 1;
		if (pss->consumer_id[idx] == 0) continue;

		do {
			switch (tier) {
			case 1: err = mq_ws_1_consume(pss->consumer_id[0], lws_writer_1, &cbd, &cb_err); break;
			case 2: err = mq_ws_2_consume(pss->consumer_id[1], lws_writer_2, &cbd, &cb_err); break;
			case 3: err = mq_ws_3_consume(pss->consumer_id[2], lws_writer_3, &cbd, &cb_err); break;
			case 4: err = mq_ws_4_consume(pss->consumer_id[3], lws_writer_4, &cbd, &cb_err); break;
			case 5: err = mq_ws_5_consume(pss->consumer_id[4], lws_writer_5, &cbd, &cb_err); break;
			default: err = -JT_WS_MQ_EMPTY;
			}

			if (!err) {
				(*total_consumed)++;
			}

			/* Check for backpressure */
			if (lws_partial_buffered(wsi) || lws_send_pipe_choked(wsi)) {
				return 1;  /* Signal caller to request writeable callback */
			}
		} while (!err);
	}

	return 0;  /* All queues drained */
}

int callback_jittertrap(struct lws *wsi, enum lws_callback_reasons reason,
                        void *user, void *in, size_t len)
{
	unsigned char buf[LWS_SEND_BUFFER_PRE_PADDING + MAX_JSON_MSG_LEN +
	                  LWS_SEND_BUFFER_POST_PADDING];
	unsigned char *p = &buf[LWS_SEND_BUFFER_PRE_PADDING];
	struct per_session_data__jittertrap *pss =
	    (struct per_session_data__jittertrap *)user;

	int err;
	int total_consumed;

	/* run jt init, stats producer, etc. */
	jt_server_tick();

	switch (reason) {
	case LWS_CALLBACK_CLOSED:
		if (pss->consumer_id[0] == 0 && pss->consumer_id[4] == 0) {
			syslog(LOG_ERR, "no consumer to unsubscribe.\n");
		} else {
			unsubscribe_all_tiers(pss);
			consumer_count--;
			if (0 == consumer_count)
				jt_srv_pause();
		}
		break;

	case LWS_CALLBACK_ESTABLISHED:
		syslog(LOG_INFO, "callback_jt: LWS_CALLBACK_ESTABLISHED\n");

		/* Initialize per-session state */
		memset(pss, 0, sizeof(*pss));
		pss->window_start = time(NULL);

		/* Subscribe to tiered queues starting at tier 3 (20ms) */
		err = subscribe_initial_tiers(pss);
		if (err) {
			const char *errmsg;
			size_t errmsg_len;

			if (err == -JT_WS_MQ_CONSUMER_LIMIT) {
				errmsg = "{\"msg\":\"error\",\"p\":{\"code\":\"max_connections\",\"text\":\"Maximum concurrent connections reached. Please close other browser tabs or try again later.\"}}";
			} else {
				errmsg = "{\"msg\":\"error\",\"p\":{\"code\":\"subscribe_failed\",\"text\":\"Failed to subscribe to message queue.\"}}";
			}
			errmsg_len = strlen(errmsg);

			/* Send error message before closing */
			memcpy(p, errmsg, errmsg_len);
			lws_write(wsi, p, errmsg_len, LWS_WRITE_TEXT);

			syslog(LOG_WARNING,
			       "consumer subscription failed (err=%d), closing connection\n",
			       err);
			return -1;
		}
		consumer_count++;

		/* Only send state messages if server is already running.
		 * For first client, jt_server_tick() sends these after jt_init(). */
		if (jt_srv_is_ready()) {
			jt_srv_send_iface_list();
			jt_srv_send_select_iface();
			jt_srv_send_netem_params();
			jt_srv_send_sample_period();
			/* Send initial resolution (tier 1 = all data available) */
			send_resolution_update(wsi, p, pss);
		}
		jt_srv_resume();
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
		/* Check adaptive tier logic */
		check_tier_adaptation(wsi, p, pss);

		/* Drain messages from subscribed queues */
		if (drain_subscribed_queues(wsi, p, pss, &total_consumed)) {
			/* Backpressure detected, request another callback */
			lws_callback_on_writable(wsi);
		}
		break;

	case LWS_CALLBACK_RECEIVE:
		{
			/*
			 * Handle fragmented WebSocket messages.
			 * Large messages (like Chromium's SDP offers ~7KB) get split
			 * into multiple fragments. We accumulate them until we see
			 * the final fragment.
			 */
			size_t remaining = sizeof(pss->rx_buf) - pss->rx_len;
			if (len >= remaining) {
				syslog(LOG_ERR, "WebSocket message too large (%zu + %zu >= %zu), dropping\n",
				       pss->rx_len, len, sizeof(pss->rx_buf));
				pss->rx_len = 0;
				break;
			}

			/* Append this fragment to the buffer */
			memcpy(pss->rx_buf + pss->rx_len, in, len);
			pss->rx_len += len;

			/* Check if this is the final fragment */
			if (lws_is_final_fragment(wsi)) {
				/* Complete message received, process it */
				jt_server_msg_receive(pss->rx_buf, pss->rx_len);
				pss->rx_len = 0;
			}
		}
		break;

	/*
	 * this just demonstrates how to use the protocol filter. If you won't
	 * study and reject connections based on header content, you don't need
	 * to handle this callback
	 */
	case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
		dump_handshake_info(wsi);
		/* you could return non-zero here and kill the connection */
		break;

	default:
		break;
	}

	return 0;
}
