#include <sys/time.h>
#include <time.h>
#include "timeywimey.h"

/* Calculate the absolute difference between t1 and t2. */
struct timespec ts_absdiff(struct timespec t1, struct timespec t2)
{
	struct timespec t;
	if ((t1.tv_sec < t2.tv_sec) ||
	    ((t1.tv_sec == t2.tv_sec) && (t1.tv_nsec <= t2.tv_nsec))) {
		/* t1 <= t2 */
		t.tv_sec = t2.tv_sec - t1.tv_sec;
		if (t2.tv_nsec < t1.tv_nsec) {
			t.tv_nsec = t2.tv_nsec + 1000000000L - t1.tv_nsec;
			t.tv_sec--;
		} else {
			t.tv_nsec = t2.tv_nsec - t1.tv_nsec;
		}
	} else {
		/* t1 > t2 */
		t.tv_sec = t1.tv_sec - t2.tv_sec;
		if (t1.tv_nsec < t2.tv_nsec) {
			t.tv_nsec = t1.tv_nsec + 1000000000L - t2.tv_nsec;
			t.tv_sec--; /* Borrow a second. */
		} else {
			t.tv_nsec = t1.tv_nsec - t2.tv_nsec;
		}
	}
	return t;
}

/* Calculate the absolute difference between t1 and t2. */
inline struct timeval tv_absdiff(struct timeval t1, struct timeval t2)
{
	struct timeval t;
	if ((t1.tv_sec < t2.tv_sec) ||
	    ((t1.tv_sec == t2.tv_sec) && (t1.tv_usec <= t2.tv_usec))) {
		/* t1 <= t2 */
		t.tv_sec = t2.tv_sec - t1.tv_sec;
		if (t2.tv_usec < t1.tv_usec) {
			t.tv_usec = t2.tv_usec + 1000000L - t1.tv_usec;
			t.tv_sec--;
		} else {
			t.tv_usec = t2.tv_usec - t1.tv_usec;
		}
	} else {
		/* t1 > t2 */
		t.tv_sec = t1.tv_sec - t2.tv_sec;
		if (t1.tv_usec < t2.tv_usec) {
			t.tv_usec = t1.tv_usec + 1000000L - t2.tv_usec;
			t.tv_sec--; /* Borrow a second. */
		} else {
			t.tv_usec = t1.tv_usec - t2.tv_usec;
		}
	}
	return t;
}

int tv_cmp(struct timeval t1, struct timeval t2)
{
	if ((t1.tv_sec < t2.tv_sec) ||
	    ((t1.tv_sec == t2.tv_sec) && (t1.tv_usec < t2.tv_usec))) {
		return -1;
	} else if ((t1.tv_sec > t2.tv_sec) ||
	           ((t1.tv_sec == t2.tv_sec) && (t1.tv_usec > t2.tv_usec))) {
		return 1;
	}
	return 0;
}

inline struct timespec ts_add(struct timespec t1, struct timespec t2)
{
	struct timespec t;
	t.tv_sec = t1.tv_sec + t2.tv_sec;
	t.tv_nsec = t1.tv_nsec + t2.tv_nsec;
	/* carry over seconds */
	if (t.tv_nsec >= 1000000000L) {
		t.tv_sec++;
		t.tv_nsec -= 1000000000L;
	}
	return t;
}

inline struct timeval tv_add(struct timeval t1, struct timeval t2)
{
	struct timeval t;
	t.tv_sec = t1.tv_sec + t2.tv_sec;
	t.tv_usec = t1.tv_usec + t2.tv_usec;
	/* carry over seconds */
	if (t.tv_usec >= 1E6L) {
		t.tv_sec++;
		t.tv_usec -= 1E6L;
	}
	return t;
}
