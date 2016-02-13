#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <stdio.h>
#include <sched.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>

#include "jittertrap.h"
#include "compute_thread.h"

static pthread_t compute_thread;

/* local prototypes */
static void *run(void *data);

int compute_thread_init(int (*compute_thread_cb)(struct somestats *stats))
{
	int err;
	if (!compute_thread) {
                err = pthread_create(&compute_thread, NULL, run, NULL);
		assert(!err);
		pthread_setname_np(compute_thread, "jt-compute");
        }
        return 0;
}

#define handle_error_en(en, msg)                                               \
	do {                                                                   \
		errno = en;                                                    \
		perror(msg);                                                   \
		exit(EXIT_FAILURE);                                            \
	} while (0)

static void set_affinity()
{
        int s, j;
        cpu_set_t cpuset;
        pthread_t thread;
        thread = pthread_self();
        /* Set affinity mask to include CPUs 1 only */
        CPU_ZERO(&cpuset);
        CPU_SET(RT_CPU, &cpuset);
        s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
        if (s != 0) {
                handle_error_en(s, "pthread_setaffinity_np");
        }

        /* Check the actual affinity mask assigned to the thread */
        s = pthread_getaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
        if (s != 0) {
                handle_error_en(s, "pthread_getaffinity_np");
        }

        printf("RT thread CPU affinity: ");
        for (j = 0; j < CPU_SETSIZE; j++) {
                if (CPU_ISSET(j, &cpuset)) {
                        printf(" CPU%d", j);
                }
        }
        printf("\n");
}

static int init_realtime(void)
{
        struct sched_param schedparm;
        memset(&schedparm, 0, sizeof(schedparm));
        schedparm.sched_priority = 1; // lowest rt priority
        sched_setscheduler(0, SCHED_FIFO, &schedparm);
        set_affinity();
        return 0;
}

static void *run()
{
	init_realtime();
	struct timespec deadline;

	clock_gettime(CLOCK_MONOTONIC, &deadline);

	for (;;) {
		deadline.tv_nsec += 1E6;

                /* Normalize the time to account for the second boundary */
                if (deadline.tv_nsec >= 1000000000) {
                        deadline.tv_nsec -= 1000000000;
                        deadline.tv_sec++;
                }

                clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline,
                                NULL);
	}
}
