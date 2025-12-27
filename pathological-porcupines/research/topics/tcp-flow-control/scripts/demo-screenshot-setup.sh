#!/bin/bash
# Demo Screenshot Setup Script
# Run this to set up demo scenarios for JitterTrap screenshots
#
# Usage: ./demo-screenshot-setup.sh [scenario]
# Scenarios: receiver-stall, sender-stall, network-impairment, compound, bbr-vs-cubic

set -e

SCENARIO=${1:-receiver-stall}
DURATION=${2:-60}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PP_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
DEMO_DIR="$SCRIPT_DIR/.."

echo "=============================================="
echo "JitterTrap Demo Screenshot Setup"
echo "=============================================="
echo "Scenario: $SCENARIO"
echo "Duration: ${DURATION}s"
echo ""

# Clean up any existing netem rules
cleanup() {
    echo "Cleaning up..."
    sudo ip netns exec pp-observer tc qdisc del dev veth-src root 2>/dev/null || true
    sudo pkill -f "python3.*server.py" 2>/dev/null || true
    sudo pkill -f "python3.*client.py" 2>/dev/null || true
}

trap cleanup EXIT

# Reset netem
sudo ip netns exec pp-observer tc qdisc del dev veth-src root 2>/dev/null || true

case "$SCENARIO" in
    receiver-stall)
        echo "=== Receiver Starvation Demo ==="
        echo "What to observe in JitterTrap:"
        echo "  - Zero-window events in Flow Details"
        echo "  - Periodic throughput drops"
        echo "  - Low RTT (not a network problem)"
        echo ""

        # Start slow receiver (50ms delay between reads)
        sudo ip netns exec pp-dest python3 "$PP_DIR/tcp-flow-control/receiver-starvation/server.py" \
            --port 9999 --delay 50 &
        sleep 1

        # Start sender
        sudo ip netns exec pp-source python3 "$PP_DIR/tcp-flow-control/receiver-starvation/client.py" \
            --host 10.0.1.2 --port 9999 --rate 10 &
        ;;

    sender-stall)
        echo "=== Sender Stall Demo ==="
        echo "What to observe in JitterTrap:"
        echo "  - Throughput dips every 2 seconds"
        echo "  - Window stays healthy (non-zero)"
        echo "  - NO zero-window events"
        echo "  - Large inter-packet gaps"
        echo ""

        # Start fast receiver
        sudo ip netns exec pp-dest python3 "$PP_DIR/tcp-timing/sender-stall/server.py" \
            --port 9999 &
        sleep 1

        # Start stuttering sender (500ms stall every 2s)
        sudo ip netns exec pp-source python3 "$PP_DIR/tcp-timing/sender-stall/client.py" \
            --host 10.0.1.2 --port 9999 --stall 500 --interval 2000 --pattern periodic &
        ;;

    network-impairment)
        echo "=== Network Impairment Demo ==="
        echo "What to observe in JitterTrap:"
        echo "  - Increased retransmits"
        echo "  - High RTT variance"
        echo "  - Degraded throughput"
        echo ""

        # Apply 100ms RTT with 20ms jitter
        sudo ip netns exec pp-observer tc qdisc add dev veth-src root netem delay 50ms 10ms

        # Start receiver
        sudo ip netns exec pp-dest python3 "$PP_DIR/tcp-flow-control/receiver-starvation/server.py" \
            --port 9999 &
        sleep 1

        # Start sender
        sudo ip netns exec pp-source python3 "$PP_DIR/tcp-flow-control/receiver-starvation/client.py" \
            --host 10.0.1.2 --port 9999 --rate 10 &
        ;;

    compound)
        echo "=== Compound Problem Demo (Masked Receiver) ==="
        echo "What to observe in JitterTrap:"
        echo "  - Retransmits present (network problem visible)"
        echo "  - Zero-window = 0 (receiver problem MASKED)"
        echo "  - Fix network first, then re-test"
        echo ""

        # Apply network impairment with loss
        sudo ip netns exec pp-observer tc qdisc add dev veth-src root netem delay 50ms loss 1%

        # Start slow receiver
        sudo ip netns exec pp-dest python3 "$PP_DIR/tcp-flow-control/receiver-starvation/server.py" \
            --port 9999 --delay 50 &
        sleep 1

        # Start sender
        sudo ip netns exec pp-source python3 "$PP_DIR/tcp-flow-control/receiver-starvation/client.py" \
            --host 10.0.1.2 --port 9999 --rate 10 &
        ;;

    bbr-vs-cubic)
        echo "=== BBR vs CUBIC Demo ==="
        echo "What to observe:"
        echo "  - Run with CUBIC first, note throughput"
        echo "  - Then run with BBR, compare"
        echo "  - BBR should show ~8x advantage at 1% loss"
        echo ""

        # Apply 1% loss
        sudo ip netns exec pp-observer tc qdisc add dev veth-src root netem loss 1%

        # Set congestion control
        echo "Current CC algorithm:"
        sudo ip netns exec pp-source sysctl net.ipv4.tcp_congestion_control

        # Start receiver
        sudo ip netns exec pp-dest python3 "$PP_DIR/tcp-flow-control/receiver-starvation/server.py" \
            --port 9999 &
        sleep 1

        # Start sender
        sudo ip netns exec pp-source python3 "$PP_DIR/tcp-flow-control/receiver-starvation/client.py" \
            --host 10.0.1.2 --port 9999 --rate 10 &
        ;;

    *)
        echo "Unknown scenario: $SCENARIO"
        echo "Available: receiver-stall, sender-stall, network-impairment, compound, bbr-vs-cubic"
        exit 1
        ;;
esac

echo ""
echo "=============================================="
echo "Demo running for ${DURATION}s"
echo ""
echo "Start JitterTrap:"
echo "  sudo ip netns exec pp-observer jt-server -i br0"
echo ""
echo "Open browser to:"
echo "  http://localhost:8080"
echo ""
echo "Take screenshot when ready!"
echo "=============================================="

sleep $DURATION

echo ""
echo "Demo complete."
