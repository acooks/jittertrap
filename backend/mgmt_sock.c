#define _POSIX_C_SOURCE 200809L
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <inttypes.h>
#include <string.h>
#include <math.h>

#include "fossa.h"

#include "jittertrap.h"
#include "iface_stats.h"
#include "netem.h"
#include "mgmt_sock.h"

#define QUOTE(str) #str
#define EXPAND_AND_QUOTE(str) QUOTE(str)

static pthread_mutex_t fossa_mutex;

static struct ns_serve_http_opts s_http_server_opts = {
	.document_root = NULL,
	.per_directory_auth_file = NULL,
	.auth_domain = NULL,
	.global_auth_file = NULL
};

struct ns_mgr mgr;
struct ns_connection *nc;

static void print_ns_str(const struct ns_str *s)
{
	assert(s);
	char ss[s->len + 1];
	memcpy(ss, s->p, s->len);
	ss[s->len] = '\0';
	printf("%s\n", ss);
}

static void print_websocket_message(const struct websocket_message *m)
{
	assert(m);
	char s[m->size + 1];
	memcpy(s, m->data, m->size);
	s[m->size] = '\0';
	printf("websocket_message: [%s]\n", s);
}

static bool match_msg_type(const struct json_token *tok, const char *r)
{
	if (!tok)
		return false;
	if (tok->type != JSON_TYPE_STRING)
		return false;
	return (strncmp(tok->ptr, r, tok->len) == 0);
}

/* json_arr_alloc: must free returned memory */
static char *json_arr_alloc()
{
	char *buf;

	buf = malloc(3);
	assert(NULL != buf);
	buf[0] = '[';
	buf[1] = ']';
	buf[2] = '\0';
	return buf;
}

/* str MUST be a malloc'ed pointer */
static char * quote_string(char *str)
{
	char *s;
	assert(str);
	s = strdup(str);
	assert(s);
	str = realloc(str, strlen(str) + 3);
	assert(str);
	snprintf(str, strlen(str)+3, "\"%s\"", s);
	free(s);
	return str;
}

/* *arr MUST be a malloc'ed pointer */
static void json_arr_append(char **arr, const char *const word)
{
	assert(NULL != arr);
	assert(NULL != *arr);
	assert(NULL != word);

	int buf_len = strlen(*arr);
	int word_len = strlen(word);

	/* comma, space, nul term */
	*arr = realloc(*arr, buf_len + word_len + 2 + 1);
	assert(NULL != *arr);

	if (buf_len >= 3) {
		memcpy((*arr) + buf_len - 1, ", ", 2);
		buf_len += 2;
	}
	memcpy(*arr + buf_len - 1, word, word_len);
	(*arr)[buf_len + word_len - 1] = ']';
	(*arr)[buf_len + word_len] = 0;
}

/* list_ifaces: must free returned memory */
static char *list_ifaces()
{
	char *json_ifaces = json_arr_alloc();
	char **ifaces = netem_list_ifaces();
	char **i = ifaces;
	assert(NULL != i);
	if (NULL == *i) {
		fprintf(stderr,
		        "No interfaces available. "
		        "Allowed interfaces (compile-time): %s\n",
		        EXPAND_AND_QUOTE(ALLOWED_IFACES));
	} else {
		do {
			*i = quote_string(*i);
			json_arr_append(&json_ifaces, *i);
			free(*i);
			i++;
		} while (*i);
	}
	free(ifaces);

	char *head = "{\"msg\": \"iface_list\", \"p\":{\"ifaces\":";
	char *tail = "}}";
	char *msg =
	    malloc(strlen(head) + strlen(json_ifaces) + strlen(tail) + 1);
	assert(NULL != msg);
	*msg = 0;
	strncat(msg, head, strlen(head));
	strncat(msg, json_ifaces, strlen(json_ifaces));
	strncat(msg, tail, strlen(tail));
	free(json_ifaces);
	return msg;
}

static void ws_send_iface_list(struct ns_connection *nc)
{
	struct ns_connection *c;
	char *buf = list_ifaces();
	printf("matched list_ifaces. ifaces:[%s]\n", buf);
	for (c = ns_next(nc->mgr, NULL); c != NULL; c = ns_next(nc->mgr, c)) {
		ns_send_websocket_frame(c, WEBSOCKET_OP_TEXT, buf, strlen(buf));
	}
	free(buf);
}

static void ws_send_dev_select(struct ns_connection *nc)
{
	struct ns_connection *c;
	char msg[MAX_JSON_MSG_LEN] = { 0 };
	char *template = "{\"msg\":\"dev_select\", \"p\":{\"iface\":\"%s\"}}";
	snprintf(msg, MAX_JSON_MSG_LEN, template, jt_get_iface());

	for (c = ns_next(nc->mgr, NULL); c != NULL; c = ns_next(nc->mgr, c)) {
		ns_send_websocket_frame(c, WEBSOCKET_OP_TEXT, msg, strlen(msg));
	}
}

static void ws_send_netem(struct ns_connection *nc, char const *iface)
{
	struct ns_connection *c;
	struct netem_params p;

	printf("get netem for iface: [%s]\n", iface);
	memcpy(p.iface, iface, MAX_IFACE_LEN);
	if (0 != netem_get_params(p.iface, &p)) {
		fprintf(stderr, "couldn't get netem parameters.\n");
		p.delay = -1;
		p.jitter = -1;
		p.loss = -1;
	}
	char *template =
	    "{\"msg\":\"netem_params\", \"p\":"
	    "{\"iface\":\"%.10s\", \"delay\":%d, \"jitter\":%d, \"loss\":%d}}";

	char msg[MAX_JSON_MSG_LEN] = { 0 };

	sprintf(msg, template, p.iface, p.delay, p.jitter, p.loss);
	printf("%s\n", msg);
	for (c = ns_next(nc->mgr, NULL); c != NULL; c = ns_next(nc->mgr, c)) {
		ns_send_websocket_frame(c, WEBSOCKET_OP_TEXT, msg, strlen(msg));
	}
}

static void ws_send_sample_period(struct ns_connection *nc)
{
	struct ns_connection *c;
	char *template = "{\"msg\": \"sample_period\", \"p\":{\"period\":%d}}";
	char msg[200] = { 0 };
	sprintf(msg, template, jt_get_sample_period());
	printf("%s\n", msg);
	for (c = ns_next(nc->mgr, NULL); c != NULL; c = ns_next(nc->mgr, c)) {
		ns_send_websocket_frame(c, WEBSOCKET_OP_TEXT, msg, strlen(msg));
	}
}

static void handle_ws_dev_select(struct json_token *tok)
{
	char iface[MAX_IFACE_LEN];

	assert(tok->len < MAX_IFACE_LEN);
	memset(iface, 0, MAX_IFACE_LEN);
	memcpy(iface, tok->ptr, tok->len);
	if (0 != jt_set_iface(iface)) {
		fprintf(stderr, "couldn't set iface to %s\n", iface);
		return;
	}
	ws_send_dev_select(nc);
	ws_send_netem(nc, iface);
	ws_send_sample_period(nc);
}

static void handle_ws_get_netem(struct ns_connection *nc,
				struct json_token *tok)
{
	char iface[MAX_IFACE_LEN];

	if (tok->len >= MAX_IFACE_LEN) {
		fprintf(stderr, "invalid iface name.");
		return;
	}
	memcpy(iface, tok->ptr, tok->len);
	iface[tok->len] = 0;

	ws_send_netem(nc, iface);
}

static bool parse_int(char *str, long *l)
{
	char *endptr;
	errno = 0;		/* To distinguish success/failure after call */

	long val = strtol(str, &endptr, 10);

	/* Check for various possible errors */
	if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
	    || (errno != 0 && val == 0)) {
		perror("strtol");
		return false;
	}

	if (endptr == str) {
		fprintf(stderr, "No digits were found\n");
		return false;
	}

	/* success! */
	*l = val;
	return true;
}

#define json_token_to_string(tok, str, strlen) \
{ \
	assert(str); \
	assert(*str); \
	(*str)[0] = '\0'; \
	if (strlen > tok->len) { \
		memcpy(*str, tok->ptr, tok->len); \
		(*str)[tok->len] = 0; \
	} \
}

static void handle_ws_set_netem(struct ns_connection *nc,
				struct json_token *t_dev,
				struct json_token *t_delay,
				struct json_token *t_jitter,
				struct json_token *t_loss)
{
	char s[MAX_JSON_TOKEN_LEN];
	long delay, jitter, loss;
	struct netem_params p = {
		.delay  = 0,
		.jitter = 0,
		.loss   = 0,
		.iface  = {0}
	};

	json_token_to_string(t_dev, &p.iface, MAX_IFACE_LEN);
	printf("set_netem: dev: %s, ", p.iface);

	json_token_to_string(t_delay, &s, MAX_JSON_TOKEN_LEN);
	if (!parse_int(s, &delay)) {
		fprintf(stderr, "couldn't parse delay\n");
		return;
	}
	p.delay = delay;
	printf("delay: %ld, ", delay);

	json_token_to_string(t_jitter, &s, MAX_JSON_TOKEN_LEN);
	if (!parse_int(s, &jitter)) {
		fprintf(stderr, "couldn't parse jitter\n");
		return;
	}
	p.jitter = jitter;
	printf("jitter: %ld, ", jitter);

	json_token_to_string(t_loss, &s, MAX_JSON_TOKEN_LEN);
	if (!parse_int(s, &loss)) {
		fprintf(stderr, "couldn't parse loss\n");
		return;
	}
	p.loss = loss;
	printf("loss: %ld\n", loss);
	printf("\n\n");

	netem_set_params(p.iface, &p);
	handle_ws_get_netem(nc, t_dev);
}

static void handle_ws_message(struct ns_connection *nc,
			      const struct websocket_message *m)
{
	print_websocket_message(m);
	struct json_token *arr, *tok, *params;

	arr = parse_json2((const char *)m->data, m->size);
	if (NULL == arr) {
		return;		/* not valid JSON. */
	}

/* expected json looks like:
 * {'msg':'list_ifaces', 'p':{}}
 *     OR
 * {'msg': 'get_netem', 'p':{'dev': 'eth0'}}
 */

	const char *key = "msg";
	tok = find_json_token(arr, key);
	params = find_json_token(arr, "p");
	if (!tok || !params) {
		char *foo = malloc(255);
		snprintf(foo, 255, "%s", m->data);
		fprintf(stderr, "Not a recognised message: %s\n", foo);
		free(arr);
		free(foo);
		return;
	}

	if (match_msg_type(tok, "dev_select")) {
		tok = find_json_token(arr, "p.dev");
		handle_ws_dev_select(tok);
	} else if (match_msg_type(tok, "get_netem")) {
		tok = find_json_token(arr, "p.dev");
		handle_ws_get_netem(nc, tok);
	} else if (match_msg_type(tok, "set_netem")) {
		handle_ws_set_netem(nc,
				find_json_token(arr, "p.dev"),
				find_json_token(arr, "p.delay"),
				find_json_token(arr, "p.jitter"),
				find_json_token(arr, "p.loss"));
	}

	free(arr);
}

static int is_websocket(struct ns_connection *c)
{
	return c->flags & NSF_IS_WEBSOCKET;
}

static void print_peer_name(struct ns_connection *c)
{
	struct sockaddr_storage addr;
	char ipstr[INET6_ADDRSTRLEN];
	socklen_t len = sizeof addr;

	if (getpeername(c->sock, (struct sockaddr *)&addr, &len) < 0) {
	    fprintf(stderr, "Error: print_peer_name: cannot get address\n");
	    return;
	}

	/* deal with both IPv4 and IPv6: */
	if (addr.ss_family == AF_INET) {
		struct sockaddr_in *s = (struct sockaddr_in *)&addr;
		inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
	} else {
		struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;
		inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof ipstr);
	}
	printf("Peer IP address: %s\n", ipstr);
}

static void ev_handler(struct ns_connection *nc, int ev, void *ev_data)
{
	struct http_message *hm = (struct http_message *)ev_data;
	struct websocket_message *wm = (struct websocket_message *)ev_data;
	pthread_mutex_lock(&fossa_mutex);
	switch (ev) {
	case NS_HTTP_REQUEST:
		/* keep this simple. no REST. serve only index.html.
		 * everything else is done through websockets.*/
		print_ns_str(&hm->message);
		ns_serve_http(nc, hm, s_http_server_opts);
		break;
	case NS_WEBSOCKET_HANDSHAKE_DONE:
		print_peer_name(nc);
		ws_send_iface_list(nc);
		ws_send_dev_select(nc);
		ws_send_netem(nc, jt_get_iface());
		ws_send_sample_period(nc);
		break;
	case NS_WEBSOCKET_FRAME:
		handle_ws_message(nc, wm);
		break;
	case NS_CLOSE:
		if (is_websocket(nc)) {
			/* TODO: if nobody is listening, stop threads. */
			;
		}
		break;
	default:
		break;
	}
	pthread_mutex_unlock(&fossa_mutex);
}


void mgmt_sock_stats_send(char *json_msg)
{
        struct ns_connection *c;
        pthread_mutex_lock(&fossa_mutex);

        for (c = ns_next(nc->mgr, NULL); c != NULL; c = ns_next(nc->mgr, c)) {
                if (is_websocket(c)) {
                        ns_printf_websocket_frame(c,
                                                  WEBSOCKET_OP_TEXT,
                                                  "%s", json_msg);
                }
        }
        pthread_mutex_unlock(&fossa_mutex);
}


int mgmt_sock_init(const char *http_port, const char *docroot)
{
	struct ns_bind_opts opts = {.flags = TCP_NODELAY };
	s_http_server_opts.document_root = docroot;

	ns_mgr_init(&mgr, NULL);
	nc = ns_bind_opt(&mgr, http_port, ev_handler, opts);
	if (nc == NULL) {
		fprintf(stderr, "Couldn't bind to port:%s\n", http_port);
		return -1;
	}
	ns_set_protocol_http_websocket(nc);
	return 0;
}

int mgmt_sock_main(void (*cb)(void), int period)
{
	for (;;) {
		ns_mgr_poll(&mgr, period);
		cb();
	}
	ns_mgr_free(&mgr);

	return 0;
}
