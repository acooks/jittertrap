#ifndef JT_SERVER_MSG_HANDLER_H
#define JT_SERVER_MSG_HANDLER_H

int jt_server_tick();
int jt_server_msg_receive(char *in, int len);

int jt_srv_send_iface_list();
int jt_srv_send_select_iface();
int jt_srv_send_netem_params();
int jt_srv_send_sample_period();

#endif
