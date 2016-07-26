#include <sys/time.h>

#include "intervals_user.h"

struct timeval const tt_intervals[INTERVAL_COUNT] = {
        { .tv_sec = 0,  .tv_usec = 5E3 },
        { .tv_sec = 0,  .tv_usec = 1E4 },
        { .tv_sec = 0,  .tv_usec = 2E4 },
        { .tv_sec = 0,  .tv_usec = 5E4 },
        { .tv_sec = 0,  .tv_usec = 1E5 },
        { .tv_sec = 0,  .tv_usec = 2E5 }
};

