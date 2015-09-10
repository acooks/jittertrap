#include "lws_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include <syslog.h>
#include <sys/time.h>
#include <unistd.h>

#include <libwebsockets.h>

#include "proto-http.h"
#include "proto-mirror.h"
#include "proto-dinc.h"
#include "test.h"

int max_poll_elements;
char *resource_path = LOCAL_RESOURCE_PATH;

static volatile int force_exit = 0;
static struct libwebsocket_context *context;

/* list of supported protocols and callbacks */
static struct libwebsocket_protocols protocols[] = {
	/* first protocol must always be HTTP handler */

	{
	  .name = "http-only",
	  .callback = callback_http,
	  .per_session_data_size = sizeof(struct per_session_data__http),
	  .rx_buffer_size = 0, /* max frame size / rx buffer */
	},
	{
	  .name = "dumb-increment-protocol",
	  .callback = callback_dumb_increment,
	  .per_session_data_size =
	      sizeof(struct per_session_data__dumb_increment),
	  .rx_buffer_size = 10,
	},
	{
	  .name = "lws-mirror-protocol",
	  .callback = callback_lws_mirror,
	  .per_session_data_size = sizeof(struct per_session_data__lws_mirror),
	  .rx_buffer_size = 128,
	},

	/* terminator */
	{ .name = NULL,
	  .callback = NULL,
	  .per_session_data_size = 0,
	  .rx_buffer_size = 0 }
};

void sighandler(int sig __attribute__((unused)))
{
	force_exit = 1;
	libwebsocket_cancel_service(context);
}

static struct option options[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "debug", required_argument, NULL, 'd' },
	{ "port", required_argument, NULL, 'p' },
	{ "ssl", no_argument, NULL, 's' },
	{ "allow-non-ssl", no_argument, NULL, 'a' },
	{ "interface", required_argument, NULL, 'i' },
	{ "closetest", no_argument, NULL, 'c' },
	{ "libev", no_argument, NULL, 'e' },
#ifndef LWS_NO_DAEMONIZE
	{ "daemonize", no_argument, NULL, 'D' },
#endif
	{ "resource_path", required_argument, NULL, 'r' },
	{ NULL, 0, 0, 0 }
};

int main(int argc, char **argv)
{
	char cert_path[1024];
	char key_path[1024];
	int n = 0;
	int use_ssl = 0;
	int opts = 0;
	char interface_name[128] = "";
	const char *iface = NULL;
	int syslog_options = LOG_PID | LOG_PERROR;
	unsigned int ms, oldms = 0;
	struct lws_context_creation_info info;

	int debug_level = 7;
#ifndef LWS_NO_DAEMONIZE
	int daemonize = 0;
#endif

	memset(&info, 0, sizeof info);
	info.port = 7681;

	while (n >= 0) {
		n = getopt_long(argc, argv, "eci:hsap:d:Dr:", options, NULL);
		if (n < 0)
			continue;
		switch (n) {
		case 'e':
			opts |= LWS_SERVER_OPTION_LIBEV;
			break;
#ifndef LWS_NO_DAEMONIZE
		case 'D':
			daemonize = 1;
			syslog_options &= ~LOG_PERROR;
			break;
#endif
		case 'd':
			debug_level = atoi(optarg);
			break;
		case 's':
			use_ssl = 1;
			break;
		case 'a':
			opts |= LWS_SERVER_OPTION_ALLOW_NON_SSL_ON_SSL_PORT;
			break;
		case 'p':
			info.port = atoi(optarg);
			break;
		case 'i':
			strncpy(interface_name, optarg, sizeof interface_name);
			interface_name[(sizeof interface_name) - 1] = '\0';
			iface = interface_name;
			break;
		case 'c':
			close_testing = 1;
			fprintf(stderr, " Close testing mode -- closes on "
			                "client after 50 dumb increments"
			                "and suppresses lws_mirror spam\n");
			break;
		case 'r':
			resource_path = optarg;
			printf("Setting resource path to \"%s\"\n",
			       resource_path);
			break;
		case 'h':
			fprintf(stderr, "Usage: test-server "
			                "[--port=<p>] [--ssl] "
			                "[-d <log bitfield>] "
			                "[--resource_path <path>]\n");
			exit(1);
		}
	}

#if !defined(LWS_NO_DAEMONIZE)
	/*
	 * normally lock path would be /var/lock/lwsts or similar, to
	 * simplify getting started without having to take care about
	 * permissions or running as root, set to /tmp/.lwsts-lock
	 */
	if (daemonize && lws_daemonize("/tmp/.lwsts-lock")) {
		fprintf(stderr, "Failed to daemonize\n");
		return 1;
	}
#endif

	signal(SIGINT, sighandler);

	/* we will only try to log things according to our debug_level */
	setlogmask(LOG_UPTO(LOG_DEBUG));
	openlog("lwsts", syslog_options, LOG_DAEMON);

	/* tell the library what debug level to emit and to send it to syslog */
	lws_set_log_level(debug_level, lwsl_emit_syslog);

	lwsl_notice("libwebsockets test server - "
	            "(C) Copyright 2010-2015 Andy Green <andy@warmcat.com> - "
	            "licensed under LGPL2.1\n");

	printf("Using resource path \"%s\"\n", resource_path);

	info.iface = iface;
	info.protocols = protocols;
#ifndef LWS_NO_EXTENSIONS
	info.extensions = libwebsocket_get_internal_extensions();
#endif
	if (!use_ssl) {
		info.ssl_cert_filepath = NULL;
		info.ssl_private_key_filepath = NULL;
	} else {
		if (strlen(resource_path) > sizeof(cert_path) - 32) {
			lwsl_err("resource path too long\n");
			return -1;
		}
		sprintf(cert_path, "%s/libwebsockets-test-server.pem",
		        resource_path);

		if (strlen(resource_path) > sizeof(key_path) - 32) {
			lwsl_err("resource path too long\n");
			return -1;
		}
		sprintf(key_path, "%s/libwebsockets-test-server.key.pem",
		        resource_path);

		info.ssl_cert_filepath = cert_path;
		info.ssl_private_key_filepath = key_path;
	}
	info.gid = -1;
	info.uid = -1;
	info.options = opts;

	context = libwebsocket_create_context(&info);
	if (context == NULL) {
		lwsl_err("libwebsocket init failed\n");
		return -1;
	}

	n = 0;
	while (n >= 0 && !force_exit) {
		struct timeval tv;

		gettimeofday(&tv, NULL);

		/*
		 * This provokes the LWS_CALLBACK_SERVER_WRITEABLE for every
		 * live websocket connection using the DUMB_INCREMENT protocol,
		 * as soon as it can take more packets (usually immediately)
		 */

		ms = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
		if ((ms - oldms) > 50) {
			libwebsocket_callback_on_writable_all_protocol(
			    &protocols[PROTOCOL_DUMB_INCREMENT]);
			oldms = ms;
		}

		/*
		 * If libwebsockets sockets are all we care about,
		 * you can use this api which takes care of the poll()
		 * and looping through finding who needed service.
		 *
		 * If no socket needs service, it'll return anyway after
		 * the number of ms in the second argument.
		 */

		n = libwebsocket_service(context, 50);
	}

	libwebsocket_context_destroy(context);

	lwsl_notice("libwebsockets-test-server exited cleanly\n");

	closelog();

	return 0;
}
