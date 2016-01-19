#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <libwebsockets.h>

#include "proto.h"

static int deny_deflate;
static int deny_mux;
static volatile int force_exit = 0;
static int longlived = 0;

void sighandler(int sig __attribute__((unused)))
{
	force_exit = 1;
}

static struct option options[] = { { "help", no_argument, NULL, 'h' },
	                           { "debug", required_argument, NULL, 'd' },
	                           { "port", required_argument, NULL, 'p' },
	                           { "ssl", no_argument, NULL, 's' },
	                           { "version", required_argument, NULL, 'v' },
	                           { "undeflated", no_argument, NULL, 'u' },
	                           { "nomux", no_argument, NULL, 'n' },
	                           { "longlived", no_argument, NULL, 'l' },
	                           { NULL, 0, 0, 0 } };

int main(int argc, char **argv)
{
	int n = 0;
	int ret = 0;
	int port = 80;
	int use_ssl = 0;
	struct lws_context *context;
	const char *address;
	struct lws *wsi_jt;
	int ietf_version = -1; /* latest */
	struct lws_context_creation_info info;

	memset(&info, 0, sizeof info);

	fprintf(stderr, "jittertrap test client\n");

	if (argc < 2)
		goto usage;

	while (n >= 0) {
		n = getopt_long(argc, argv, "nuv:hsp:d:l", options, NULL);
		if (n < 0)
			continue;
		switch (n) {
		case 'd':
			lws_set_log_level(atoi(optarg), NULL);
			break;
		case 's':
			use_ssl = 2; /* 2 = allow selfsigned */
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case 'l':
			longlived = 1;
			break;
		case 'v':
			ietf_version = atoi(optarg);
			break;
		case 'u':
			deny_deflate = 1;
			break;
		case 'n':
			deny_mux = 1;
			break;
		case 'h':
			goto usage;
		}
	}

	if (optind >= argc)
		goto usage;

	signal(SIGINT, sighandler);

	address = argv[optind];

	/*
	 * create the websockets context.  This tracks open connections and
	 * knows how to route any traffic and which protocol version to use,
	 * and if each connection is client or server side.
	 *
	 * For this client-only demo, we tell it to not listen on any port.
	 */

	info.port = CONTEXT_PORT_NO_LISTEN;
	info.protocols = protocols;
#ifndef LWS_NO_EXTENSIONS
	info.extensions = lws_get_internal_extensions();
#endif
	info.gid = -1;
	info.uid = -1;

	context = lws_create_context(&info);
	if (context == NULL) {
		fprintf(stderr, "Creating libwebsocket context failed\n");
		return 1;
	}

	/* create a client websocket */

	wsi_jt = lws_client_connect(
	    context, address, port, use_ssl, "/", argv[optind], argv[optind],
	    protocols[PROTOCOL_JITTERTRAP].name, ietf_version);

	if (wsi_jt == NULL) {
		fprintf(stderr, "libwebsocket connect failed\n");
		ret = 1;
		goto bail;
	}

	fprintf(stderr, "Waiting for connect...\n");

	do {
		const struct timespec rqtp = {.tv_sec = 0, .tv_nsec = 1E5 };
		nanosleep(&rqtp, NULL);
	} while (!force_exit && (0 == lws_service(context, 0)));

bail:
	fprintf(stderr, "Exiting\n");

	lws_context_destroy(context);

	return ret;

usage:
	fprintf(stderr, "Usage: jittertrap-cli "
	                "<server address> [--port=<p>] "
	                "[--ssl] [-k] [-v <ver>] "
	                "[-d <log bitfield>] [-l]\n");
	return 1;
}
