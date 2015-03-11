#ifndef JITTERTRAP_H
#define JITTERTRAP_H

#define MAX_IFACE_LEN 16

#define MAX_JSON_TOKEN_LEN 256

#define MAX_JSON_MSG_LEN 4096

#define FILTERED_SAMPLES_PER_MSG 20
#define SAMPLES_PER_FRAME 20

/* raw samples must be an integer multiple of filtered samples */
static_assert((SAMPLES_PER_FRAME % FILTERED_SAMPLES_PER_MSG) == 0,
	      "Decimation requires SAMPLES_PER_FRAME to be an integer "
	      "multiple of FILTERED_SAMPLES_PER_MSG");

#endif
