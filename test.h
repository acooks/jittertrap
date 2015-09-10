#ifndef TEST_H
#define TEST_H

enum demo_protocols {
	/* always first */
	PROTOCOL_HTTP = 0,

	PROTOCOL_DUMB_INCREMENT,
	PROTOCOL_LWS_MIRROR,

	/* always last */
	DEMO_PROTOCOL_COUNT
};

int close_testing;

#define LOCAL_RESOURCE_PATH INSTALL_DATADIR "/libwebsockets-test-server"
char *resource_path;


void dump_handshake_info(struct libwebsocket *wsi);

#endif
