#ifndef NETEM_H
#define NETEM_H

struct netem_params {
	uint32_t delay;  /* milliseconds */
	uint32_t jitter; /* milliseconds */
	uint32_t loss;   /* percentage, [0-100] */
	char iface[MAX_IFACE_LEN];
};

int netem_init(void);
int is_iface_allowed(const char *needle);
const char *get_allowed_ifaces(void);
void set_allowed_ifaces(const char *ifaces);
char **netem_list_ifaces(void);
int netem_set_params(const char *iface, struct netem_params *params);
int netem_get_params(char *iface, struct netem_params *params);

/* Start a background thread that monitors NETLINK_ROUTE for link
 * additions/removals. When a change is detected, the cached interface list
 * is refreshed and the supplied callback is invoked from the monitor thread.
 * The callback is rate-limited; bursts of link events coalesce into a single
 * call.
 */
int netem_monitor_start(void (*on_link_change)(void));
void netem_monitor_stop(void);

#endif
