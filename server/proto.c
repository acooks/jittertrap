#include <stdio.h>
#include <syslog.h>

#include <libwebsockets.h>

/*
 * this is just an example of parsing handshake headers, you don't need this
 * in your code unless you will filter allowing connections by the header
 * content
 */

void dump_handshake_info(struct lws *wsi)
{
	int n = 0;
	char buf[256];
	const unsigned char *c;

	do {
		c = lws_token_to_string(n);
		if (!c) {
			n++;
			continue;
		}

		if (!lws_hdr_total_length(wsi, n)) {
			n++;
			continue;
		}

		lws_hdr_copy(wsi, buf, sizeof buf, n);

		syslog(LOG_INFO, "    %s = %s", (char *)c, buf);
		n++;
	} while (c);
}
