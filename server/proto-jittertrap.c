/* jittertrap protocol */

#include <unistd.h>

#include <libwebsockets.h>

#include "proto.h"
#include "jt_server_message_handler.h"
#include "websocket_message_queue.h"
#include "proto-jittertrap.h"

struct cb_data
{
	struct libwebsocket *wsi;
	unsigned char *buf;
};

static int lws_writer(struct jt_ws_msg *m, void *data)
{
	int len, n;
	struct cb_data *d = (struct cb_data *)data;
	assert(d);
	len = snprintf((char *)d->buf, MAX_JSON_MSG_LEN, "%s", m->m);
	assert(len >= 0);
	if (len > 0) {
		n = libwebsocket_write(d->wsi, d->buf, len, LWS_WRITE_TEXT);
		if (n < len) {
			/* short write :( */
			return -1;
		}
	}
	return 0;
}

int callback_jittertrap(struct libwebsocket_context *context
                        __attribute__((unused)),
                        struct libwebsocket *wsi,
                        enum libwebsocket_callback_reasons reason, void *user,
                        void *in, size_t len)
{
	unsigned char buf[LWS_SEND_BUFFER_PRE_PADDING + MAX_JSON_MSG_LEN +
	                  LWS_SEND_BUFFER_POST_PADDING];
	unsigned char *p = &buf[LWS_SEND_BUFFER_PRE_PADDING];
	struct per_session_data__jittertrap *pss =
	    (struct per_session_data__jittertrap *)user;

	int err;
	struct cb_data cbd = { wsi, p };

	/* run jt init, stats producer, response handlers, etc. */
	jt_server_tick();

	switch (reason) {

	case LWS_CALLBACK_ESTABLISHED:
		lwsl_info("callback_jt: "
		          "LWS_CALLBACK_ESTABLISHED\n");
		pss->number = 0;
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
		do {
			err = jt_ws_mq_consume(lws_writer, &cbd);
		} while (!err);
		break;

	case LWS_CALLBACK_RECEIVE:
		jt_server_msg_receive(in);
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
