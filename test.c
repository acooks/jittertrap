#define _GNU_SOURCE
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>

#include "flow.h"
#include "intervals_user.h"
#include "intervals.h"

int main(int argc, char *argv[])
{
	int err;
	void *res;
	struct tt_thread_info ti = {
		0,
		.thread_name = "tt-test",
		.thread_prio = 0
	};

	if (argc == 2) {
		ti.dev = argv[1];
	} else {
		ti.dev = NULL;
	}

	/* start & run thread for capture and interval processing */
	tt_intervals_init(&ti);

	err = pthread_attr_init(&ti.attr);
	if (err) {
		handle_error_en(err, "pthread_attr_init");
	}

	err = pthread_create(&ti.thread_id, &ti.attr, tt_intervals_run, &ti);
	if (err) {
		handle_error_en(err, "pthread_create");
	}

	tt_update_ref_window_size(tt_intervals[0]);
	tt_update_ref_window_size(tt_intervals[INTERVAL_COUNT - 1]);

	/* check if the thread is still alive */
	if (EBUSY != pthread_tryjoin_np(ti.thread_id, &res)) {
		handle_error_en(err, "pthread_tryjoin_np");
	}

	pthread_cancel(ti.thread_id);

	pthread_join(ti.thread_id, &res);

	free(ti.t5);

	return 0;
}
