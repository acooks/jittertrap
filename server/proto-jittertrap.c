/* jittertrap protocol */

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <syslog.h>
#include <libwebsockets.h>

#include "proto.h"
#include "jt_server_message_handler.h"

#include "mq_msg_ws.h"

#include "proto-jittertrap.h"

struct cb_data {
	struct lws *wsi;
	unsigned char *buf;
};

static int lws_writer(struct mq_ws_msg *m, void *data)
{
	int len, n;
	struct cb_data *d = (struct cb_data *)data;
	assert(d);
	len = snprintf((char *)d->buf, MAX_JSON_MSG_LEN, "%s", m->m);
	assert(len >= 0);
	if (len > 0) {
		n = lws_write(d->wsi, d->buf, len, LWS_WRITE_TEXT);
		if (n < len) {
			/* short write :( */
			fprintf(stderr, "Short write :(\n");
			return -1;
		}
	}
	return 0;
}

int callback_jittertrap(struct lws *wsi, enum lws_callback_reasons reason,
                        void *user, void *in, size_t len)
{
	unsigned char buf[LWS_SEND_BUFFER_PRE_PADDING + MAX_JSON_MSG_LEN +
	                  LWS_SEND_BUFFER_POST_PADDING];
	unsigned char *p = &buf[LWS_SEND_BUFFER_PRE_PADDING];
	struct per_session_data__jittertrap *pss =
	    (struct per_session_data__jittertrap *)user;

	int err, cb_err;
	struct cb_data cbd = { wsi, p };

	/* run jt init, stats producer, etc. */
	jt_server_tick();

	switch (reason) {
	case LWS_CALLBACK_CLOSED:
		if (!pss->consumer_id) {
			syslog(LOG_ERR, "no consumer to unsubscribe.\n");
		} else {
			err = mq_ws_consumer_unsubscribe(pss->consumer_id);
			if (err) {
				syslog(LOG_ERR,
				       "mq consumer unsubscribe failed.\n");
			}
		}
		break;

	case LWS_CALLBACK_ESTABLISHED:
		syslog(LOG_INFO, "callback_jt: LWS_CALLBACK_ESTABLISHED\n");
		err = mq_ws_consumer_subscribe(&(pss->consumer_id));
		if (err) {
			syslog(LOG_ERR, "mq consumer subscription failed.\n");
			return -1;
		}
		jt_srv_send_iface_list();
		jt_srv_send_select_iface();
		jt_srv_send_netem_params();
		jt_srv_send_sample_period();
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
		do {
			err = mq_ws_consume(pss->consumer_id, lws_writer, &cbd,
			                    &cb_err);
			if (lws_partial_buffered(wsi) ||
			    lws_send_pipe_choked(wsi)) {
				lws_callback_on_writable(wsi);
				break;
			}
		} while (!err);

		if (-JT_WS_MQ_EMPTY != err) {
			return err;
		}
		break;

	case LWS_CALLBACK_RECEIVE:
		jt_server_msg_receive(in, len);
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
