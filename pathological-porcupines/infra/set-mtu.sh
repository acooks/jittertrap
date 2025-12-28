#!/bin/bash
# set-mtu.sh - Set MTU on test path interfaces
#
# Usage:
#   set-mtu.sh <mtu>            Set MTU on all test interfaces
#   set-mtu.sh <mtu> src        Set MTU on source side only
#   set-mtu.sh <mtu> dst        Set MTU on destination side only
#   set-mtu.sh <mtu> all        Set MTU on all interfaces (same as no arg)
#   set-mtu.sh reset            Reset MTU to 1500 on all interfaces
#
# Examples:
#   set-mtu.sh 1400             # Simulate tunnel overhead
#   set-mtu.sh 576              # Minimum IPv4 MTU
#   set-mtu.sh 1280 src         # IPv6 minimum on source side
#   set-mtu.sh reset            # Reset to defaults

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

require_root

if [[ $# -lt 1 ]]; then
    echo "Usage: set-mtu.sh <mtu|reset> [src|dst|all]"
    echo ""
    echo "Examples:"
    echo "  set-mtu.sh 1400        # All interfaces to 1400"
    echo "  set-mtu.sh 576 src     # Source side only"
    echo "  set-mtu.sh reset       # Reset to 1500"
    exit 1
fi

MTU="$1"
SIDE="${2:-all}"

# Handle reset
if [[ "$MTU" == "reset" ]]; then
    reset_mtu
    exit 0
fi

# Validate MTU is a number
if ! [[ "$MTU" =~ ^[0-9]+$ ]]; then
    echo "Error: MTU must be a positive integer" >&2
    exit 1
fi

# Validate MTU range
if [[ $MTU -lt 68 || $MTU -gt 65535 ]]; then
    echo "Error: MTU must be between 68 and 65535" >&2
    exit 1
fi

set_mtu() {
    local iface="$1"
    local ns="$2"
    ns_exec "$ns" ip link set dev "$iface" mtu "$MTU"
    echo "  Set $ns:$iface MTU to $MTU"
}

case "$SIDE" in
    src|source)
        echo "Setting MTU on source side..."
        set_mtu veth-src "$PP_NS_SOURCE"
        set_mtu veth-src "$PP_NS_OBSERVER"
        ;;
    dst|dest|destination)
        echo "Setting MTU on destination side..."
        set_mtu veth-dst "$PP_NS_OBSERVER"
        set_mtu veth-dst "$PP_NS_DEST"
        ;;
    all)
        echo "Setting MTU on all test interfaces..."
        set_mtu veth-src "$PP_NS_SOURCE"
        set_mtu veth-src "$PP_NS_OBSERVER"
        set_mtu veth-dst "$PP_NS_OBSERVER"
        set_mtu veth-dst "$PP_NS_DEST"
        # Also set bridge MTU
        ns_exec "$PP_NS_OBSERVER" ip link set dev br0 mtu "$MTU"
        echo "  Set pp-observer:br0 MTU to $MTU"
        ;;
    *)
        echo "Error: Unknown side '$SIDE' (use src, dst, or all)" >&2
        exit 1
        ;;
esac

echo ""
echo "MTU configuration complete"
