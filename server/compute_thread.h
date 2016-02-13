#ifndef COMPUTE_THREAD_H
#define COMPUTE_THREAD_H

struct somestats {
 int foo;
};

/* callback */
int compute_thread_init(int (*compute_thread_cb)(struct somestats *stats));

#endif
