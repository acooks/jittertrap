#!/bin/bash
# run-test.sh - Run a pathological-porcupines test with JitterTrap observation
#
# Usage:
#   run-test.sh <test-path> [options]
#
# Options:
#   --auto               Start test automatically after 5 seconds (no user prompt)
#   --impairment <prof>  Apply impairment profile before test
#   --no-jittertrap      Skip starting JitterTrap (for debugging)
#   --no-browser         Don't auto-open browser (just print URL)
#   --reset              Reset impairments/MTU after test
#
# Examples:
#   run-test.sh tcp-timing/persist-timer
#   run-test.sh tcp-timing/persist-timer --auto
#   run-test.sh udp/bursty-sender --impairment wan
#   run-test.sh tcp-lifecycle/rst-storm --auto --no-browser

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PP_ROOT="$(dirname "$SCRIPT_DIR")"

# shellcheck source=common.sh
source "$SCRIPT_DIR/common.sh"

require_root

# Default options
AUTO_MODE=false
IMPAIRMENT=""
RUN_JITTERTRAP=true
OPEN_BROWSER=true
RESET_AFTER=false
JT_BINARY="${JT_BINARY:-$PP_ROOT/../server/jt-server}"
JT_RESOURCES="${JT_RESOURCES:-$PP_ROOT/../html5-client/output}"
JT_URL="http://10.0.0.2:8080"

# Parse arguments
if [[ $# -lt 1 ]]; then
    echo "Usage: run-test.sh <test-path> [options]"
    echo ""
    echo "Options:"
    echo "  --auto               Start test automatically (no prompt)"
    echo "  --impairment <prof>  Apply impairment profile (wan, lossy, etc.)"
    echo "  --no-jittertrap      Skip JitterTrap"
    echo "  --no-browser         Don't auto-open browser"
    echo "  --reset              Reset network after test"
    echo ""
    echo "Examples:"
    echo "  run-test.sh tcp-timing/persist-timer"
    echo "  run-test.sh udp/bursty-sender --auto --impairment wan"
    exit 1
fi

TEST_PATH="$1"
shift

while [[ $# -gt 0 ]]; do
    case $1 in
        --auto)
            AUTO_MODE=true
            shift
            ;;
        --impairment)
            IMPAIRMENT="$2"
            shift 2
            ;;
        --no-jittertrap)
            RUN_JITTERTRAP=false
            shift
            ;;
        --no-browser)
            OPEN_BROWSER=false
            shift
            ;;
        --reset)
            RESET_AFTER=true
            shift
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

# Validate test exists
TEST_DIR="$PP_ROOT/$TEST_PATH"
if [[ ! -d "$TEST_DIR" ]]; then
    echo "Error: Test directory not found: $TEST_DIR" >&2
    echo "Available tests:" >&2
    find "$PP_ROOT" -name "server.py" -o -name "sender.py" 2>/dev/null | \
        sed "s|$PP_ROOT/||" | sed 's|/[^/]*\.py$||' | sort -u | head -20 >&2
    exit 1
fi

# Ensure topology exists
if ! topology_exists; then
    echo "Creating test topology..."
    "$SCRIPT_DIR/setup-topology.sh"
fi

# Apply impairment if specified
if [[ -n "$IMPAIRMENT" ]]; then
    echo "Applying impairment profile: $IMPAIRMENT"
    "$SCRIPT_DIR/add-impairment.sh" "$IMPAIRMENT"
fi

# Start JitterTrap if requested
JT_PID=""
if [[ "$RUN_JITTERTRAP" == "true" ]]; then
    if [[ ! -x "$JT_BINARY" ]]; then
        echo "Warning: JitterTrap binary not found at $JT_BINARY" >&2
        echo "Set JT_BINARY environment variable or use --no-jittertrap" >&2
        RUN_JITTERTRAP=false
    else
        echo "Starting JitterTrap..."
        # Note: We don't use --interface because libwebsockets has issues binding
        # to interface names inside network namespaces. Binding to 0.0.0.0 inside
        # the namespace is fine - it's only accessible via the namespace's IPs.
        ns_exec "$PP_NS_OBSERVER" "$JT_BINARY" --allowed veth-src:veth-dst -r "$JT_RESOURCES" -p 8080 &
        JT_PID=$!
        sleep 2

        # Verify it started
        if ! kill -0 "$JT_PID" 2>/dev/null; then
            echo "Error: JitterTrap failed to start" >&2
            exit 1
        fi

        # Open browser if requested
        if [[ "$OPEN_BROWSER" == "true" ]]; then
            if [[ -n "${SUDO_USER:-}" ]] && command -v xdg-open &>/dev/null; then
                echo "Opening browser to $JT_URL ..."
                # Run as original user with correct runtime dir
                sudo -u "$SUDO_USER" XDG_RUNTIME_DIR="/run/user/$SUDO_UID" xdg-open "$JT_URL" 2>/dev/null &
            elif command -v xdg-open &>/dev/null; then
                echo "Opening browser to $JT_URL ..."
                xdg-open "$JT_URL" 2>/dev/null &
            else
                echo "Open $JT_URL in your browser"
            fi
        fi
    fi
fi

# Print banner
echo ""
echo "========================================"
echo "  Pathological Porcupines Test Runner"
echo "========================================"
echo ""
echo "  Test:     $TEST_PATH"
if [[ -n "$IMPAIRMENT" ]]; then
echo "  Impairment: $IMPAIRMENT"
fi
if [[ "$RUN_JITTERTRAP" == "true" ]]; then
echo "  JitterTrap: http://10.0.0.2:8080"
fi
echo ""
echo "========================================"

# Print JitterTrap observation instructions
if [[ "$RUN_JITTERTRAP" == "true" ]]; then
    echo ""
    echo "JitterTrap Setup:"
    echo "  Select interface: veth-src or veth-dst"
    echo ""

    # Extract what to watch from README if available
    readme="$TEST_DIR/README.md"
    if [[ -f "$readme" ]]; then
        echo "What to observe:"
        # Extract JitterTrap Indicators table rows (lines starting with |)
        sed -n '/## JitterTrap Indicators/,/^##/p' "$readme" 2>/dev/null | \
            grep '^|' | grep -v '^| Metric' | grep -v '^|--' | \
            head -5 | \
            while IFS='|' read -r _ metric expected why _; do
                metric=$(echo "$metric" | xargs)
                expected=$(echo "$expected" | xargs)
                if [[ -n "$metric" && -n "$expected" ]]; then
                    echo "  - $metric: $expected"
                fi
            done
    fi
fi
echo ""

# Wait for user or auto-start
if [[ "$AUTO_MODE" == "true" ]]; then
    echo "Auto mode: starting test in 5 seconds..."
    sleep 5
else
    if [[ "$RUN_JITTERTRAP" == "true" ]]; then
        echo "Connect to JitterTrap at http://10.0.0.2:8080"
        echo ""
    fi
    echo "Press ENTER when ready to start test (Ctrl+C to abort)..."
    read -r
fi

# Determine test scripts (server.py/client.py or receiver.py/sender.py)
if [[ -f "$TEST_DIR/server.py" ]]; then
    DEST_SCRIPT="$TEST_DIR/server.py"
    SRC_SCRIPT="$TEST_DIR/client.py"
elif [[ -f "$TEST_DIR/receiver.py" ]]; then
    DEST_SCRIPT="$TEST_DIR/receiver.py"
    SRC_SCRIPT="$TEST_DIR/sender.py"
else
    echo "Error: No server.py or receiver.py found in $TEST_DIR" >&2
    [[ -n "$JT_PID" ]] && kill "$JT_PID" 2>/dev/null || true
    exit 1
fi

# Verify scripts exist
if [[ ! -f "$SRC_SCRIPT" ]]; then
    echo "Error: Source script not found: $SRC_SCRIPT" >&2
    [[ -n "$JT_PID" ]] && kill "$JT_PID" 2>/dev/null || true
    exit 1
fi

# Start destination (server/receiver)
echo "Starting receiver in $PP_NS_DEST namespace..."
ns_exec "$PP_NS_DEST" python3 "$DEST_SCRIPT" --port 9999 &
DEST_PID=$!

# Wait for server to start listening
if ! wait_for_port "$PP_NS_DEST" 9999 5; then
    echo "Warning: Server may not have started correctly" >&2
fi

# Start source (client/sender)
echo "Starting sender in $PP_NS_SOURCE namespace..."
echo ""
echo "--- Test Output ---"
ns_exec "$PP_NS_SOURCE" python3 "$SRC_SCRIPT" --host "$PP_DST_IP" --port 9999
SRC_EXIT=$?
echo "--- End Test Output ---"
echo ""

# Wait for destination to finish
wait "$DEST_PID" 2>/dev/null || true
DEST_EXIT=$?

# Stop JitterTrap
if [[ -n "$JT_PID" ]]; then
    kill "$JT_PID" 2>/dev/null || true
    wait "$JT_PID" 2>/dev/null || true
fi

# Reset network if requested
if [[ "$RESET_AFTER" == "true" ]]; then
    echo "Resetting network configuration..."
    reset_network
fi

# Report results
if [[ $SRC_EXIT -eq 0 && $DEST_EXIT -eq 0 ]]; then
    print_result "PASS" "$TEST_PATH"
    exit 0
else
    print_result "FAIL" "$TEST_PATH" "Sender exit: $SRC_EXIT, Receiver exit: $DEST_EXIT"
    exit 1
fi
