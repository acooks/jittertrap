#ifndef PROTO_H
#define PROTO_H

enum protocol_ids {

	PROTOCOL_JITTERTRAP,

	/* always last */
	DEMO_PROTOCOL_COUNT
};

/* jittertrap protocol */
int callback_jittertrap(struct libwebsocket_context *context,
		struct libwebsocket *wsi,
		enum libwebsocket_callback_reasons reason,
		void *user __attribute__((unused)),
		void *in __attribute__((unused)), size_t len);

/* list of supported protocols and callbacks */
static struct libwebsocket_protocols protocols[] = {
	[PROTOCOL_JITTERTRAP] = {
	  .name = "",
	  .callback = callback_jittertrap,
	  .per_session_data_size = 0,
	  .rx_buffer_size = 512,
	},
	{ NULL, NULL, 0, 0, 0, NULL, NULL, 0 } /* end */
};

#endif
