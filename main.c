#define _GNU_SOURCE
#include <signal.h>
#include <poll.h>
#include <stdio.h>
#include <pcap.h>
#include <time.h>

void got_packet(u_char *user, const struct pcap_pkthdr *h, const u_char *bytes)
{
	/* Print the time and length */
	printf("%ld.%06ld,  %d\n", h->ts.tv_sec, h->ts.tv_usec, h->len);
}

int grab_packets(int fd, pcap_t *handle)
{
	int ready;
	struct timespec timeout_ts = {.tv_sec = 0, .tv_nsec = 1E8 };
	struct pollfd fds[] = { {.fd = fd,
		                 .events = POLLIN,
		                 .revents = POLLHUP } };
	int num_packets = 0;

	while (1) {
		ready = ppoll(fds, 1, &timeout_ts, NULL);
		if (ready) {
			num_packets = pcap_dispatch(handle, num_packets,
			                            got_packet, NULL);
			printf("%d packets\n", num_packets);
		}
	}
	return 0;
}

int main(int argc, char *argv[])
{
	char *dev, errbuf[PCAP_ERRBUF_SIZE];
	pcap_t *handle;
	int selectable_fd;

	if (argc == 2) {
		dev = argv[1];
	} else {
		dev = pcap_lookupdev(errbuf);
	}

	if (dev == NULL) {
		fprintf(stderr, "Couldn't find default device: %s\n", errbuf);
		return (2);
	}
	printf("Device: %s\n", dev);

	handle = pcap_open_live(dev, BUFSIZ, 1, 0, errbuf);
	if (handle == NULL) {
		fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
		return (2);
	}

	if (pcap_datalink(handle) != DLT_EN10MB) {
		fprintf(stderr, "Device %s doesn't provide Ethernet headers - "
		                "not supported\n",
		        dev);
		return (2);
	}

	if (pcap_setnonblock(handle, 1, errbuf) != 0) {
		fprintf(stderr, "Non-blocking mode failed: %s\n", errbuf);
		return (2);
	}

	selectable_fd = pcap_get_selectable_fd(handle);
	if (-1 == selectable_fd) {
		fprintf(stderr, "pcap handle not selectable.\n");
		return (2);
	}

	grab_packets(selectable_fd, handle);
	/* And close the session */
	pcap_close(handle);
	return 0;
}
