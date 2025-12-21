#!/bin/bash
# cleanup-processes.sh - Kill orphaned test processes in pathological-porcupines namespaces
#
# Usage:
#   cleanup-processes.sh          # Clean all stale processes
#   cleanup-processes.sh --list   # Just list what would be cleaned (dry run)
#
# This script finds and kills:
# - tcpdump processes in pp-observer namespace
# - jt-server processes in pp-observer namespace
# - python test processes in pp-source and pp-dest namespaces

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

DRY_RUN=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --list|--dry-run|-n)
            DRY_RUN=true
            shift
            ;;
        *)
            echo "Unknown option: $1" >&2
            echo "Usage: cleanup-processes.sh [--list]" >&2
            exit 1
            ;;
    esac
done

# Check if running as root (required for namespace access)
if [[ $EUID -ne 0 ]]; then
    echo "Note: Running without root - may not see all namespace processes" >&2
fi

# Check if topology exists
if ! topology_exists; then
    echo "Test topology does not exist (no pp-observer namespace)"
    echo "Nothing to clean up."
    exit 0
fi

echo "Scanning for orphaned test processes..."
echo ""

found_any=false

# Function to find processes in a namespace by command name pattern
find_ns_processes() {
    local ns="$1"
    local pattern="$2"
    local desc="$3"

    local pids
    pids=$(ip netns pids "$ns" 2>/dev/null | while read pid; do
        if [[ -f "/proc/$pid/comm" ]]; then
            local comm
            comm=$(cat "/proc/$pid/comm" 2>/dev/null || true)
            if [[ "$comm" =~ $pattern ]]; then
                echo "$pid"
            fi
        fi
    done)

    if [[ -n "$pids" ]]; then
        found_any=true
        echo "$desc in $ns:"
        for pid in $pids; do
            local cmdline
            cmdline=$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null | head -c 80 || echo "unknown")
            echo "  PID $pid: $cmdline"
            if [[ "$DRY_RUN" == "false" ]]; then
                kill "$pid" 2>/dev/null || true
            fi
        done
        if [[ "$DRY_RUN" == "false" ]]; then
            sleep 0.3
            for pid in $pids; do
                kill -9 "$pid" 2>/dev/null || true
            done
        fi
        echo ""
    fi
}

# Find tcpdump in observer
find_ns_processes "$PP_NS_OBSERVER" "^tcpdump$" "tcpdump"

# Find jt-server in observer
find_ns_processes "$PP_NS_OBSERVER" "^jt-server$" "jt-server"

# Find python in source/dest (pathological tests)
find_python_processes() {
    local ns="$1"
    local pids
    pids=$(ip netns pids "$ns" 2>/dev/null | while read pid; do
        if [[ -f "/proc/$pid/comm" ]]; then
            local comm
            comm=$(cat "/proc/$pid/comm" 2>/dev/null || true)
            if [[ "$comm" =~ ^python ]]; then
                # Check if it's a pathological-porcupines test
                if grep -q "pathological" "/proc/$pid/cmdline" 2>/dev/null; then
                    echo "$pid"
                fi
            fi
        fi
    done)

    if [[ -n "$pids" ]]; then
        found_any=true
        echo "python test processes in $ns:"
        for pid in $pids; do
            local cmdline
            cmdline=$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null | head -c 80 || echo "unknown")
            echo "  PID $pid: $cmdline"
            if [[ "$DRY_RUN" == "false" ]]; then
                kill "$pid" 2>/dev/null || true
            fi
        done
        if [[ "$DRY_RUN" == "false" ]]; then
            sleep 0.3
            for pid in $pids; do
                kill -9 "$pid" 2>/dev/null || true
            done
        fi
        echo ""
    fi
}

find_python_processes "$PP_NS_SOURCE"
find_python_processes "$PP_NS_DEST"

# Also check for jt-server running outside namespaces
if pgrep -x jt-server >/dev/null 2>&1; then
    found_any=true
    echo "jt-server outside namespaces:"
    pgrep -ax jt-server | while read line; do
        echo "  $line"
    done
    if [[ "$DRY_RUN" == "false" ]]; then
        pkill -x jt-server 2>/dev/null || true
    fi
    echo ""
fi

if [[ "$found_any" == "false" ]]; then
    echo "No orphaned processes found."
else
    if [[ "$DRY_RUN" == "true" ]]; then
        echo "Dry run - no processes were killed."
        echo "Run without --list to actually clean up."
    else
        echo "Cleanup complete."
    fi
fi
