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

#include "proto.h"

int max_poll_elements = 10;

#ifdef EXTERNAL_POLL
struct pollfd *pollfds;
int *fd_lookup;
int count_pollfds;
#endif

#if 0

FIXME: this is not used at the moment, but something like this is needed.

/*
 * We take a strict whitelist approach to stop ../ attacks
 */

struct serveable
{
	const char *urlpath;
	const char *mimetype;
};

#endif

struct per_session_data__http {
	int fd;
};

const char *get_mimetype(const char *file)
{
	int n = strlen(file);

	if (n < 5)
		return NULL;

	if (!strcmp(&file[n - 4], ".ico"))
		return "image/x-icon";

	if (!strcmp(&file[n - 4], ".png"))
		return "image/png";

	if (!strcmp(&file[n - 5], ".html"))
		return "text/html";

	if (!strcmp(&file[n - 4], ".css"))
		return "text/css";

	if (!strcmp(&file[n - 3], ".js"))
		return "application/ecmascript";

	if (!strcmp(&file[n - 5], ".woff"))
		return "application/font-woff";

	if (!strcmp(&file[n - 6], ".woff2"))
		return "application/font-woff2";

	if (!strcmp(&file[n - 4], ".ttf"))
		return "application/x-font-ttf";

	if (!strcmp(&file[n - 4], ".svg"))
		return "image/svg+xml";

	if (!strcmp(&file[n - 4], ".eot"))
		return "application/vnd.ms-fontobject";

	return NULL;
}

/* this protocol server (always the first one) just knows how to do HTTP */

int callback_http(struct lws *wsi, enum lws_callback_reasons reason, void *user,
                  void *in, size_t len)
{
	char buf[256];
	char leaf_path[1024];
	char b64[64];
	struct timeval tv;
	int n, m;
	unsigned char *p;
	char *other_headers;
	static unsigned char buffer[4096];
	struct per_session_data__http *pss =
	    (struct per_session_data__http *)user;
	const char *mimetype;
#ifdef EXTERNAL_POLL
	struct lws_pollargs *pa = (struct lws_pollargs *)in;
#endif
	switch (reason) {
	case LWS_CALLBACK_HTTP:

		dump_handshake_info(wsi);

		if (len < 1) {
			lws_return_http_status(wsi, HTTP_STATUS_BAD_REQUEST,
			                       NULL);
			goto try_to_reuse;
		}

#if 0
FIXME: this does not do what we need, but something like this is needed.

		/* this example server has no concept of directories */
		if (strchr((const char *)in + 1, '/')) {
			lws_return_http_status(
			    context, wsi, HTTP_STATUS_FORBIDDEN, NULL);
			goto try_to_reuse;
		}
#endif

		/* if a legal POST URL, let it continue and accept data */
		if (lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI))
			return 0;

		/* collect the file part of the url in buf */
		strncpy(buf, resource_path, 255);
		if (strcmp(in, "/")) {
			if (*((const char *)in) != '/')
				strcat(buf, "/");
			strncat(buf, in, sizeof(buf) - strlen(resource_path));
		} else /* default file to serve */
			strcat(buf, "/index.html");
		buf[sizeof(buf) - 1] = '\0';
		syslog(LOG_INFO, "get %s\n", buf);

		/* refuse to serve files we don't understand */
		mimetype = get_mimetype(buf);
		if (!mimetype) {
			syslog(LOG_ERR, "Unknown mimetype for %s\n", buf);
			lws_return_http_status(
			    wsi, HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE, NULL);
			return -1;
		}

		/* demostrates how to set a cookie on / */

		other_headers = NULL;
		n = 0;
		if (!strcmp((const char *)in, "/") &&
		    !lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_COOKIE)) {
			/* this isn't very unguessable but it'll do for us */
			gettimeofday(&tv, NULL);
			n = sprintf(b64, "test=LWS_%u_%u_COOKIE;Max-Age=360000",
			            (unsigned int)tv.tv_sec,
			            (unsigned int)tv.tv_usec);

			p = (unsigned char *)leaf_path;

			if (lws_add_http_header_by_name(
			        wsi, (unsigned char *)"set-cookie:",
			        (unsigned char *)b64, n, &p,
			        (unsigned char *)leaf_path + sizeof(leaf_path)))
				return 1;
			n = (char *)p - leaf_path;
			other_headers = leaf_path;
		}

		n = lws_serve_http_file(wsi, buf, mimetype, other_headers, n);
		if (n < 0 || ((n > 0) && lws_http_transaction_completed(wsi))) {
			/* error or can't reuse connection: close the socket */
			return -1;
		}

		/*
		 * notice that the sending of the file completes asynchronously,
		 * we'll get a LWS_CALLBACK_HTTP_FILE_COMPLETION callback when
		 * it's done
		 */

		break;

	case LWS_CALLBACK_HTTP_BODY:
		strncpy(buf, in, 20);
		buf[20] = '\0';
		if (len < 20)
			buf[len] = '\0';

		syslog(LOG_NOTICE, "LWS_CALLBACK_HTTP_BODY: %s... len %d\n",
		       (const char *)buf, (int)len);

		break;

	case LWS_CALLBACK_HTTP_BODY_COMPLETION:
		syslog(LOG_NOTICE, "LWS_CALLBACK_HTTP_BODY_COMPLETION\n");
		/* the whole of the sent body arrived,
		 * close or reuse the connection */
		lws_return_http_status(wsi, HTTP_STATUS_OK, NULL);
		goto try_to_reuse;

	case LWS_CALLBACK_HTTP_FILE_COMPLETION:
		// lwsl_info("LWS_CALLBACK_HTTP_FILE_COMPLETION seen\n");
		/* kill the connection after we sent one file */
		goto try_to_reuse;

	case LWS_CALLBACK_HTTP_WRITEABLE:
		/* we can send more of whatever it is we were sending */
		do {
			/* we'd like the send this much */
			n = sizeof(buffer) - LWS_SEND_BUFFER_PRE_PADDING;

			/* but if the peer told us he wants less, we adapt */
			m = lws_get_peer_write_allowance(wsi);

			/* -1 means not using a protocol that has this info */
			if (m == 0)
				/* right now, peer can't handle anything */
				goto later;

			if (m != -1 && m < n)
				/* he couldn't handle that much */
				n = m;

			n = read(pss->fd, buffer + LWS_SEND_BUFFER_PRE_PADDING,
			         n);

			/* problem reading, close conn */
			if (n < 0)
				goto bail;

			/* sent it all, close conn */
			if (n == 0)
				goto flush_bail;

			/*
			 * To support HTTP2, take care of preamble space
			 * identification of when we send the last payload frame
			 * Handled by the library itself if you sent a
			 * content-length header.
			 */
			m = lws_write(wsi, buffer + LWS_SEND_BUFFER_PRE_PADDING,
			              n, LWS_WRITE_HTTP);
			if (m < 0)
				/* write failed, close conn */
				goto bail;

			/*
			 * http2 won't do this
			 */
			if (m != n)
				/* partial write, adjust */
				if (lseek(pss->fd, m - n, SEEK_CUR) < 0)
					goto bail;

			if (m) /* while still active, extend timeout */
				lws_set_timeout(
				    wsi, PENDING_TIMEOUT_HTTP_CONTENT, 5);

			/* if we have indigestion, let him clear it,
			 *  before eating more */
			if (lws_partial_buffered(wsi))
				break;

		} while (!lws_send_pipe_choked(wsi));

	later:
		lws_callback_on_writable(wsi);
		break;
	flush_bail:
		/* true if still partial pending */
		if (lws_partial_buffered(wsi)) {
			lws_callback_on_writable(wsi);
			break;
		}
		close(pss->fd);
		goto try_to_reuse;

	bail:
		close(pss->fd);
		return -1;

	/*
	 * callback for confirming to continue with client IP appear in
	 * protocol 0 callback since no websocket protocol has been agreed
	 * yet.  You can just ignore this if you won't filter on client IP
	 * since the default uhandled callback return is 0, meaning let the
	 * connection continue.
	 */

	case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
		/* if we returned non-zero from here, we kill the connection */
		break;

#ifdef EXTERNAL_POLL
	/*
	 * callbacks for managing the external poll() array appear in
	 * protocol 0 callback
	 */

	case LWS_CALLBACK_LOCK_POLL:
		/*
		 * lock mutex to protect pollfd state
		 * called before any other POLL related callback
		 */
		break;

	case LWS_CALLBACK_UNLOCK_POLL:
		/*
		 * unlock mutex to protect pollfd state when
		 * called after any other POLL related callback
		 */
		break;

	case LWS_CALLBACK_ADD_POLL_FD:

		if (count_pollfds >= max_poll_elements) {
			syslog(LOG_ERR, "LWS_CALLBACK_ADD_POLL_FD: "
			       "too many sockets to track\n");
			return 1;
		}

		fd_lookup[pa->fd] = count_pollfds;
		pollfds[count_pollfds].fd = pa->fd;
		pollfds[count_pollfds].events = pa->events;
		pollfds[count_pollfds++].revents = 0;
		break;

	case LWS_CALLBACK_DEL_POLL_FD:
		if (!--count_pollfds)
			break;
		m = fd_lookup[pa->fd];
		/* have the last guy take up the vacant slot */
		pollfds[m] = pollfds[count_pollfds];
		fd_lookup[pollfds[count_pollfds].fd] = m;
		break;

	case LWS_CALLBACK_CHANGE_MODE_POLL_FD:
		pollfds[fd_lookup[pa->fd]].events = pa->events;
		break;

#endif

	default:
		break;
	}

	return 0;

try_to_reuse:
	if (lws_http_transaction_completed(wsi))
		return -1;

	return 0;
}
