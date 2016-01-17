#ifndef PROTO_HTTP_H
#define PROTO_HTTP_H

struct per_session_data__http {
	int fd;
};

int callback_http(struct lws *wsi, enum lws_callback_reasons reason, void *user,
                  void *in, size_t len);

#endif
