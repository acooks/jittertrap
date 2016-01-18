#ifndef JT_SERVER_MSG_HANDLER_H
#define JT_SERVER_MSG_HANDLER_H

#define MAX_JSON_TOKEN_LEN 256

#define MAX_JSON_MSG_LEN 3000

#define MESSAGES_PER_SECOND 50

#define USECS_PER_SECOND 1000000
#define FILTERED_SAMPLES_PER_MSG                                               \
	(USECS_PER_SECOND / SAMPLE_PERIOD_US / MESSAGES_PER_SECOND)

#define SAMPLES_PER_FRAME                                                      \
	(USECS_PER_SECOND / SAMPLE_PERIOD_US / MESSAGES_PER_SECOND)

#ifndef static_assert
#define static_assert _Static_assert
#endif

/* raw samples must be an integer multiple of filtered samples */
static_assert((SAMPLES_PER_FRAME % FILTERED_SAMPLES_PER_MSG) == 0,
              "Decimation requires SAMPLES_PER_FRAME to be an integer "
              "multiple of FILTERED_SAMPLES_PER_MSG");

int jt_server_tick();
int jt_server_msg_receive(char *in);

int jt_srv_send_iface_list();
int jt_srv_send_select_iface();
int jt_srv_send_netem_params();
int jt_srv_send_sample_period();

#endif
