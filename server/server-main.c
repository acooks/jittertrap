#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include <syslog.h>
#include <unistd.h>

#include <libwebsockets.h>

#include "proto.h"
#include "proto-jittertrap.h"
#include "pcap_buffer.h"

#define xstr(s) str(s)
#define str(s) #s

char *resource_path = xstr(WEB_SERVER_DOCUMENT_ROOT);

static volatile int force_exit = 0;
static struct lws_context *context;


/* list of supported protocols and callbacks */
static struct lws_protocols protocols[] = {
	    /* first protocol must always be HTTP handler */

	    [PROTOCOL_HTTP] =
	        {
	            .name = "http-only",
	            .callback = lws_callback_http_dummy
	        },
	    [PROTOCOL_JITTERTRAP] =
	        {
	            .name = "jittertrap",
	            .callback = callback_jittertrap,
	            .per_session_data_size =
	                sizeof(struct per_session_data__jittertrap),
	                        .rx_buffer_size = 0,
	                        .tx_packet_size = 16384,	        },

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
	{ "debug", required_argument, NULL, '1' },
	{ "port", required_argument, NULL, 'p' },
	{ "interface", required_argument, NULL, 'i' },
#ifndef LWS_NO_DAEMONIZE
	{ "daemonize", no_argument, NULL, 'D' },
#endif
	{ "resource_path", required_argument, NULL, 'r' },
	{ NULL, 0, 0, 0 }
};


/* MIME type for pcap files */
static const struct lws_protocol_vhost_options pcap_mimetype = {
        NULL,                           /* next */
        NULL,                           /* options */
        ".pcap",                        /* extension */
        "application/vnd.tcpdump.pcap"  /* MIME type */
};

/* Mount for pcap file downloads - must be first in chain */
static struct lws_http_mount pcap_mount = {
        .mount_next             = NULL,             /* will be set to &mount */
        .mountpoint             = "/pcap",          /* mountpoint URL */
        .origin                 = PCAP_BUF_PCAP_DIR, /* serve from pcap dir */
        .def                    = NULL,             /* no default filename */
        .protocol               = NULL,
        .cgienv                 = NULL,
        .extra_mimetypes        = &pcap_mimetype,
        .interpret              = NULL,
        .cgi_timeout            = 0,
        .cache_max_age          = 0,
        .auth_mask              = 0,
        .cache_reusable         = 0,
        .cache_revalidate       = 0,
        .cache_intermediaries   = 0,
        .origin_protocol        = LWSMPRO_FILE,     /* files in a dir */
        .mountpoint_len         = 5,                /* strlen("/pcap") */
        .basic_auth_login_file  = NULL
};

static struct lws_http_mount mount = {
        .mount_next             = &pcap_mount,      /* chain to pcap mount */
        .mountpoint             = "/",              /* mountpoint URL */
        .origin                 = "./mount-origin", /* serve from dir */
        .def                    = "index.html",     /* default filename */
        .protocol               = NULL,
        .cgienv                 = NULL,
        .extra_mimetypes        = NULL,
        .interpret              = NULL,
        .cgi_timeout            = 0,
        .cache_max_age          = 0,
        .auth_mask              = 0,
        .cache_reusable         = 0,
        .cache_revalidate       = 0,
        .cache_intermediaries   = 0,
        .origin_protocol        = LWSMPRO_FILE,     /* files in a dir */
        .mountpoint_len         = 1,                /* char count */
        .basic_auth_login_file  = NULL
};

int main(int argc, char **argv)
{
	int n = 0;
	int opts = 0;
	char interface_name[128] = "";
	const char *iface = NULL;
	int syslog_options = LOG_PID | LOG_PERROR;
	struct lws_context_creation_info info;

	int debug_level = LOG_WARNING;
#ifndef LWS_NO_DAEMONIZE
	int daemonize = 0;
#endif

	memset(&info, 0, sizeof info);
	info.port = WEB_SERVER_PORT;

	while (n >= 0) {
		n = getopt_long(argc, argv, "ci:hsp:dDr:", options, NULL);
		if (n < 0)
			continue;
		switch (n) {
#ifndef LWS_NO_DAEMONIZE
		case 'D':
			daemonize = 1;
			syslog_options &= ~LOG_PERROR;
			break;
#endif
		/* opt that wont be a short opt either - for long --debug */
		case '1':
			debug_level = atoi(optarg);
			debug_level =
			    (debug_level > LOG_DEBUG) ? LOG_DEBUG : debug_level;
			debug_level =
			    (debug_level < LOG_EMERG) ? LOG_DEBUG : debug_level;
			break;
		case 'd':
			debug_level = LOG_DEBUG;
			break;
		case 'p':
			info.port = atoi(optarg);
			break;
		case 'i':
			strncpy(interface_name, optarg, sizeof interface_name);
			interface_name[(sizeof interface_name) - 1] = '\0';
			iface = interface_name;
			break;
		case 'r':
			resource_path = optarg;
			mount.origin = resource_path;
			break;
		case 'h':
			fprintf(stderr,
			        "Usage: " PROGNAME "[--port=<p>] "
			        "[-d <log level>]"
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
	setlogmask(LOG_UPTO(debug_level));
	openlog("jt-server", syslog_options, LOG_DAEMON);

	/* tell the library what debug level to emit and to send it to syslog */
	lws_set_log_level(LOG_UPTO(debug_level), lwsl_emit_syslog);

	syslog(LOG_NOTICE, "jittertrap server\n");
	syslog(LOG_INFO, "Using resource path \"%s\"\n", resource_path);

	/* Initialize pcap buffer with default config */
	if (pcap_buf_ensure_directory() != 0) {
		syslog(LOG_WARNING, "Could not create pcap directory\n");
	}
	if (pcap_buf_init(NULL) != 0) {
		syslog(LOG_WARNING, "Could not initialize pcap buffer\n");
	}

	info.iface = iface;
	info.protocols = protocols;
	info.mounts = &mount;
	info.gid = -1;
	info.uid = -1;
	info.options = opts;

	context = lws_create_context(&info);
	if (context == NULL) {
		syslog(LOG_ERR, "libwebsocket init failed\n");
		return -1;
	}

	n = 0;
	while (n >= 0 && !force_exit) {
		lws_callback_on_writable_all_protocol(
		    context, &protocols[PROTOCOL_JITTERTRAP]);

               /*
		*  FIXME:
		*  The lws_service() timeout doesn't seem to work as expected.
		*  This helps slow things down, but it's not a proper solution.
                */
		const struct timespec rqtp = {.tv_sec = 0, .tv_nsec = 5E5 };
		nanosleep(&rqtp, NULL);

		n = lws_service(context, 1);
	}

	lws_context_destroy(context);

	syslog(LOG_INFO, "jittertrap server exited cleanly\n");

	closelog();

	return 0;
}
