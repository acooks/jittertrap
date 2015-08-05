#include <sys/time.h>
#include <time.h>
#include "timeywimey.h"

/* Calculate the absolute difference between t1 and t2. */
struct timespec ts_absdiff(struct timespec t1, struct timespec t2)
{
	struct timespec t;
	if ((t1.tv_sec < t2.tv_sec) ||
            ((t1.tv_sec == t2.tv_sec) &&
	     (t1.tv_nsec <= t2.tv_nsec)))
	{
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
			t.tv_sec--;  /* Borrow a second. */
		} else {
			t.tv_nsec = t1.tv_nsec - t2.tv_nsec;
		}
	}
	return t;
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

