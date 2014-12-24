#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <stdio.h>
#include <sched.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <linux/types.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/cache.h>
#include <netlink/utils.h>
#include <netlink/route/link.h>

#include "stats_thread.h"

/* globals */
static pthread_t stats_thread;
struct sigaction sa;

struct nl_sock *nl_sock;
struct nl_cache *nl_link_cache;

char *g_iface;
struct byte_counts stats_o;
struct byte_counts stats_c;

void (*stats_handler)(struct byte_counts *counts);

/* local prototypes */
static void * run(void *data);


int stats_thread_init(void (*_stats_handler)(struct byte_counts *counts))
{
  if (!g_iface || !_stats_handler) {
    return -1;
  }
  stats_handler = _stats_handler;

  if (!stats_thread) {
    return pthread_create(&stats_thread, NULL, run, NULL);
  }
  return 0;
}

void stats_monitor_iface(const char *_iface)
{
  if (g_iface) {
    free(g_iface);
  }
  g_iface = strdup(_iface);
  stats_o.rx_bytes = 0;
  stats_o.tx_bytes = 0;
}

static int init_nl(void)
{
  int err;

  /* Allocate and initialize a new netlink handle */
  if (!(nl_sock = nl_socket_alloc())) {
    fprintf(stderr, "Failed to alloc netlink socket\n");
    return -EOPNOTSUPP;
  }

  /* Bind and connect socket to protocol, NETLINK_ROUTE in our case. */
  if ((err = nl_connect(nl_sock, NETLINK_ROUTE)) < 0) {
    fprintf(stderr, "Failed to connect to kernel\n");
    return -EOPNOTSUPP;
  }

  /* Retrieve a list of all available interfaces and populate cache. */
  if ((err = rtnl_link_alloc_cache(nl_sock, AF_UNSPEC, &nl_link_cache)) < 0) {
    fprintf(stderr, "Error creating link cache\n");
    return -EOPNOTSUPP;
  }
  return 0;
}

static int read_counters(const char *iface)
{
  int err;
  struct rtnl_link *link;
  assert(nl_sock);
  assert(nl_link_cache);

  if ((err = nl_cache_refill(nl_sock, nl_link_cache)) < 0) {
    fprintf(stderr, "Unable to resync link cache: %s\n", nl_geterror(err));
    return -1;
  }

  /* filter link by name */
  if ((link = rtnl_link_get_by_name(nl_link_cache, iface)) == NULL) {
    fprintf(stderr, "unknown interface/link name.\n");
    return -1;
  }

  /* read and return counter */
  stats_c.rx_bytes = rtnl_link_get_stat(link, RTNL_LINK_RX_BYTES);
  stats_c.tx_bytes = rtnl_link_get_stat(link, RTNL_LINK_TX_BYTES);
  return 0;
}

static void calc_deltas()
{
  if (0 == stats_o.rx_bytes) {
    stats_o.rx_bytes = stats_c.rx_bytes;
    stats_o.tx_bytes = stats_c.tx_bytes;
  }

  stats_c.rx_bytes_delta = stats_c.rx_bytes -
                           ((stats_o.rx_bytes + stats_c.rx_bytes) / 2);
  stats_c.tx_bytes_delta = stats_c.tx_bytes -
                           ((stats_o.tx_bytes + stats_c.tx_bytes) / 2);
}

static void timer_handler(int signum)
{
  struct timeval t;
  gettimeofday(&t, NULL);
  stats_c.timestamp = (t.tv_sec * 1000 * 1000 + t.tv_usec);
  if (0 == read_counters(g_iface)) {
    calc_deltas();
    stats_handler(&stats_c);
  }
  memcpy(&stats_o, &stats_c, sizeof(struct byte_counts));
}

static int init_realtime(void)
{
  struct sched_param schedparm;
  memset(&schedparm, 0, sizeof(schedparm));
  schedparm.sched_priority = 1; // lowest rt priority
  return sched_setscheduler(0, SCHED_FIFO, &schedparm);
}

static void init_timer(void)
{
  struct itimerval timer;

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = &timer_handler;
  sigaction(SIGALRM, &sa, NULL);

  timer.it_value.tv_sec = 0;
  timer.it_value.tv_usec = 1000 * SAMPLE_PERIOD_MS;

  timer.it_interval.tv_sec = 0;
  timer.it_interval.tv_usec = 1000 * SAMPLE_PERIOD_MS;

  setitimer(ITIMER_REAL, &timer, NULL);
}

static void * run(void *data)
{
  init_nl();
  init_realtime();
  init_timer();

  for (;;) {
    pause();
  }

  return NULL;
}
