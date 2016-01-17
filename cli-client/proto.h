#ifndef PROTO_H
#define PROTO_H

enum protocol_ids {

	PROTOCOL_JITTERTRAP,

	/* always last */
	DEMO_PROTOCOL_COUNT
};

/* jittertrap protocol */
int callback_jittertrap(struct lws *wsi,
                        enum lws_callback_reasons reason,
                        void *user __attribute__((unused)),
                        void *in __attribute__((unused)), size_t len);

/* list of supported protocols and callbacks */
extern struct lws_protocols protocols[];

#endif
