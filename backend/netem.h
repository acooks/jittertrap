#ifndef NETEM_H
#define NETEM_H

struct netem_params {
	uint32_t delay;		/* milliseconds */
	uint32_t jitter;	/* milliseconds */
	uint32_t loss;		/* percentage, [0-100] */
	char iface[MAX_IFACE_LEN];
};

int netem_init();
int is_iface_allowed(const char *needle);
char **netem_list_ifaces();
int netem_set_params(const char *iface, struct netem_params *params);
int netem_get_params(char *iface, struct netem_params *params);


#endif
