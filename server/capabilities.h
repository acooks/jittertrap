/*
 * capabilities.h - Linux capability detection and management
 *
 * Provides runtime detection of available capabilities to enable
 * graceful degradation when running without full privileges.
 */

#ifndef CAPABILITIES_H
#define CAPABILITIES_H

#include <stdbool.h>

/* Capability status structure */
struct jt_cap_status {
	bool has_net_raw;        /* CAP_NET_RAW - packet capture */
	bool has_net_admin;      /* CAP_NET_ADMIN - netem/tc */
	bool has_sys_nice;       /* CAP_SYS_NICE - RT scheduling */
	bool has_net_bind;       /* CAP_NET_BIND_SERVICE - port <1024 */
	bool is_root;            /* Running as UID 0 */
	bool checked;            /* Capabilities have been checked */
};

/*
 * Initialize and check capabilities.
 * Must be called early in main() before any privileged operations.
 * Returns 0 on success, -1 on error.
 */
int caps_init(void);

/*
 * Get current capability status.
 * Returns pointer to static status structure.
 */
const struct jt_cap_status *caps_get_status(void);

/*
 * Check if specific features are available.
 * Returns true if the required capability is present or running as root.
 */
bool caps_can_capture(void);       /* CAP_NET_RAW or root */
bool caps_can_netem(void);         /* CAP_NET_ADMIN or root */
bool caps_can_realtime(void);      /* CAP_SYS_NICE or root */
bool caps_can_bind_low_port(void); /* CAP_NET_BIND_SERVICE or root */

/*
 * Log current capability status to syslog.
 * Logs at LOG_NOTICE for available capabilities,
 * LOG_WARNING for missing capabilities.
 */
void caps_log_status(void);

#endif /* CAPABILITIES_H */
