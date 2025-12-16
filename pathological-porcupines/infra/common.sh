#!/bin/bash
# common.sh - Shared functions for pathological-porcupines test infrastructure
# Source this file from other scripts: source "$(dirname "$0")/common.sh"

# Namespace names
PP_NS_SOURCE="pp-source"
PP_NS_OBSERVER="pp-observer"
PP_NS_DEST="pp-dest"

# IP addresses
PP_SRC_IP="10.0.1.1"
PP_DST_IP="10.0.1.2"
PP_MGMT_IP="10.0.0.2"

# Execute command in a namespace
ns_exec() {
    local ns="$1"
    shift
    ip netns exec "$ns" "$@"
}

# Wait for a TCP port to be listening in a namespace
wait_for_port() {
    local ns="$1"
    local port="$2"
    local timeout="${3:-10}"
    local elapsed=0

    while ! ns_exec "$ns" ss -tln 2>/dev/null | grep -q ":${port} "; do
        sleep 0.5
        elapsed=$((elapsed + 1))
        if [[ $elapsed -ge $((timeout * 2)) ]]; then
            echo "Timeout waiting for port $port in $ns" >&2
            return 1
        fi
    done
    return 0
}

# Check if topology exists
topology_exists() {
    ip netns list 2>/dev/null | grep -q "^${PP_NS_OBSERVER}$"
}

# Ensure topology exists, create if not
ensure_topology() {
    if ! topology_exists; then
        local script_dir
        script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
        "$script_dir/setup-topology.sh"
    fi
}

# Get test duration from test's config or default
get_test_duration() {
    local test_dir="$1"
    local default="${2:-15}"

    if [[ -f "$test_dir/config.sh" ]]; then
        # shellcheck source=/dev/null
        source "$test_dir/config.sh"
        echo "${DURATION:-$default}"
    else
        echo "$default"
    fi
}

# Reset all impairments to clean state
reset_impairments() {
    ns_exec "$PP_NS_OBSERVER" tc qdisc del dev veth-src root 2>/dev/null || true
    ns_exec "$PP_NS_OBSERVER" tc qdisc del dev veth-dst root 2>/dev/null || true
    echo "Impairments cleared"
}

# Reset MTU to defaults (1500)
reset_mtu() {
    ns_exec "$PP_NS_SOURCE" ip link set veth-src mtu 1500 2>/dev/null || true
    ns_exec "$PP_NS_DEST" ip link set veth-dst mtu 1500 2>/dev/null || true
    ns_exec "$PP_NS_OBSERVER" ip link set veth-src mtu 1500 2>/dev/null || true
    ns_exec "$PP_NS_OBSERVER" ip link set veth-dst mtu 1500 2>/dev/null || true
    ns_exec "$PP_NS_OBSERVER" ip link set br0 mtu 1500 2>/dev/null || true
    echo "MTU reset to 1500"
}

# Reset all network configuration to defaults
reset_network() {
    reset_impairments
    reset_mtu
}

# Print a banner for test output
print_banner() {
    local title="$1"
    local width=60
    local padding=$(( (width - ${#title} - 2) / 2 ))

    echo ""
    printf '=%.0s' $(seq 1 $width)
    echo ""
    printf '%*s %s %*s\n' "$padding" "" "$title" "$padding" ""
    printf '=%.0s' $(seq 1 $width)
    echo ""
}

# Print test result
print_result() {
    local status="$1"
    local test_name="$2"
    local message="${3:-}"

    echo ""
    printf '=%.0s' $(seq 1 60)
    echo ""
    if [[ "$status" == "PASS" ]]; then
        echo "TEST PASSED: $test_name"
    else
        echo "TEST FAILED: $test_name"
    fi
    if [[ -n "$message" ]]; then
        echo "  $message"
    fi
    printf '=%.0s' $(seq 1 60)
    echo ""
}

# Check if we're running as root
require_root() {
    if [[ $EUID -ne 0 ]]; then
        echo "Error: This script must be run as root (use sudo)" >&2
        exit 1
    fi
}
