#ifndef JT_SERVER_MSG_HANDLER_H
#define JT_SERVER_MSG_HANDLER_H

int jt_server_tick(void);
int jt_server_msg_receive(char *in, int len);

int jt_srv_send_iface_list(void);
int jt_srv_send_select_iface(void);
int jt_srv_send_netem_params(void);
int jt_srv_send_sample_period(void);
int jt_srv_send_pcap_config(void);
int jt_srv_send_pcap_status(void);
int jt_srv_send_video_init(void);  /* Send video_init message with codec info */
int jt_srv_send_vlc_status(void);  /* Send VLC passthrough status */
void jt_srv_set_client_addr(const char *addr);  /* Set current client IP for VLC passthrough */
int jt_srv_resume(void);
int jt_srv_pause(void);
int jt_srv_is_ready(void);  /* Returns 1 if server is RUNNING, 0 otherwise */
#endif
