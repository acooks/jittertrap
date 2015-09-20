#ifndef PROTO_MIRROR_H
#define PROTO_MIRROR_H

/* lws-mirror_protocol */
#include "proto-mirror.h"

#define MAX_MESSAGE_QUEUE 32

struct per_session_data__lws_mirror
{
	struct libwebsocket *wsi;
	int ringbuffer_tail;
};

struct a_message
{
	void *payload;
	size_t len;
};

int callback_lws_mirror(struct libwebsocket_context *context,
                        struct libwebsocket *wsi,
                        enum libwebsocket_callback_reasons reason, void *user,
                        void *in, size_t len);

#endif
