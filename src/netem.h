#ifndef NETEM_H
#define NETEM_H

struct netem_params {
	char *iface;
	int delay;
	int jitter;
	int loss;
};

int netem_init();
char **netem_list_ifaces();
int netem_set_params(const char *iface, struct netem_params *params);
int netem_get_params(char *iface, struct netem_params *params);

#endif
