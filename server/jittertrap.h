#ifndef JITTERTRAP_H
#define JITTERTRAP_H

#ifndef MAX_JSON_TOKEN_LEN
#define MAX_JSON_TOKEN_LEN 256
#endif

#ifndef MAX_JSON_MSG_LEN
#define MAX_JSON_MSG_LEN 4096
#endif

#define MESSAGES_PER_SECOND 100

#define USECS_PER_SECOND 1000000
#define SAMPLES_PER_FRAME                                                      \
	(USECS_PER_SECOND / SAMPLE_PERIOD_US / MESSAGES_PER_SECOND)

#ifndef static_assert
#define static_assert _Static_assert
#endif

#define xstr(s) str(s)
#define str(s) #s

/* SAMPLES_PER_FRAME must be an integer */
static_assert(
    (SAMPLES_PER_FRAME * SAMPLE_PERIOD_US * MESSAGES_PER_SECOND) ==
        USECS_PER_SECOND,
    "SAMPLES_PER_FRAME must be an integer, therefore"
    " SAMPLES_PER_FRAME * SAMPLE_PERIOD_US * MESSAGES_PER_SECOND "
    " must equal USECS_PER_SECOND. "
    " (int)" xstr(SAMPLES_PER_FRAME *SAMPLE_PERIOD_US
                      *MESSAGES_PER_SECOND) " != (int)" xstr(USECS_PER_SECOND));

/* for synchronization of netlink cache operations. */
pthread_mutex_t nl_sock_mutex;

int jt_get_sample_period();
int jt_set_iface(const char *iface);
char const *jt_get_iface();

#endif
