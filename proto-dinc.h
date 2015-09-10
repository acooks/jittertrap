#ifndef PROTO_DINC_H
#define PROTO_DINC_H

/* dumb_increment protocol */

/*
 * one of these is auto-created for each connection and a pointer to the
 * appropriate instance is passed to the callback in the user parameter
 *
 * for this example protocol we use it to individualize the count for each
 * connection.
 */

struct per_session_data__dumb_increment
{
	int number;
};

int callback_dumb_increment(struct libwebsocket_context *context,
                            struct libwebsocket *wsi,
                            enum libwebsocket_callback_reasons reason,
                            void *user, void *in, size_t len);
#endif
