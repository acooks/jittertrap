#!/bin/bash
# setup-topology.sh - Create pathological-porcupines test topology
# Architecture: L2 bridging with same subnet (no routing)
# Requires: root/sudo, iproute2
# Idempotent: safe to run multiple times

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Namespace names
NS_SOURCE="pp-source"
NS_OBSERVER="pp-observer"
NS_DEST="pp-dest"

# IP addressing
# Management network (host <-> observer): 10.0.0.0/24
HOST_MGMT_IP="10.0.0.1/24"
OBS_MGMT_IP="10.0.0.2/24"

# Test network: 10.0.1.0/24 (SAME subnet for L2 bridging)
SRC_IP="10.0.1.1/24"
DST_IP="10.0.1.2/24"
# Note: No IP on bridge - pure L2 forwarding

echo "Creating pathological-porcupines test topology..."

# Step 1: Create namespaces (skip if exist)
for ns in $NS_SOURCE $NS_OBSERVER $NS_DEST; do
    if ! ip netns list | grep -q "^${ns}\b"; then
        ip netns add "$ns"
        echo "  Created namespace: $ns"
    else
        echo "  Namespace exists: $ns"
    fi
done

# Step 2: Create veth pairs
# Host <-> Observer (management)
if ! ip link show veth-host &>/dev/null; then
    ip link add veth-host type veth peer name veth-mgmt
    ip link set veth-mgmt netns $NS_OBSERVER
    echo "  Created veth pair: veth-host <-> veth-mgmt"
else
    echo "  Veth pair exists: veth-host <-> veth-mgmt"
fi

# Source <-> Observer (test traffic)
if ! ip netns exec $NS_OBSERVER ip link show veth-src &>/dev/null 2>&1; then
    ip link add veth-src-tmp type veth peer name veth-src
    ip link set veth-src-tmp netns $NS_SOURCE
    ip link set veth-src netns $NS_OBSERVER
    # Rename in source namespace to veth-src
    ip netns exec $NS_SOURCE ip link set veth-src-tmp name veth-src
    echo "  Created veth pair: pp-source:veth-src <-> pp-observer:veth-src"
else
    echo "  Veth pair exists: pp-source:veth-src <-> pp-observer:veth-src"
fi

# Observer <-> Dest (test traffic)
if ! ip netns exec $NS_OBSERVER ip link show veth-dst &>/dev/null 2>&1; then
    ip link add veth-dst-tmp type veth peer name veth-dst
    ip link set veth-dst-tmp netns $NS_DEST
    ip link set veth-dst netns $NS_OBSERVER
    # Rename in dest namespace to veth-dst
    ip netns exec $NS_DEST ip link set veth-dst-tmp name veth-dst
    echo "  Created veth pair: pp-observer:veth-dst <-> pp-dest:veth-dst"
else
    echo "  Veth pair exists: pp-observer:veth-dst <-> pp-dest:veth-dst"
fi

# Step 3: Create L2 bridge in observer namespace (NO IP - pure L2)
if ! ip netns exec $NS_OBSERVER ip link show br0 &>/dev/null 2>&1; then
    ip netns exec $NS_OBSERVER ip link add br0 type bridge
    ip netns exec $NS_OBSERVER ip link set veth-src master br0
    ip netns exec $NS_OBSERVER ip link set veth-dst master br0
    echo "  Created bridge: br0 (veth-src + veth-dst)"
else
    echo "  Bridge exists: br0"
fi

# Step 4: Disable IPv6 on test interfaces (reduces noise in observations)
echo "Disabling IPv6 on test interfaces..."
ip netns exec $NS_SOURCE sysctl -qw net.ipv6.conf.all.disable_ipv6=1
ip netns exec $NS_SOURCE sysctl -qw net.ipv6.conf.default.disable_ipv6=1
ip netns exec $NS_DEST sysctl -qw net.ipv6.conf.all.disable_ipv6=1
ip netns exec $NS_DEST sysctl -qw net.ipv6.conf.default.disable_ipv6=1
ip netns exec $NS_OBSERVER sysctl -qw net.ipv6.conf.veth-src.disable_ipv6=1
ip netns exec $NS_OBSERVER sysctl -qw net.ipv6.conf.veth-dst.disable_ipv6=1
ip netns exec $NS_OBSERVER sysctl -qw net.ipv6.conf.br0.disable_ipv6=1

# Step 5: Assign IPv4 addresses
# Host side (management)
if ! ip addr show dev veth-host 2>/dev/null | grep -q "10.0.0.1"; then
    ip addr add $HOST_MGMT_IP dev veth-host
    echo "  Assigned IP: veth-host = 10.0.0.1"
fi
ip link set veth-host up

# Observer namespace (management only - no IP on test interfaces)
if ! ip netns exec $NS_OBSERVER ip addr show dev veth-mgmt 2>/dev/null | grep -q "10.0.0.2"; then
    ip netns exec $NS_OBSERVER ip addr add $OBS_MGMT_IP dev veth-mgmt
    echo "  Assigned IP: veth-mgmt = 10.0.0.2"
fi
ip netns exec $NS_OBSERVER ip link set veth-mgmt up
ip netns exec $NS_OBSERVER ip link set br0 up
ip netns exec $NS_OBSERVER ip link set veth-src up
ip netns exec $NS_OBSERVER ip link set veth-dst up
ip netns exec $NS_OBSERVER ip link set lo up

# Source namespace (test network - same subnet as dest)
if ! ip netns exec $NS_SOURCE ip addr show dev veth-src 2>/dev/null | grep -q "10.0.1.1"; then
    ip netns exec $NS_SOURCE ip addr add $SRC_IP dev veth-src
    echo "  Assigned IP: pp-source:veth-src = 10.0.1.1"
fi
ip netns exec $NS_SOURCE ip link set veth-src up
ip netns exec $NS_SOURCE ip link set lo up

# Dest namespace (test network - same subnet as source)
if ! ip netns exec $NS_DEST ip addr show dev veth-dst 2>/dev/null | grep -q "10.0.1.2"; then
    ip netns exec $NS_DEST ip addr add $DST_IP dev veth-dst
    echo "  Assigned IP: pp-dest:veth-dst = 10.0.1.2"
fi
ip netns exec $NS_DEST ip link set veth-dst up
ip netns exec $NS_DEST ip link set lo up

# Step 6: Disable TSO/GSO for accurate packet capture
# Without this, packets are coalesced and pcap shows jumbo frames
echo "Disabling TSO/GSO on test interfaces..."
ip netns exec $NS_SOURCE ethtool -K veth-src tso off gso off 2>/dev/null || true
ip netns exec $NS_OBSERVER ethtool -K veth-src tso off gso off 2>/dev/null || true
ip netns exec $NS_OBSERVER ethtool -K veth-dst tso off gso off 2>/dev/null || true
ip netns exec $NS_DEST ethtool -K veth-dst tso off gso off 2>/dev/null || true

# Step 7: Verify connectivity
echo ""
echo "Verifying topology..."
sleep 0.5  # Allow ARP to settle

if ip netns exec $NS_SOURCE ping -c 1 -W 2 10.0.1.2 &>/dev/null; then
    echo "  [OK] Source can reach Dest (10.0.1.1 -> 10.0.1.2)"
else
    echo "  [FAIL] Connectivity check failed!"
    echo "  Debug: Check bridge membership with 'ip netns exec pp-observer bridge link'"
    exit 1
fi

if ping -c 1 -W 2 10.0.0.2 &>/dev/null; then
    echo "  [OK] Host can reach Observer management (10.0.0.1 -> 10.0.0.2)"
else
    echo "  [WARN] Host cannot reach observer management interface"
    echo "  JitterTrap UI may not be accessible"
fi

echo ""
echo "=========================================="
echo "Topology created successfully"
echo "=========================================="
echo ""
echo "Namespaces:"
echo "  Source:   $NS_SOURCE (10.0.1.1)"
echo "  Observer: $NS_OBSERVER (mgmt: 10.0.0.2, bridge: br0)"
echo "  Dest:     $NS_DEST (10.0.1.2)"
echo ""
echo "To run a test:"
echo "  sudo ./infra/run-test.sh <test-path>"
echo ""
echo "Available tests:"
find "$SCRIPT_DIR/.." -name "client.py" -o -name "sender.py" 2>/dev/null | \
    sed "s|$SCRIPT_DIR/../||" | sed 's|/[^/]*\.py$||' | sort -u | \
    while read -r test; do
        # Extract description from README (line 5, after "## Overview")
        readme="$SCRIPT_DIR/../$test/README.md"
        if [[ -f "$readme" ]]; then
            desc=$(sed -n '5p' "$readme" | head -c 50)
            printf "  %-36s %s\n" "$test" "$desc"
        else
            echo "  $test"
        fi
    done
echo ""
echo "Example:"
echo "  sudo ./infra/run-test.sh tcp-timing/persist-timer"
echo ""
