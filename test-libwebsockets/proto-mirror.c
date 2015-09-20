/* lws-mirror_protocol */

#include <unistd.h>

#include <libwebsockets.h>

#include "proto-mirror.h"
#include "test.h"

static struct a_message ringbuffer[MAX_MESSAGE_QUEUE];
static int ringbuffer_head;

int callback_lws_mirror(struct libwebsocket_context *context,
                        struct libwebsocket *wsi,
                        enum libwebsocket_callback_reasons reason, void *user,
                        void *in, size_t len)
{
	int n;
	struct per_session_data__lws_mirror *pss =
	    (struct per_session_data__lws_mirror *)user;

	switch (reason) {

	case LWS_CALLBACK_ESTABLISHED:
		lwsl_info("callback_lws_mirror: LWS_CALLBACK_ESTABLISHED\n");
		pss->ringbuffer_tail = ringbuffer_head;
		pss->wsi = wsi;
		break;

	case LWS_CALLBACK_PROTOCOL_DESTROY:
		lwsl_notice("mirror protocol cleaning up\n");
		for (n = 0; (unsigned int)n <
		                (sizeof ringbuffer / sizeof ringbuffer[0]);
		     n++)
			if (ringbuffer[n].payload)
				free(ringbuffer[n].payload);
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
		if (close_testing)
			break;
		while (pss->ringbuffer_tail != ringbuffer_head) {

			n = libwebsocket_write(
			    wsi, (unsigned char *)
			             ringbuffer[pss->ringbuffer_tail].payload +
			             LWS_SEND_BUFFER_PRE_PADDING,
			    ringbuffer[pss->ringbuffer_tail].len,
			    LWS_WRITE_TEXT);
			if (n < 0) {
				lwsl_err("ERROR %d writing to mirror socket\n",
				         n);
				return -1;
			}
			if ((unsigned int)n <
			    ringbuffer[pss->ringbuffer_tail].len)
				lwsl_err("mirror partial write %d vs %d\n", n,
				         ringbuffer[pss->ringbuffer_tail].len);

			if (pss->ringbuffer_tail == (MAX_MESSAGE_QUEUE - 1))
				pss->ringbuffer_tail = 0;
			else
				pss->ringbuffer_tail++;

			if (((ringbuffer_head - pss->ringbuffer_tail) &
			     (MAX_MESSAGE_QUEUE - 1)) ==
			    (MAX_MESSAGE_QUEUE - 15))
				libwebsocket_rx_flow_allow_all_protocol(
				    libwebsockets_get_protocol(wsi));

			// lwsl_debug("tx fifo %d\n", (ringbuffer_head -
			// pss->ringbuffer_tail) & (MAX_MESSAGE_QUEUE - 1));

			if (lws_partial_buffered(wsi) ||
			    lws_send_pipe_choked(wsi)) {
				libwebsocket_callback_on_writable(context, wsi);
				break;
			}
		}
		break;

	case LWS_CALLBACK_RECEIVE:

		if (((ringbuffer_head - pss->ringbuffer_tail) &
		     (MAX_MESSAGE_QUEUE - 1)) == (MAX_MESSAGE_QUEUE - 1)) {
			lwsl_err("dropping!\n");
			goto choke;
		}

		if (ringbuffer[ringbuffer_head].payload)
			free(ringbuffer[ringbuffer_head].payload);

		ringbuffer[ringbuffer_head].payload =
		    malloc(LWS_SEND_BUFFER_PRE_PADDING + len +
		           LWS_SEND_BUFFER_POST_PADDING);
		ringbuffer[ringbuffer_head].len = len;
		memcpy((char *)ringbuffer[ringbuffer_head].payload +
		           LWS_SEND_BUFFER_PRE_PADDING,
		       in, len);
		if (ringbuffer_head == (MAX_MESSAGE_QUEUE - 1))
			ringbuffer_head = 0;
		else
			ringbuffer_head++;

		if (((ringbuffer_head - pss->ringbuffer_tail) &
		     (MAX_MESSAGE_QUEUE - 1)) != (MAX_MESSAGE_QUEUE - 2))
			goto done;

	choke:
		lwsl_debug("LWS_CALLBACK_RECEIVE: throttling %p\n", wsi);
		libwebsocket_rx_flow_control(wsi, 0);

	//		lwsl_debug("rx fifo %d\n", (ringbuffer_head -
	// pss->ringbuffer_tail) & (MAX_MESSAGE_QUEUE - 1));
	done:
		libwebsocket_callback_on_writable_all_protocol(
		    libwebsockets_get_protocol(wsi));
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
