/*
 * capabilities.c - Linux capability detection and management
 *
 * Uses libcap to detect available capabilities at runtime.
 */

#include <sys/capability.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <stdio.h>

#include "capabilities.h"

static struct jt_cap_status cap_status = {0};

/*
 * Check if a specific capability is in the effective set.
 */
static bool check_capability(cap_value_t cap)
{
	cap_t caps;
	cap_flag_value_t value;
	bool has_cap = false;

	caps = cap_get_proc();
	if (caps == NULL) {
		return false;
	}

	if (cap_get_flag(caps, cap, CAP_EFFECTIVE, &value) == 0) {
		has_cap = (value == CAP_SET);
	}

	cap_free(caps);
	return has_cap;
}

int caps_init(void)
{
	cap_status.is_root = (geteuid() == 0);
	cap_status.has_net_raw = check_capability(CAP_NET_RAW);
	cap_status.has_net_admin = check_capability(CAP_NET_ADMIN);
	cap_status.has_sys_nice = check_capability(CAP_SYS_NICE);
	cap_status.has_net_bind = check_capability(CAP_NET_BIND_SERVICE);
	cap_status.checked = true;

	return 0;
}

const struct jt_cap_status *caps_get_status(void)
{
	return &cap_status;
}

bool caps_can_capture(void)
{
	return cap_status.is_root || cap_status.has_net_raw;
}

bool caps_can_netem(void)
{
	return cap_status.is_root || cap_status.has_net_admin;
}

bool caps_can_realtime(void)
{
	return cap_status.is_root || cap_status.has_sys_nice;
}

bool caps_can_bind_low_port(void)
{
	return cap_status.is_root || cap_status.has_net_bind;
}

void caps_log_status(void)
{
	int missing_count = 0;
	char missing_caps[256] = "";
	char *p = missing_caps;
	int remaining = sizeof(missing_caps);

	if (cap_status.is_root) {
		syslog(LOG_NOTICE, "Running as root (all capabilities available)");
		return;
	}

	/* Count missing capabilities and build setcap string */
	if (!cap_status.has_net_raw) {
		int n = snprintf(p, remaining, "%scap_net_raw",
		                 missing_count ? "," : "");
		p += n;
		remaining -= n;
		missing_count++;
	}
	if (!cap_status.has_net_admin) {
		int n = snprintf(p, remaining, "%scap_net_admin",
		                 missing_count ? "," : "");
		p += n;
		remaining -= n;
		missing_count++;
	}
	if (!cap_status.has_sys_nice) {
		int n = snprintf(p, remaining, "%scap_sys_nice",
		                 missing_count ? "," : "");
		p += n;
		remaining -= n;
		missing_count++;
	}
	if (!cap_status.has_net_bind) {
		int n = snprintf(p, remaining, "%scap_net_bind_service",
		                 missing_count ? "," : "");
		p += n;
		remaining -= n;
		missing_count++;
	}

	if (missing_count == 0) {
		syslog(LOG_NOTICE, "All required capabilities available");
		return;
	}

	/* Log what's missing */
	syslog(LOG_WARNING, "Missing %d capabilit%s:",
	       missing_count, missing_count > 1 ? "ies" : "y");

	if (!caps_can_capture()) {
		syslog(LOG_WARNING,
		       "  CAP_NET_RAW: packet capture will fail");
	}
	if (!caps_can_netem()) {
		syslog(LOG_WARNING,
		       "  CAP_NET_ADMIN: network impairment disabled");
	}
	if (!caps_can_realtime()) {
		syslog(LOG_WARNING,
		       "  CAP_SYS_NICE: real-time scheduling disabled");
	}
	if (!caps_can_bind_low_port()) {
		syslog(LOG_WARNING,
		       "  CAP_NET_BIND_SERVICE: cannot bind to ports < 1024");
	}

	/* Provide consolidated fix command */
	syslog(LOG_WARNING,
	       "To grant all: sudo setcap '%s+ep' <binary>", missing_caps);
}
