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
#   --pcap [file]        Capture packets on bridge (default: screenshots/<timestamp>/<test>.pcap)
#
# Examples:
#   run-test.sh tcp-timing/persist-timer
#   run-test.sh tcp-timing/persist-timer --auto
#   run-test.sh udp/bursty-sender --impairment wan
#   run-test.sh tcp-lifecycle/rst-storm --auto --no-browser
#   run-test.sh tcp-timing/persist-timer --auto --pcap

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
TEARDOWN_AFTER=true
CAPTURE_SCREENSHOTS=false
CAPTURE_PCAP=false
PCAP_FILE=""
JT_BINARY="${JT_BINARY:-$PP_ROOT/../server/jt-server}"
JT_RESOURCES="${JT_RESOURCES:-$PP_ROOT/../html5-client/output}"
JT_URL="http://10.0.0.2:8080"

# Screenshot capture state
SCREENSHOT_FIFO=""
SCREENSHOT_PID=""
SCREENSHOT_OUTPUT_DIR=""
SCREENSHOT_FD=""

# PCAP capture state
TCPDUMP_PID=""

# JitterTrap state
JT_PID=""

# Destination process state
DEST_PID=""

# Kill a process reliably, waiting for it to exit
# Args: $1=pid, $2=description, $3=signal (default TERM)
kill_process() {
    local pid="$1"
    local desc="$2"
    local sig="${3:-TERM}"

    if [[ -z "$pid" ]] || ! kill -0 "$pid" 2>/dev/null; then
        return 0
    fi

    echo "  Stopping $desc (pid $pid)..."
    kill -"$sig" "$pid" 2>/dev/null || true

    # Wait up to 2 seconds for graceful shutdown
    local timeout=4
    while kill -0 "$pid" 2>/dev/null && [[ $timeout -gt 0 ]]; do
        sleep 0.5
        timeout=$((timeout - 1))
    done

    # Force kill if still running
    if kill -0 "$pid" 2>/dev/null; then
        echo "  Force killing $desc..."
        kill -9 "$pid" 2>/dev/null || true
        sleep 0.2
    fi
}

# Kill any stale tcpdump processes in the observer namespace
cleanup_stale_tcpdump() {
    # Find tcpdump processes running in the pp-observer namespace
    local pids
    pids=$(ip netns pids "$PP_NS_OBSERVER" 2>/dev/null | while read pid; do
        if [[ -f "/proc/$pid/comm" ]] && grep -q "^tcpdump$" "/proc/$pid/comm" 2>/dev/null; then
            echo "$pid"
        fi
    done)

    if [[ -n "$pids" ]]; then
        echo "  Cleaning stale tcpdump processes in $PP_NS_OBSERVER..."
        for pid in $pids; do
            kill -INT "$pid" 2>/dev/null || true
        done
        sleep 0.5
        for pid in $pids; do
            kill -9 "$pid" 2>/dev/null || true
        done
    fi
}

# Kill any stale jt-server processes in the observer namespace
cleanup_stale_jt_server() {
    # Find jt-server processes running in the pp-observer namespace
    local pids
    pids=$(ip netns pids "$PP_NS_OBSERVER" 2>/dev/null | while read pid; do
        if [[ -f "/proc/$pid/comm" ]] && grep -q "^jt-server$" "/proc/$pid/comm" 2>/dev/null; then
            echo "$pid"
        fi
    done)

    if [[ -n "$pids" ]]; then
        echo "  Cleaning stale jt-server processes in $PP_NS_OBSERVER..."
        for pid in $pids; do
            kill "$pid" 2>/dev/null || true
        done
        sleep 0.5
        for pid in $pids; do
            kill -9 "$pid" 2>/dev/null || true
        done
    fi
}

# Kill any stale python test processes in source and dest namespaces
cleanup_stale_python() {
    for ns in "$PP_NS_SOURCE" "$PP_NS_DEST"; do
        local pids
        pids=$(ip netns pids "$ns" 2>/dev/null | while read pid; do
            if [[ -f "/proc/$pid/comm" ]] && grep -q "^python" "/proc/$pid/comm" 2>/dev/null; then
                # Check if it's a pathological-porcupines test
                if grep -q "pathological" "/proc/$pid/cmdline" 2>/dev/null; then
                    echo "$pid"
                fi
            fi
        done)

        if [[ -n "$pids" ]]; then
            echo "  Cleaning stale python processes in $ns..."
            for pid in $pids; do
                kill "$pid" 2>/dev/null || true
            done
            sleep 0.3
            for pid in $pids; do
                kill -9 "$pid" 2>/dev/null || true
            done
        fi
    done
}

# Cleanup function for trap - ensures all background processes are killed
cleanup_all() {
    local exit_code=$?
    local did_cleanup=false

    # Check if any cleanup is needed before printing header
    if [[ -n "$TCPDUMP_PID" ]] || [[ -n "$SCREENSHOT_PID" ]] || \
       [[ -n "$JT_PID" ]] || [[ -n "$DEST_PID" ]]; then
        if kill -0 "$TCPDUMP_PID" 2>/dev/null || \
           kill -0 "$SCREENSHOT_PID" 2>/dev/null || \
           kill -0 "$JT_PID" 2>/dev/null || \
           kill -0 "$DEST_PID" 2>/dev/null; then
            echo ""
            echo "Cleaning up..."
            did_cleanup=true
        fi
    fi

    # Stop tcpdump (use INT for clean pcap file closure)
    if [[ -n "$TCPDUMP_PID" ]]; then
        kill_process "$TCPDUMP_PID" "tcpdump" "INT"
        TCPDUMP_PID=""
    fi

    # Also clean up any orphaned tcpdump in namespace
    if topology_exists; then
        cleanup_stale_tcpdump
    fi

    # Stop screenshot controller
    if [[ -n "$SCREENSHOT_PID" ]]; then
        kill_process "$SCREENSHOT_PID" "screenshot controller"
        SCREENSHOT_PID=""
    fi

    # Close screenshot FIFO fd
    if [[ -n "${SCREENSHOT_FD:-}" ]]; then
        exec 3>&- 2>/dev/null || true
        SCREENSHOT_FD=""
    fi
    [[ -n "$SCREENSHOT_FIFO" ]] && rm -f "$SCREENSHOT_FIFO"

    # Stop JitterTrap
    if [[ -n "$JT_PID" ]]; then
        kill_process "$JT_PID" "jt-server"
        JT_PID=""
    fi

    # Also clean up any orphaned jt-server in namespace
    if topology_exists; then
        cleanup_stale_jt_server
    fi

    # Stop destination (server/receiver)
    if [[ -n "$DEST_PID" ]]; then
        kill_process "$DEST_PID" "destination process"
        DEST_PID=""
    fi

    # Also clean up any orphaned python test processes in namespaces
    if topology_exists; then
        cleanup_stale_python
    fi

    [[ "$did_cleanup" == "true" ]] && echo "Cleanup complete."
    exit $exit_code
}

# Set trap for cleanup on exit, interrupt, or error
trap cleanup_all EXIT INT TERM

# Parse arguments
if [[ $# -lt 1 ]]; then
    echo "Usage: run-test.sh <test-path> [options]"
    echo ""
    echo "Options:"
    echo "  --auto               Start test automatically (no prompt)"
    echo "  --impairment <prof>  Apply impairment profile (wan, lossy, etc.)"
    echo "  --no-jittertrap      Skip JitterTrap"
    echo "  --no-browser         Don't auto-open browser"
    echo "  --no-teardown        Keep namespaces after test (for debugging)"
    echo "  --capture-screenshots  Capture screenshots when test passes"
    echo "  --pcap [file]        Capture packets on bridge interface"
    echo ""
    echo "Examples:"
    echo "  run-test.sh tcp-timing/persist-timer"
    echo "  run-test.sh udp/bursty-sender --auto --impairment wan"
    echo "  run-test.sh tcp-timing/persist-timer --auto --pcap"
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
        --no-teardown)
            TEARDOWN_AFTER=false
            shift
            ;;
        --capture-screenshots)
            CAPTURE_SCREENSHOTS=true
            shift
            ;;
        --pcap)
            CAPTURE_PCAP=true
            # Check if next arg is a filename (not another option)
            if [[ $# -gt 1 ]] && [[ ! "$2" =~ ^-- ]]; then
                PCAP_FILE="$2"
                shift
            fi
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

# Screenshot capture functions
setup_screenshot_capture() {
    local test_path="$1"
    local readme="$PP_ROOT/$test_path/README.md"

    # Create output directory with timestamp
    local timestamp
    timestamp=$(date +%Y-%m-%d_%H%M%S)
    SCREENSHOT_OUTPUT_DIR="$PP_ROOT/screenshots/$timestamp"
    mkdir -p "$SCREENSHOT_OUTPUT_DIR"

    # Fix ownership if running under sudo so the controller can write
    if [[ -n "${SUDO_USER:-}" ]]; then
        chown "$SUDO_USER:$SUDO_USER" "$SCREENSHOT_OUTPUT_DIR"
    fi

    # Create named pipe for IPC
    SCREENSHOT_FIFO="/tmp/pp-screenshot-$$.fifo"
    mkfifo "$SCREENSHOT_FIFO"
    # Make FIFO accessible to non-root user
    chmod 666 "$SCREENSHOT_FIFO"

    # Get screenshot config from README
    local config
    config=$("$SCRIPT_DIR/get-screenshot-config.py" "$readme")

    echo "Starting screenshot controller..."
    echo "  Output: $SCREENSHOT_OUTPUT_DIR"

    # Start screenshot controller in background
    # Run as original user if we're running under sudo
    if [[ -n "${SUDO_USER:-}" ]]; then
        sudo -u "$SUDO_USER" node "$SCRIPT_DIR/screenshot-controller.js" \
            "$SCREENSHOT_FIFO" \
            "$SCREENSHOT_OUTPUT_DIR" \
            "$test_path" \
            "$config" &
    else
        node "$SCRIPT_DIR/screenshot-controller.js" \
            "$SCREENSHOT_FIFO" \
            "$SCREENSHOT_OUTPUT_DIR" \
            "$test_path" \
            "$config" &
    fi
    SCREENSHOT_PID=$!

    # Give controller time to start listening
    sleep 1

    # Verify it started
    if ! kill -0 "$SCREENSHOT_PID" 2>/dev/null; then
        echo "Warning: Screenshot controller failed to start" >&2
        CAPTURE_SCREENSHOTS=false
        rm -f "$SCREENSHOT_FIFO"
        return 1
    fi

    # Open a persistent file descriptor to keep FIFO open for writing
    # This prevents EOF when individual echo commands complete
    exec 3>"$SCREENSHOT_FIFO"
    SCREENSHOT_FD=3

    # Send INIT command to start browser and wait for data
    echo "INIT" >&3

    # Wait for controller to signal READY via ready file
    local ready_file="$SCREENSHOT_OUTPUT_DIR/.ready"
    echo "  Waiting for JitterTrap UI to be ready..."
    local ready_timeout=60  # 60 second timeout
    while [[ ! -f "$ready_file" ]] && [[ $ready_timeout -gt 0 ]]; do
        # Check if controller is still running
        if ! kill -0 "$SCREENSHOT_PID" 2>/dev/null; then
            echo "Warning: Screenshot controller exited during initialization" >&2
            CAPTURE_SCREENSHOTS=false
            return 1
        fi
        sleep 1
        ready_timeout=$((ready_timeout - 1))
    done

    if [[ -f "$ready_file" ]]; then
        echo "  JitterTrap UI ready"
        rm -f "$ready_file"
    else
        echo "Warning: Timeout waiting for JitterTrap UI, proceeding anyway" >&2
    fi

    return 0
}

trigger_screenshot() {
    local line="$1"

    # Only trigger if screenshot capture is enabled and FD is open
    if [[ "$CAPTURE_SCREENSHOTS" != "true" ]] || [[ -z "${SCREENSHOT_FD:-}" ]]; then
        return
    fi

    # Trigger on [PASS] patterns
    if [[ "$line" =~ \[PASS\] ]]; then
        echo "CAPTURE_ALL" >&${SCREENSHOT_FD} 2>/dev/null || true
    fi
}

cleanup_screenshot() {
    if [[ -n "$SCREENSHOT_PID" ]] && kill -0 "$SCREENSHOT_PID" 2>/dev/null; then
        echo "Stopping screenshot controller..."

        # Send SHUTDOWN via open file descriptor and close it
        # Closing the FD causes EOF on the FIFO reader, triggering shutdown
        if [[ -n "${SCREENSHOT_FD:-}" ]]; then
            echo "SHUTDOWN" >&${SCREENSHOT_FD} 2>/dev/null || true
            # Close fd 3 explicitly (the FD we opened)
            exec 3>&- 2>/dev/null || true
            SCREENSHOT_FD=""
        fi

        # Wait for clean shutdown (allow time for captures to complete)
        # Check quickly at first (process usually exits fast), then slow down
        local waited=0
        # Fast checks for first 2 seconds (20 x 0.1s)
        while kill -0 "$SCREENSHOT_PID" 2>/dev/null && [[ $waited -lt 20 ]]; do
            sleep 0.1
            waited=$((waited + 1))
        done
        # If still running, slower checks for up to 10 more seconds
        while kill -0 "$SCREENSHOT_PID" 2>/dev/null && [[ $waited -lt 40 ]]; do
            sleep 0.5
            waited=$((waited + 1))
        done
        # Check if it exited cleanly
        if kill -0 "$SCREENSHOT_PID" 2>/dev/null; then
            # Still running after timeout - force kill
            echo "  Screenshot controller did not exit cleanly, killing..."
            kill -9 "$SCREENSHOT_PID" 2>/dev/null || true
            sleep 0.5
        fi
    fi

    # Close FD if still open (fd 3)
    if [[ -n "${SCREENSHOT_FD:-}" ]]; then
        exec 3>&- 2>/dev/null || true
        SCREENSHOT_FD=""
    fi

    # Clean up FIFO
    [[ -n "$SCREENSHOT_FIFO" ]] && rm -f "$SCREENSHOT_FIFO"

    # Report output location
    if [[ -n "$SCREENSHOT_OUTPUT_DIR" ]] && [[ -d "$SCREENSHOT_OUTPUT_DIR" ]]; then
        local count
        count=$(find "$SCREENSHOT_OUTPUT_DIR" -name "*.png" 2>/dev/null | wc -l)
        if [[ $count -gt 0 ]]; then
            echo ""
            echo "Screenshots saved: $SCREENSHOT_OUTPUT_DIR ($count files)"
        fi
    fi
}

# PCAP capture functions
setup_pcap_capture() {
    local test_path="$1"

    # Determine output file
    if [[ -z "$PCAP_FILE" ]]; then
        # Use screenshot output dir if available, otherwise create one
        if [[ -z "$SCREENSHOT_OUTPUT_DIR" ]]; then
            local timestamp
            timestamp=$(date +%Y-%m-%d_%H%M%S)
            SCREENSHOT_OUTPUT_DIR="$PP_ROOT/screenshots/$timestamp"
            mkdir -p "$SCREENSHOT_OUTPUT_DIR"
            # Fix ownership if running under sudo
            if [[ -n "${SUDO_USER:-}" ]]; then
                chown "$SUDO_USER:$SUDO_USER" "$SCREENSHOT_OUTPUT_DIR"
            fi
        fi
        PCAP_FILE="$SCREENSHOT_OUTPUT_DIR/$(echo "$test_path" | tr '/' '_').pcap"
    fi

    # Ensure parent directory exists
    mkdir -p "$(dirname "$PCAP_FILE")"

    echo "Starting packet capture on br0..."
    echo "  Output: $PCAP_FILE"

    # Start tcpdump in observer namespace on bridge interface
    # -i br0: capture on bridge
    # -s 0: capture full packets (no truncation)
    # -w: write to file
    # -U: packet-buffered output (flush after each packet)
    ns_exec "$PP_NS_OBSERVER" tcpdump -i br0 -s 0 -U -w "$PCAP_FILE" 2>/dev/null &
    TCPDUMP_PID=$!

    # Give tcpdump time to start
    sleep 0.5

    # Verify it started
    if ! kill -0 "$TCPDUMP_PID" 2>/dev/null; then
        echo "Warning: tcpdump failed to start" >&2
        CAPTURE_PCAP=false
        return 1
    fi

    return 0
}

cleanup_pcap_capture() {
    if [[ -n "$TCPDUMP_PID" ]] && kill -0 "$TCPDUMP_PID" 2>/dev/null; then
        echo "Stopping packet capture..."

        # Send SIGINT for clean shutdown (tcpdump writes final stats)
        kill -INT "$TCPDUMP_PID" 2>/dev/null || true

        # Wait for clean shutdown
        local timeout=10
        while kill -0 "$TCPDUMP_PID" 2>/dev/null && [[ $timeout -gt 0 ]]; do
            sleep 0.5
            timeout=$((timeout - 1))
        done

        # Force kill if still running
        if kill -0 "$TCPDUMP_PID" 2>/dev/null; then
            kill -9 "$TCPDUMP_PID" 2>/dev/null || true
        fi

        TCPDUMP_PID=""
    fi

    # Report pcap file location and stats
    if [[ -n "$PCAP_FILE" ]] && [[ -f "$PCAP_FILE" ]]; then
        local size
        size=$(ls -lh "$PCAP_FILE" 2>/dev/null | awk '{print $5}')
        local packets
        packets=$(tcpdump -r "$PCAP_FILE" 2>/dev/null | wc -l || echo "unknown")
        echo ""
        echo "PCAP saved: $PCAP_FILE ($size, $packets packets)"

        # Fix ownership if running under sudo
        if [[ -n "${SUDO_USER:-}" ]]; then
            chown "$SUDO_USER:$SUDO_USER" "$PCAP_FILE"
        fi
    fi
}

# Apply impairment if specified
if [[ -n "$IMPAIRMENT" ]]; then
    echo "Applying impairment profile: $IMPAIRMENT"
    "$SCRIPT_DIR/add-impairment.sh" "$IMPAIRMENT"
fi

# Screenshot capture uses its own browser, so disable xdg-open browser
# Do this BEFORE starting JitterTrap to avoid opening two browsers
if [[ "$CAPTURE_SCREENSHOTS" == "true" ]]; then
    OPEN_BROWSER=false
fi

# Clean up any stale processes from previous runs
if topology_exists; then
    cleanup_stale_python
    cleanup_stale_tcpdump
fi

# Start JitterTrap if requested
if [[ "$RUN_JITTERTRAP" == "true" ]]; then
    # Also clean up jt-server
    if topology_exists; then
        cleanup_stale_jt_server
    fi
    # Also check for any jt-server running outside namespaces
    if pgrep -x jt-server >/dev/null 2>&1; then
        echo "Cleaning up stale jt-server processes..."
        pkill -x jt-server 2>/dev/null || true
        sleep 1
    fi

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
        # Wait for JitterTrap to fully initialize (server + sampling threads)
        sleep 4

        # Verify it started
        if ! kill -0 "$JT_PID" 2>/dev/null; then
            echo "Error: JitterTrap failed to start" >&2
            exit 1
        fi

        # Open browser if requested (disabled when --capture-screenshots is used)
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

# Start screenshot capture if requested (requires JitterTrap to be running)
if [[ "$CAPTURE_SCREENSHOTS" == "true" ]]; then
    if [[ "$RUN_JITTERTRAP" == "true" ]]; then
        setup_screenshot_capture "$TEST_PATH"
    else
        echo "Warning: Screenshot capture requires JitterTrap (--no-jittertrap disables it)" >&2
        CAPTURE_SCREENSHOTS=false
    fi
fi

# Start pcap capture if requested
if [[ "$CAPTURE_PCAP" == "true" ]]; then
    setup_pcap_capture "$TEST_PATH"
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
if [[ "$CAPTURE_PCAP" == "true" ]]; then
echo "  PCAP:     $PCAP_FILE"
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
# Note: Don't hardcode port - let each test use its default
# (TCP tests use 9999, RTP tests use 5004 for JitterTrap detection)
echo "Starting receiver in $PP_NS_DEST namespace..."
ns_exec "$PP_NS_DEST" python3 "$DEST_SCRIPT" &
DEST_PID=$!

# Wait for server/receiver to initialize
# For TCP tests, we could check port; for UDP we just wait
sleep 1

# Start source (client/sender)
echo "Starting sender in $PP_NS_SOURCE namespace..."
echo ""
echo "--- Test Output ---"

# Set up log file if capturing screenshots
LOG_FILE=""
if [[ -n "$SCREENSHOT_OUTPUT_DIR" ]]; then
    LOG_FILE="$SCREENSHOT_OUTPUT_DIR/$(echo "$TEST_PATH" | tr '/' '_').log"
fi

# Run sender and parse output for screenshot triggers
if [[ "$CAPTURE_SCREENSHOTS" == "true" ]] && [[ -n "${SCREENSHOT_FD:-}" ]]; then
    # Capture output, log it, and trigger screenshots on [PASS] patterns

    # Create temp files for IPC (subshell can't set parent variables)
    EXIT_FILE=$(mktemp)
    TRIGGER_FLAG=$(mktemp)
    rm -f "$TRIGGER_FLAG"  # Remove so we can test existence

    # Run command and capture exit status
    {
        ns_exec "$PP_NS_SOURCE" python3 "$SRC_SCRIPT" --host "$PP_DST_IP" 2>&1
        echo $? > "$EXIT_FILE"
    } | while IFS= read -r line; do
        echo "$line"
        [[ -n "$LOG_FILE" ]] && echo "$line" >> "$LOG_FILE"
        # Trigger on first [PASS] pattern only (avoid duplicate captures)
        # Write to fd 3 which stays open in the parent shell
        if [[ "$line" =~ \[PASS\] ]] && [[ ! -f "$TRIGGER_FLAG" ]]; then
            echo "CAPTURE_ALL" >> "$SCREENSHOT_FIFO" 2>/dev/null || true
            touch "$TRIGGER_FLAG"
        fi
    done

    # Read exit status from temp file
    SRC_EXIT=$(cat "$EXIT_FILE" 2>/dev/null || echo 1)
    rm -f "$EXIT_FILE" "$TRIGGER_FLAG"
else
    # Simple execution without screenshot capture
    ns_exec "$PP_NS_SOURCE" python3 "$SRC_SCRIPT" --host "$PP_DST_IP"
    SRC_EXIT=$?
fi

echo "--- End Test Output ---"
echo ""

# Wait for destination to finish
wait "$DEST_PID" 2>/dev/null || true
DEST_EXIT=$?

# Clean up screenshot capture (before stopping JitterTrap)
if [[ "$CAPTURE_SCREENSHOTS" == "true" ]]; then
    cleanup_screenshot
    SCREENSHOT_PID=""  # Clear so trap doesn't double-kill
fi

# Clean up pcap capture
if [[ "$CAPTURE_PCAP" == "true" ]]; then
    cleanup_pcap_capture
    TCPDUMP_PID=""  # Clear so trap doesn't double-kill
fi

# Stop JitterTrap - use kill_process for reliable termination
if [[ -n "$JT_PID" ]]; then
    echo "Stopping JitterTrap..."
    kill_process "$JT_PID" "jt-server"
    JT_PID=""  # Clear so trap doesn't double-kill
fi

# Also clean up any orphaned jt-server in namespace (belt and suspenders)
if topology_exists; then
    cleanup_stale_jt_server
fi

# Clear destination PID so trap doesn't try to kill it
DEST_PID=""

# Report results and cleanup
if [[ $SRC_EXIT -eq 0 && $DEST_EXIT -eq 0 ]]; then
    print_result "PASS" "$TEST_PATH"

    # Tear down topology after successful test (unless --no-teardown)
    if [[ "$TEARDOWN_AFTER" == "true" ]]; then
        echo ""
        echo "Tearing down test topology..."
        "$SCRIPT_DIR/teardown-topology.sh"
    fi

    exit 0
else
    print_result "FAIL" "$TEST_PATH" "Sender exit: $SRC_EXIT, Receiver exit: $DEST_EXIT"

    # On failure, keep topology for debugging unless explicitly requested
    if [[ "$TEARDOWN_AFTER" == "true" ]]; then
        echo ""
        echo "Note: Topology preserved for debugging. Run teardown-topology.sh to clean up."
    fi

    exit 1
fi
