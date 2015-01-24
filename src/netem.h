#ifndef NETEM_H
#define NETEM_H

struct netem_params {
	char *iface;
	uint32_t delay;		/* milliseconds */
	uint32_t jitter;	/* milliseconds */
	uint32_t loss;		/* percentage, [0-100] */
};

int netem_init();
char **netem_list_ifaces();
int netem_set_params(const char *iface, struct netem_params *params);
int netem_get_params(char *iface, struct netem_params *params);

#endif
