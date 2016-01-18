#ifndef TIMEYWIMEY_H
#define TIMEYWIMEY_H

/* Calculate the absolute difference between t1 and t2. */
struct timespec ts_absdiff(struct timespec t1, struct timespec t2);
struct timespec ts_add(struct timespec t1, struct timespec t2);
#endif
