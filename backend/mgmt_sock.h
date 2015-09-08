#ifndef MGMT_SOCK_H
#define MGMT_SOCK_H

int mgmt_sock_init(const char *http_port, const char *docroot);

int mgmt_sock_main(void (*cb)(void), int period);

void mgmt_sock_stats_send(char *json_msg);



#endif
