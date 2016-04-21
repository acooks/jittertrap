#ifndef TIMEYWIMEY_H
#define TIMEYWIMEY_H

/* Calculate the absolute difference between t1 and t2. */
struct timespec ts_absdiff(struct timespec t1, struct timespec t2);
struct timespec ts_add(struct timespec t1, struct timespec t2);
struct timeval tv_absdiff(struct timeval t1, struct timeval t2);
struct timeval tv_add(struct timeval t1, struct timeval t2);
int tv_cmp(struct timeval t1, struct timeval t2);
#endif
