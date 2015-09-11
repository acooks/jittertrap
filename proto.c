#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <libwebsockets.h>

#include "proto.h"

/* jittertrap protocol */
int callback_jittertrap(struct libwebsocket_context *context,
                        struct libwebsocket *wsi,
                        enum libwebsocket_callback_reasons reason,
                        void *user __attribute__((unused)),
                        void *in __attribute__((unused)), size_t len)
{
	switch (reason) {

	case LWS_CALLBACK_CLIENT_ESTABLISHED:
		fprintf(stderr, "callback_jittertrap:"
		                " LWS_CALLBACK_CLIENT_ESTABLISHED\n");

		/*
		 * start the ball rolling,
		 * LWS_CALLBACK_CLIENT_WRITEABLE will come next service
		 */

		libwebsocket_callback_on_writable(context, wsi);
		break;

	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		fprintf(stderr, "LWS_CALLBACK_CLIENT_CONNECTION_ERROR\n");
		// was_closed = 1;
		return -1;
		break;

	case LWS_CALLBACK_CLOSED:
		fprintf(stderr, "LWS_CALLBACK_CLOSED\n");
		// was_closed = 1;
		return -1;
		break;

	case LWS_CALLBACK_CLIENT_RECEIVE:
		fprintf(stderr, "\rrx %d '%s'", (int)len, (char *)in);
		break;

	case LWS_CALLBACK_CLIENT_WRITEABLE:
		libwebsocket_callback_on_writable(context, wsi);
		break;

	default:
		/* fprintf(stderr, "callback reason: %d\n", reason); */
		break;
	}

	return 0;
}
