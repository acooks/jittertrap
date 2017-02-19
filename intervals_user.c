#include <sys/time.h>

struct timeval const tt_intervals[INTERVAL_COUNT] = {
        { .tv_sec = 0,  .tv_usec = 1E5 },
        { .tv_sec = 0,  .tv_usec = 2E5 },
        { .tv_sec = 0,  .tv_usec = 5E5 },
        { .tv_sec = 1,  .tv_usec = 0 },
        { .tv_sec = 3,  .tv_usec = 0 },
        { .tv_sec = 5,  .tv_usec = 0 },
        { .tv_sec = 10, .tv_usec = 0 },
        { .tv_sec = 60, .tv_usec = 0 }
};

