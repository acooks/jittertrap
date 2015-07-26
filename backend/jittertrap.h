#ifndef JITTERTRAP_H
#define JITTERTRAP_H

#define MAX_IFACE_LEN 16

#define MAX_JSON_TOKEN_LEN 256

#define MAX_JSON_MSG_LEN 4096

#define MESSAGES_PER_SECOND 50

#define USECS_PER_SECOND 1000000
#define FILTERED_SAMPLES_PER_MSG \
	(USECS_PER_SECOND / SAMPLE_PERIOD_US / MESSAGES_PER_SECOND)

#define SAMPLES_PER_FRAME \
	(USECS_PER_SECOND / SAMPLE_PERIOD_US / MESSAGES_PER_SECOND)

#ifndef static_assert
#define static_assert _Static_assert
#endif

/* raw samples must be an integer multiple of filtered samples */
static_assert((SAMPLES_PER_FRAME % FILTERED_SAMPLES_PER_MSG) == 0,
	      "Decimation requires SAMPLES_PER_FRAME to be an integer "
	      "multiple of FILTERED_SAMPLES_PER_MSG");

/* for synchronization of netlink cache operations. */
pthread_mutex_t nl_sock_mutex;

#endif
