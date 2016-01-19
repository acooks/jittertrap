#ifndef TEST_H
#define TEST_H

enum protocol_ids {
	/* always first */
	PROTOCOL_HTTP = 0,

	PROTOCOL_JITTERTRAP,

	/* always last */
	PROTOCOL_TERMINATOR
};

int close_testing;

char *resource_path;

void dump_handshake_info(struct lws *wsi);

#endif
