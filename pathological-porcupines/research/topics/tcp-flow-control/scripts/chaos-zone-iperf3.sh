#!/bin/bash
# chaos-zone-iperf3.sh - Run iperf3 experiments in chaos zone conditions
# This serves as a sanity check against our custom measurement tool

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EXPERIMENT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RESULTS_DIR="$EXPERIMENT_DIR/results/chaos-zone-iperf3"

mkdir -p "$RESULTS_DIR"

# Ensure topology exists
if ! ip netns list 2>/dev/null | grep -q "^pp-source\b"; then
    echo "ERROR: pathological-porcupines topology not found"
    echo "Run: sudo $EXPERIMENT_DIR/../../../infra/setup-topology.sh"
    exit 1
fi

# Function to set network conditions
set_netem() {
    local delay_ms=$1
    local jitter_ms=$2
    local loss_pct=${3:-0}

    # Clear existing qdisc (interface names are veth-src and veth-dst in pp-observer)
    sudo ip netns exec pp-observer tc qdisc del dev veth-src root 2>/dev/null || true
    sudo ip netns exec pp-observer tc qdisc del dev veth-dst root 2>/dev/null || true

    if [[ "$delay_ms" -gt 0 ]] || [[ "$jitter_ms" -gt 0 ]] || [[ "$loss_pct" != "0" ]]; then
        # Add delay/jitter/loss on both directions
        sudo ip netns exec pp-observer tc qdisc add dev veth-src root netem delay ${delay_ms}ms ${jitter_ms}ms loss ${loss_pct}%
        sudo ip netns exec pp-observer tc qdisc add dev veth-dst root netem delay ${delay_ms}ms ${jitter_ms}ms loss ${loss_pct}%
    fi
}

# Function to set congestion control
set_cc() {
    local algo=$1
    sudo ip netns exec pp-source sysctl -q -w net.ipv4.tcp_congestion_control=$algo
}

# Function to run single iperf3 test
run_iperf3_test() {
    local delay_ms=$1
    local jitter_ms=$2
    local cc_algo=$3
    local duration=$4
    local iteration=$5
    local output_file=$6

    echo "Running: delay=${delay_ms}ms, jitter=±${jitter_ms}ms, cc=${cc_algo}, iter=${iteration}"

    # Set network conditions
    set_netem "$delay_ms" "$jitter_ms" 0

    # Set congestion control
    set_cc "$cc_algo"

    # Start iperf3 server in destination namespace
    sudo ip netns exec pp-dest iperf3 -s -1 -D -p 5201
    sleep 0.5

    # Run iperf3 client in source namespace
    local result
    result=$(sudo ip netns exec pp-source iperf3 -c 10.0.1.2 -p 5201 -t "$duration" -J 2>/dev/null || echo '{"error": true}')

    # Extract metrics from JSON
    local throughput_bps=$(echo "$result" | python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
    if 'end' in d:
        print(d['end']['sum_sent']['bits_per_second'])
    else:
        print(0)
except:
    print(0)
" 2>/dev/null || echo "0")

    local retransmits=$(echo "$result" | python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
    if 'end' in d:
        print(d['end']['sum_sent'].get('retransmits', 0))
    else:
        print(0)
except:
    print(0)
" 2>/dev/null || echo "0")

    local throughput_kbps=$(python3 -c "print(${throughput_bps} / 8 / 1024)")

    # Append to CSV
    echo "${delay_ms},${jitter_ms},${cc_algo},${iteration},${throughput_kbps},${retransmits}" >> "$output_file"

    echo "  -> throughput=${throughput_kbps} KB/s, retransmits=${retransmits}"

    # Kill any remaining iperf3 processes
    sudo ip netns exec pp-dest pkill -9 iperf3 2>/dev/null || true
    sleep 0.5
}

# Main experiment
main() {
    local output_file="$RESULTS_DIR/iperf3-chaos-zone.csv"
    local duration=10
    local iterations=3

    echo "=============================================="
    echo "Chaos Zone iperf3 Sanity Check"
    echo "=============================================="
    echo "Output: $output_file"
    echo ""

    # CSV header
    echo "delay_ms,jitter_ms,congestion_algo,iteration,throughput_kbps,retransmits" > "$output_file"

    # Chaos zone conditions: 50ms delay (100ms RTT), jitter 8-16ms
    local delays=(50)
    local jitters=(0 8 12 16 20)
    local algos=(cubic bbr)

    local total=$((${#delays[@]} * ${#jitters[@]} * ${#algos[@]} * iterations))
    local count=0

    for delay in "${delays[@]}"; do
        for jitter in "${jitters[@]}"; do
            for algo in "${algos[@]}"; do
                for iter in $(seq 1 $iterations); do
                    count=$((count + 1))
                    echo "[$count/$total]"
                    run_iperf3_test "$delay" "$jitter" "$algo" "$duration" "$iter" "$output_file"
                done
            done
        done
    done

    # Clear netem
    set_netem 0 0 0

    echo ""
    echo "=============================================="
    echo "Complete! Results in: $output_file"
    echo "=============================================="

    # Quick summary
    echo ""
    echo "Summary by condition:"
    python3 -c "
import pandas as pd
df = pd.read_csv('$output_file')
summary = df.groupby(['jitter_ms', 'congestion_algo'])['throughput_kbps'].agg(['mean', 'std', 'count'])
summary['cv'] = summary['std'] / summary['mean'] * 100
print(summary.round(1).to_string())
"
}

main "$@"
