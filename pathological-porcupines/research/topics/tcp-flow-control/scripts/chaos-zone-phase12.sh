#!/bin/bash
# chaos-zone-phase12.sh - Run Phase 12 experiments for new chaos zone hypotheses
# Tests H-C6 through H-C10

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EXPERIMENT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RESULTS_DIR="$EXPERIMENT_DIR/results/chaos-zone-phase12"

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

    sudo ip netns exec pp-observer tc qdisc del dev veth-src root 2>/dev/null || true
    sudo ip netns exec pp-observer tc qdisc del dev veth-dst root 2>/dev/null || true

    if [[ "$delay_ms" -gt 0 ]] || [[ "$jitter_ms" -gt 0 ]] || [[ "$loss_pct" != "0" ]]; then
        sudo ip netns exec pp-observer tc qdisc add dev veth-src root netem delay ${delay_ms}ms ${jitter_ms}ms loss ${loss_pct}%
        sudo ip netns exec pp-observer tc qdisc add dev veth-dst root netem delay ${delay_ms}ms ${jitter_ms}ms loss ${loss_pct}%
    fi
}

# Function to set TCP parameters
set_tcp_params() {
    local namespace=$1
    local initcwnd=${2:-10}
    local timestamps=${3:-1}

    # Initial congestion window (via ip route)
    sudo ip netns exec "$namespace" ip route change default via 10.0.0.1 initcwnd "$initcwnd" 2>/dev/null || \
    sudo ip netns exec "$namespace" ip route change 10.0.1.0/24 via 10.0.0.1 initcwnd "$initcwnd" 2>/dev/null || true

    # TCP timestamps
    sudo ip netns exec "$namespace" sysctl -q -w net.ipv4.tcp_timestamps="$timestamps"
}

# Function to set congestion control
set_cc() {
    local algo=$1
    sudo ip netns exec pp-source sysctl -q -w net.ipv4.tcp_congestion_control=$algo
}

# Function to run single iperf3 test with ss monitoring
run_test_with_ss() {
    local delay_ms=$1
    local jitter_ms=$2
    local cc_algo=$3
    local initcwnd=$4
    local timestamps=$5
    local duration=$6
    local iteration=$7
    local output_file=$8
    local ss_output_file=$9

    echo "Running: delay=${delay_ms}ms, jitter=±${jitter_ms}ms, cc=${cc_algo}, initcwnd=${initcwnd}, ts=${timestamps}, iter=${iteration}"

    # Set network conditions
    set_netem "$delay_ms" "$jitter_ms" 0

    # Set TCP parameters
    set_tcp_params pp-source "$initcwnd" "$timestamps"

    # Set congestion control
    set_cc "$cc_algo"

    # Start iperf3 server in destination namespace
    sudo ip netns exec pp-dest iperf3 -s -1 -D -p 5201
    sleep 0.5

    # Start ss monitoring in background
    local ss_file="${ss_output_file%.csv}_${cc_algo}_j${jitter_ms}_i${iteration}.txt"
    sudo ip netns exec pp-source bash -c "while true; do ss -tin dst 10.0.1.2 2>/dev/null; sleep 0.1; done" > "$ss_file" 2>/dev/null &
    local ss_pid=$!

    # Run iperf3 client
    local result
    result=$(sudo ip netns exec pp-source iperf3 -c 10.0.1.2 -p 5201 -t "$duration" -J 2>/dev/null || echo '{"error": true}')

    # Stop ss monitoring
    sudo kill $ss_pid 2>/dev/null || true

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

    # Parse ss output for first cwnd after first retransmit
    local first_cwnd=$(grep -m 1 "cwnd:" "$ss_file" 2>/dev/null | sed 's/.*cwnd:\([0-9]*\).*/\1/' || echo "0")
    local cwnd_count=$(grep -c "cwnd:" "$ss_file" 2>/dev/null || echo "0")

    # Append to CSV
    echo "${delay_ms},${jitter_ms},${cc_algo},${initcwnd},${timestamps},${iteration},${throughput_kbps},${retransmits},${first_cwnd},${cwnd_count}" >> "$output_file"

    echo "  -> throughput=${throughput_kbps%.*} KB/s, retransmits=${retransmits}, first_cwnd=${first_cwnd}"

    # Cleanup
    sudo ip netns exec pp-dest pkill -9 iperf3 2>/dev/null || true
    sleep 0.5
}

# H-C7: Initial Congestion Window Sweep
run_hc7() {
    echo "=============================================="
    echo "H-C7: Congestion Window at First Loss"
    echo "=============================================="

    local output_file="$RESULTS_DIR/hc7-initcwnd.csv"
    local ss_dir="$RESULTS_DIR/ss_dumps"
    mkdir -p "$ss_dir"

    echo "delay_ms,jitter_ms,congestion_algo,initcwnd,timestamps,iteration,throughput_kbps,retransmits,first_cwnd,cwnd_samples" > "$output_file"

    local delay=50
    local jitters=(8 12 16)
    local algos=(cubic)
    local initcwnds=(3 10 20 40)
    local iterations=5

    local total=$((${#jitters[@]} * ${#algos[@]} * ${#initcwnds[@]} * iterations))
    local count=0

    for jitter in "${jitters[@]}"; do
        for algo in "${algos[@]}"; do
            for initcwnd in "${initcwnds[@]}"; do
                for iter in $(seq 1 $iterations); do
                    count=$((count + 1))
                    echo "[$count/$total]"
                    run_test_with_ss "$delay" "$jitter" "$algo" "$initcwnd" 1 10 "$iter" "$output_file" "$ss_dir/ss"
                done
            done
        done
    done

    echo "H-C7 complete: $output_file"
}

# H-C9: Buffer Pressure Threshold Sweep
run_hc9() {
    echo "=============================================="
    echo "H-C9: Buffer Pressure Threshold Sweep"
    echo "=============================================="

    local output_file="$RESULTS_DIR/hc9-buffers.csv"
    local ss_dir="$RESULTS_DIR/ss_dumps"
    mkdir -p "$ss_dir"

    echo "delay_ms,jitter_ms,congestion_algo,initcwnd,timestamps,iteration,throughput_kbps,retransmits,first_cwnd,cwnd_samples" > "$output_file"

    local delay=50
    local jitter=12  # Fixed at chaos zone center
    local algo=cubic
    local iterations=5

    # Test different wmem/rmem values
    local wmems=(16384 65536 262144 1048576)

    local total=$((${#wmems[@]} * iterations))
    local count=0

    for wmem in "${wmems[@]}"; do
        # Set write buffer size
        sudo ip netns exec pp-source sysctl -q -w net.ipv4.tcp_wmem="4096 ${wmem} ${wmem}"

        for iter in $(seq 1 $iterations); do
            count=$((count + 1))
            echo "[$count/$total] wmem=${wmem}"
            # Reuse the test function but add wmem info to output
            run_test_with_ss "$delay" "$jitter" "$algo" 10 1 10 "$iter" "$output_file" "$ss_dir/ss"
        done
    done

    # Reset to defaults
    sudo ip netns exec pp-source sysctl -q -w net.ipv4.tcp_wmem="4096 16384 4194304"

    echo "H-C9 complete: $output_file"
}

# H-C10: TCP Timestamps Comparison
run_hc10() {
    echo "=============================================="
    echo "H-C10: TCP Timestamps Comparison"
    echo "=============================================="

    local output_file="$RESULTS_DIR/hc10-timestamps.csv"
    local ss_dir="$RESULTS_DIR/ss_dumps"
    mkdir -p "$ss_dir"

    echo "delay_ms,jitter_ms,congestion_algo,initcwnd,timestamps,iteration,throughput_kbps,retransmits,first_cwnd,cwnd_samples" > "$output_file"

    local delay=50
    local jitters=(8 12 16)
    local algo=cubic
    local timestamps_vals=(0 1)
    local iterations=5

    local total=$((${#jitters[@]} * ${#timestamps_vals[@]} * iterations))
    local count=0

    for jitter in "${jitters[@]}"; do
        for ts in "${timestamps_vals[@]}"; do
            for iter in $(seq 1 $iterations); do
                count=$((count + 1))
                echo "[$count/$total]"
                run_test_with_ss "$delay" "$jitter" "$algo" 10 "$ts" 10 "$iter" "$output_file" "$ss_dir/ss"
            done
        done
    done

    echo "H-C10 complete: $output_file"
}

# Main
main() {
    echo "=============================================="
    echo "Phase 12: Chaos Zone Deep Investigation"
    echo "=============================================="
    echo "Results: $RESULTS_DIR"
    echo ""

    case "${1:-all}" in
        hc7) run_hc7 ;;
        hc9) run_hc9 ;;
        hc10) run_hc10 ;;
        all)
            run_hc7
            run_hc10
            # run_hc9  # Buffer sweep takes longer
            ;;
    esac

    # Clear netem
    set_netem 0 0 0

    echo ""
    echo "=============================================="
    echo "Phase 12 complete! Results in: $RESULTS_DIR"
    echo "=============================================="
}

main "$@"
