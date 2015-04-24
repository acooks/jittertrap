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
#include "frozen.h"
#include "jittertrap.h"
#include "stats_thread.h"
#include "netem.h"

#define QUOTE(str) #str
#define EXPAND_AND_QUOTE(str) QUOTE(str)

static pthread_mutex_t fossa_mutex;
static pthread_mutex_t unsent_frame_count_mutex;

struct iface_stats *g_raw_samples;
int g_unsent_frame_count = 0;


static const char *s_http_port = EXPAND_AND_QUOTE(WEB_SERVER_PORT);
static struct ns_serve_http_opts s_http_server_opts = {
	.document_root = EXPAND_AND_QUOTE(WEB_SERVER_DOCUMENT_ROOT),
	.per_directory_auth_file = NULL,
	.auth_domain = NULL,
	.global_auth_file = NULL
};

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

/* json_arr_alocc: must free returned memory */
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
char * quote_string(char *str)
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
};

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
		free(ifaces);
	}

	char *head = "{\"ifaces\":";
	char *tail = "}";
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

static void handle_ws_list_ifaces(struct ns_connection *nc)
{
	struct ns_connection *c;
	char *buf = list_ifaces();
	printf("matched list_ifaces. ifaces:[%s]\n", buf);
	for (c = ns_next(nc->mgr, NULL); c != NULL; c = ns_next(nc->mgr, c)) {
		ns_send_websocket_frame(c, WEBSOCKET_OP_TEXT, buf, strlen(buf));
	}
	free(buf);
}

static void handle_ws_dev_select(struct json_token *tok)
{
	char iface[MAX_IFACE_LEN];

	assert(tok->len < MAX_IFACE_LEN);
	memset(iface, 0, MAX_IFACE_LEN);
	memcpy(iface, tok->ptr, tok->len);
	if (!is_iface_allowed(iface)) {
		printf("ignoring request to switch to iface: [%s] - "
		       "iface not in allowed list: [%s]\n",
		       iface, EXPAND_AND_QUOTE(ALLOWED_IFACES));
		return;
	}
	printf("switching to iface: [%s]\n", iface);
	stats_monitor_iface(iface);
}

static void handle_ws_get_netem(struct ns_connection *nc,
				struct json_token *tok)
{
	struct ns_connection *c;
	struct netem_params p;

	if (tok->len >= MAX_IFACE_LEN) {
		fprintf(stderr, "invalid iface name.");
		return;
	}
	memcpy(p.iface, tok->ptr, tok->len);
	p.iface[tok->len] = 0;
	printf("get netem for iface: [%s]\n", p.iface);
	if (0 != netem_get_params(p.iface, &p)) {
		fprintf(stderr, "couldn't get netem parameters.\n");
		p.delay = -1;
		p.jitter = -1;
		p.loss = -1;
	}

	char *template =
	    "{\"netem_params\":"
	    "{\"iface\":\"%.10s\", \"delay\":%d, \"jitter\":%d, \"loss\":%d}}";
	char msg[200] = { 0 };
	sprintf(msg, template, p.iface, p.delay, p.jitter, p.loss);
	printf("%s\n", msg);
	for (c = ns_next(nc->mgr, NULL); c != NULL; c = ns_next(nc->mgr, c)) {
		ns_send_websocket_frame(c, WEBSOCKET_OP_TEXT, msg, strlen(msg));
	}
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

static void handle_ws_get_period(struct ns_connection *nc)
{
	struct ns_connection *c;
	char *template = "{\"sample_period\":%d}";
	char msg[200] = { 0 };
	sprintf(msg, template, get_sample_period());
	printf("%s\n", msg);
	for (c = ns_next(nc->mgr, NULL); c != NULL; c = ns_next(nc->mgr, c)) {
		ns_send_websocket_frame(c, WEBSOCKET_OP_TEXT, msg, strlen(msg));
	}
}

static void handle_ws_set_period(struct ns_connection *nc,
				 struct json_token *tok)
{
	long period;
	char s[MAX_JSON_TOKEN_LEN];
	json_token_to_string(tok, &s, MAX_JSON_TOKEN_LEN);
        if (!parse_int(s, &period)) {
                fprintf(stderr, "couldn't parse period\n");
                return;
        }
        printf("setting sample period: %ld, ", period);
	set_sample_period(period);
	handle_ws_get_period(nc);
}

static void handle_ws_message(struct ns_connection *nc,
			      const struct websocket_message *m)
{
	print_websocket_message(m);
	struct json_token *arr, *tok;

	arr = parse_json2((const char *)m->data, m->size);
	if (NULL == arr) {
		return;		/* not valid JSON. */
	}

/* expected json looks like:
 * {'msg':'list_ifaces'}
 *     OR
 * {'msg': 'get_netem', 'dev': 'eth0' }
 */

	const char *key = "msg";
	tok = find_json_token(arr, key);
	if (tok) {
		if (match_msg_type(tok, "list_ifaces")) {
			handle_ws_list_ifaces(nc);
		} else if (match_msg_type(tok, "dev_select")) {
			tok = find_json_token(arr, "dev");
			handle_ws_dev_select(tok);
		} else if (match_msg_type(tok, "get_netem")) {
			tok = find_json_token(arr, "dev");
			handle_ws_get_netem(nc, tok);
		} else if (match_msg_type(tok, "set_netem")) {
			handle_ws_set_netem(nc,
					    find_json_token(arr, "dev"),
					    find_json_token(arr, "delay"),
					    find_json_token(arr, "jitter"),
					    find_json_token(arr, "loss"));
		} else if (match_msg_type(tok, "set_sample_period")) {
			tok = find_json_token(arr, "period");
			handle_ws_set_period(nc, tok);
		} else if (match_msg_type(tok, "get_sample_period")) {
			handle_ws_get_period(nc);
		}
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

static void stats_to_json(struct iface_stats *s, char json_msg[]) {

	char *m = json_arr_alloc();

	int i;
	for (i = 0; i < FILTERED_SAMPLES_PER_MSG; i++) {
		char msg[MAX_JSON_MSG_LEN];
		snprintf(msg,
			 MAX_JSON_MSG_LEN,
			 "{"
			 "\"rxDelta\":%" PRId64 ","
			 "\"txDelta\":%" PRId64 ","
			 "\"rxPktDelta\":%" PRId64 ","
			 "\"txPktDelta\":%" PRId64 ""
			 "}",
			 s->samples[i].rx_bytes_delta,
			 s->samples[i].tx_bytes_delta,
			 s->samples[i].rx_packets_delta,
			 s->samples[i].tx_packets_delta);
		json_arr_append(&m, msg);
	}

	snprintf(json_msg,
		 MAX_JSON_MSG_LEN,
		 "{\"stats\": {\"iface\": \"%s\",\"s\": %s}}",
		 s->iface,
		 m);
	free(m);
}

inline static void stats_send(struct iface_stats *samples)
{
	struct ns_connection *c;
	pthread_mutex_lock(&fossa_mutex);

	char *json_msg = malloc(MAX_JSON_MSG_LEN);
	assert(json_msg);

	stats_to_json(samples, json_msg);

	for (c = ns_next(nc->mgr, NULL); c != NULL; c = ns_next(nc->mgr, c)) {
		if (is_websocket(c)) {
			ns_printf_websocket_frame(c,
						  WEBSOCKET_OP_TEXT,
						  "%s", json_msg);
		}
	}
	pthread_mutex_unlock(&fossa_mutex);
	free(json_msg);
}

static void stats_filter_and_send()
{
	pthread_mutex_lock(&unsent_frame_count_mutex);
	if (g_unsent_frame_count > 0) {
		stats_send(g_raw_samples);
		g_unsent_frame_count--;
	}
	pthread_mutex_unlock(&unsent_frame_count_mutex);
}

/* callback for the real-time stats thread. */
void stats_event_handler(struct iface_stats *raw_samples)
{
	pthread_mutex_lock(&unsent_frame_count_mutex);
	g_raw_samples = raw_samples;
	g_unsent_frame_count++;
	pthread_mutex_unlock(&unsent_frame_count_mutex);
}

int main()
{
	struct ns_mgr mgr;
	struct ns_bind_opts opts = {.flags = TCP_NODELAY };

	ns_mgr_init(&mgr, NULL);
	printf("Allowed ifaces: %s\n", EXPAND_AND_QUOTE(ALLOWED_IFACES));
	printf("Web document root: %s\n",
	       EXPAND_AND_QUOTE(WEB_SERVER_DOCUMENT_ROOT));
	printf("Binding to port: %s\n", s_http_port);

	nc = ns_bind_opt(&mgr, s_http_port, ev_handler, opts);
	if (nc == NULL) {
		fprintf(stderr, "Couldn't bind to port:%s\n", s_http_port);
		return -1;
	}
	ns_set_protocol_http_websocket(nc);

	if (netem_init() < 0) {
		fprintf(stderr,
			"Couldn't initialise netlink for netem module.\n");
		return -1;
	}

	stats_monitor_iface("lo");
	stats_thread_init(stats_event_handler);

	for (;;) {
		ns_mgr_poll(&mgr, 1);
		stats_filter_and_send();
	}
	ns_mgr_free(&mgr);

	return 0;
}
