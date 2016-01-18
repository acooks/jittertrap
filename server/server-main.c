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

#include "proto.h"
#include "proto-http.h"
#include "proto-jittertrap.h"

int max_poll_elements;
char *resource_path = LOCAL_RESOURCE_PATH;

static volatile int force_exit = 0;
static struct lws_context *context;

/* list of supported protocols and callbacks */
static struct lws_protocols protocols[] = {
	    /* first protocol must always be HTTP handler */

	    [PROTOCOL_HTTP] =
	        {
	            .name = "http-only",
	            .callback = callback_http,
	            .per_session_data_size =
	                sizeof(struct per_session_data__http),
	            .rx_buffer_size = 0, /* max frame size / rx buffer */
	        },
	    [PROTOCOL_JITTERTRAP] =
	        {
	            .name = "jittertrap",
	            .callback = callback_jittertrap,
	            .per_session_data_size =
	                sizeof(struct per_session_data__jittertrap),
	            .rx_buffer_size = 4000,
	        },

	    /* terminator */
	    [PROTOCOL_TERMINATOR] = {.name = NULL,
	                             .callback = NULL,
	                             .per_session_data_size = 0,
	                             .rx_buffer_size = 0 }
};

void sighandler(int sig __attribute__((unused)))
{
	force_exit = 1;
	lws_cancel_service(context);
}

static struct option options[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "debug", required_argument, NULL, 'd' },
	{ "port", required_argument, NULL, 'p' },
	{ "ssl", no_argument, NULL, 's' },
	{ "interface", required_argument, NULL, 'i' },
	{ "closetest", no_argument, NULL, 'c' },
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
			                "client after 50 messages.\n");
			break;
		case 'r':
			resource_path = optarg;
			printf("Setting resource path to \"%s\"\n",
			       resource_path);
			break;
		case 'h':
			fprintf(stderr,
			        "Usage: " PROGNAME "[--port=<p>] [--ssl] "
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

	lwsl_notice("jittertrap server\n");

	printf("Using resource path \"%s\"\n", resource_path);

	info.iface = iface;
	info.protocols = protocols;
#ifndef LWS_NO_EXTENSIONS
	info.extensions = lws_get_internal_extensions();
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

	context = lws_create_context(&info);
	if (context == NULL) {
		lwsl_err("libwebsocket init failed\n");
		return -1;
	}

	n = 0;
	while (n >= 0 && !force_exit) {
		lws_callback_on_writable_all_protocol(
		    context, &protocols[PROTOCOL_JITTERTRAP]);

		/* FIXME: something is causing us to spin. This helps to
		 * slow things down, but it's not a proper solution.
		 */
		usleep(1);

		/*
		 * takes care of the poll() and looping through finding who
		 * needs service.
		 *
		 * If no socket needs service, it'll return anyway after
		 * the number of ms in the second argument.
		 */

		n = lws_service(context, 1);
	}

	lws_context_destroy(context);

	lwsl_notice("jittertrap server exited cleanly\n");

	closelog();

	return 0;
}
