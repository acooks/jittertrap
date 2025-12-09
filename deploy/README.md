# JitterTrap Deployment Guide

This guide covers deploying JitterTrap as a non-root service using Linux capabilities.

## Privilege Requirements

JitterTrap requires specific Linux capabilities for full functionality:

| Capability | Required For | Feature Impact if Missing |
|------------|--------------|--------------------------|
| `CAP_NET_RAW` | Packet capture (pcap) | **Cannot start** - essential |
| `CAP_NET_ADMIN` | Network impairment (netem/tc) | Impairment features disabled |
| `CAP_SYS_NICE` | Real-time scheduling (SCHED_FIFO) | Reduced timing accuracy |
| `CAP_NET_BIND_SERVICE` | Bind to ports < 1024 | Use `-p 8080` or higher port |

### Why capabilities can't be dropped after startup

JitterTrap's interface-switching feature restarts the packet capture thread when users select a different network interface via the web UI. This requires:
- `CAP_NET_RAW` for each new pcap handle
- `CAP_SYS_NICE` for setting thread priorities on the new thread

Therefore, these capabilities must be retained for the entire process lifetime.

## Deployment Options

### Option 1: Systemd Service (Recommended for Production)

1. **Create dedicated user:**
   ```bash
   sudo useradd -r -s /bin/false jittertrap
   ```

2. **Install the binary and web content:**
   ```bash
   sudo install -m 755 server/jt-server /usr/bin/
   sudo mkdir -p /usr/share/jittertrap/html
   sudo cp -r html5-client/output/* /usr/share/jittertrap/html/
   ```

3. **Install and enable service:**
   ```bash
   # Full capabilities (with impairment injection)
   sudo cp deploy/jittertrap.service /etc/systemd/system/

   # OR monitor-only (no impairment injection)
   sudo cp deploy/jittertrap-monitor.service /etc/systemd/system/jittertrap.service

   sudo systemctl daemon-reload
   sudo systemctl enable --now jittertrap
   ```

4. **Check status:**
   ```bash
   sudo systemctl status jittertrap
   sudo journalctl -u jittertrap -f
   ```

### Option 2: File Capabilities (Development/Manual Deployment)

Set capabilities directly on the binary:

```bash
# Full capabilities (using port 8080)
sudo setcap 'cap_net_raw,cap_net_admin,cap_sys_nice+ep' ./server/jt-server
./server/jt-server -p 8080 -r html5-client/output/

# Full capabilities (using port 80)
sudo setcap 'cap_net_raw,cap_net_admin,cap_sys_nice,cap_net_bind_service+ep' ./server/jt-server
./server/jt-server -r html5-client/output/

# Verify
getcap ./server/jt-server
```

**Note:** File capabilities are cleared when the binary is modified (e.g., recompiled).

### Option 3: Running as Root (Not Recommended)

For testing only:
```bash
sudo ./server/jt-server -p 8080 -r html5-client/output/
```

## Service Files

Two systemd service files are provided:

### `jittertrap.service` - Full Features
- All capabilities enabled
- Network impairment injection available
- Best for WAN emulation testing

### `jittertrap-monitor.service` - Monitor Only
- No `CAP_NET_ADMIN` capability
- Network impairment features disabled
- Suitable for passive network monitoring

## Security Hardening

Both service files include systemd hardening options:

| Option | Purpose |
|--------|---------|
| `NoNewPrivileges=yes` | Prevent privilege escalation |
| `ProtectSystem=strict` | Read-only filesystem except allowed paths |
| `ProtectHome=yes` | No access to home directories |
| `PrivateTmp=yes` | Isolated /tmp namespace |
| `ProtectKernelTunables=yes` | No access to /proc/sys, /sys |
| `RestrictNamespaces=yes` | Prevent namespace creation |
| `MemoryDenyWriteExecute=yes` | No writable+executable memory |
| `CapabilityBoundingSet=...` | Limit available capabilities |

## Troubleshooting

### Check capability status at startup

JitterTrap logs its capability status on startup. Check syslog or journalctl:
```bash
journalctl -u jittertrap | grep -E "(CAP_|Capability)"
```

### Missing capability warnings

If you see warnings like:
```
Missing 4 capabilities:
  CAP_NET_RAW: packet capture will fail
  CAP_NET_ADMIN: network impairment disabled
  CAP_SYS_NICE: real-time scheduling disabled
  CAP_NET_BIND_SERVICE: cannot bind to ports < 1024
To grant all: sudo setcap 'cap_net_raw,cap_net_admin,cap_sys_nice,cap_net_bind_service+ep' <binary>
```

Verify capabilities are set:
```bash
# For file capabilities
getcap /usr/bin/jt-server

# For systemd service
systemctl show jittertrap | grep -i cap
```

### Permission denied on packet capture

Ensure `CAP_NET_RAW` is available and the user is in the appropriate group for the network interface.

### Netem operations fail

Check that `CAP_NET_ADMIN` is available. For systemd, ensure `AmbientCapabilities` includes it.

## Port Configuration

By default, JitterTrap uses port 80 which requires root or `CAP_NET_BIND_SERVICE`. The systemd service files use port 8080 instead.

To use port 80:
1. Add `CAP_NET_BIND_SERVICE` to `AmbientCapabilities`
2. Change `-p 8080` to `-p 80` in `ExecStart`

Or use a reverse proxy (nginx, Apache) to forward from port 80.
