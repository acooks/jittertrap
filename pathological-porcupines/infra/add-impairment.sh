#!/bin/bash
# add-impairment.sh - Apply network impairments using tc/netem
#
# Usage:
#   add-impairment.sh <profile>                    Apply profile to forward path only
#   add-impairment.sh <profile> --bidir            Apply profile to both directions
#   add-impairment.sh custom <netem args...>       Custom netem configuration
#   add-impairment.sh custom --bidir <netem args...>
#
# Profiles:
#   clean      - Remove all impairments
#   wan        - 50ms +/- 10ms delay (normal distribution)
#   lossy      - 1% packet loss
#   congested  - 100ms delay + 1 Mbit rate limit
#   reorder    - 25% packet reordering
#   jitter     - 20ms +/- 40ms delay (pareto distribution)
#   satellite  - 300ms +/- 50ms delay + 0.5% loss
#
# Examples:
#   add-impairment.sh wan
#   add-impairment.sh wan --bidir
#   add-impairment.sh custom delay 100ms loss 2%
#   add-impairment.sh custom --bidir delay 50ms 25ms distribution normal

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

require_root

if [[ $# -lt 1 ]]; then
    echo "Usage: add-impairment.sh <profile|custom> [--bidir] [netem args...]"
    echo ""
    echo "Profiles: clean, wan, lossy, congested, reorder, jitter, satellite"
    echo ""
    echo "Examples:"
    echo "  add-impairment.sh wan                    # 50ms delay, forward only"
    echo "  add-impairment.sh wan --bidir            # 50ms delay, both directions"
    echo "  add-impairment.sh custom delay 100ms     # Custom config"
    exit 1
fi

PROFILE="$1"
shift

BIDIR=false
if [[ "${1:-}" == "--bidir" ]]; then
    BIDIR=true
    shift
fi

# Interfaces in observer namespace
IFACE_FWD="veth-src"   # Source -> Observer (forward path)
IFACE_REV="veth-dst"   # Observer -> Dest (reverse path)

apply_netem() {
    local iface="$1"
    shift

    # Clear existing qdisc
    ns_exec "$PP_NS_OBSERVER" tc qdisc del dev "$iface" root 2>/dev/null || true

    # Apply new config if args provided
    if [[ $# -gt 0 ]]; then
        ns_exec "$PP_NS_OBSERVER" tc qdisc add dev "$iface" root netem "$@"
    fi
}

apply_profile() {
    local iface="$1"
    local profile="$2"

    case "$profile" in
        clean)
            apply_netem "$iface"
            ;;
        wan)
            # Typical WAN latency
            apply_netem "$iface" delay 50ms 10ms distribution normal
            ;;
        lossy)
            # Random packet loss
            apply_netem "$iface" loss 1%
            ;;
        congested)
            # High delay + rate limit (bufferbloat simulation)
            apply_netem "$iface" delay 100ms rate 1mbit
            ;;
        reorder)
            # Packet reordering
            apply_netem "$iface" delay 10ms reorder 75% 50%
            ;;
        jitter)
            # High jitter with pareto distribution
            apply_netem "$iface" delay 20ms 40ms distribution pareto
            ;;
        satellite)
            # Satellite link characteristics
            apply_netem "$iface" delay 300ms 50ms loss 0.5%
            ;;
        *)
            echo "Unknown profile: $profile" >&2
            echo "Available: clean, wan, lossy, congested, reorder, jitter, satellite" >&2
            exit 1
            ;;
    esac
}

# Apply to forward path
if [[ "$PROFILE" == "custom" ]]; then
    if [[ $# -eq 0 ]]; then
        echo "Error: 'custom' requires netem arguments" >&2
        exit 1
    fi
    apply_netem "$IFACE_FWD" "$@"
    echo "Applied custom impairment to $IFACE_FWD: netem $*"
else
    apply_profile "$IFACE_FWD" "$PROFILE"
    echo "Applied '$PROFILE' profile to $IFACE_FWD"
fi

# Apply to reverse path if bidirectional
if [[ "$BIDIR" == "true" ]]; then
    if [[ "$PROFILE" == "custom" ]]; then
        apply_netem "$IFACE_REV" "$@"
        echo "Applied custom impairment to $IFACE_REV: netem $*"
    else
        apply_profile "$IFACE_REV" "$PROFILE"
        echo "Applied '$PROFILE' profile to $IFACE_REV"
    fi
fi

# Show current configuration
echo ""
echo "Current impairment configuration:"
echo "  Forward ($IFACE_FWD):"
if qdisc=$(ns_exec "$PP_NS_OBSERVER" tc qdisc show dev "$IFACE_FWD" 2>/dev/null | grep netem); then
    echo "    $qdisc"
else
    echo "    (none)"
fi
echo "  Reverse ($IFACE_REV):"
if qdisc=$(ns_exec "$PP_NS_OBSERVER" tc qdisc show dev "$IFACE_REV" 2>/dev/null | grep netem); then
    echo "    $qdisc"
else
    echo "    (none)"
fi
