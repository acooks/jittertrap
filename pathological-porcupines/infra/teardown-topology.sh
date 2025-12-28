#!/bin/bash
# teardown-topology.sh - Remove pathological-porcupines test topology
# Safe to run even if topology doesn't exist or is partially created

set -euo pipefail

echo "Removing pathological-porcupines test topology..."

# Remove host-side veth (peer in namespace auto-deleted)
if ip link show veth-host &>/dev/null 2>&1; then
    ip link del veth-host
    echo "  Removed: veth-host"
fi

# Remove namespaces (also removes all interfaces inside)
for ns in pp-source pp-observer pp-dest; do
    if ip netns list | grep -q "^${ns}\b"; then
        ip netns del "$ns"
        echo "  Removed namespace: $ns"
    fi
done

echo ""
echo "Topology removed successfully"
