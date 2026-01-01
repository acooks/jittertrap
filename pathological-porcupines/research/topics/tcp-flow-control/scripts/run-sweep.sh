#!/bin/bash
# run-sweep.sh - Run TCP flow control parameter sweep in pp topology
#
# This script runs the sweep experiment using the pathological-porcupines
# network namespace topology, which provides realistic network conditions
# (unlike localhost which is too fast for zero-window events).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EXPERIMENT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PP_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
INFRA_DIR="$PP_ROOT/infra"

# Default output location (results/ is sibling to scripts/)
OUTPUT_DIR="${OUTPUT_DIR:-$EXPERIMENT_DIR/results}"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# Parse arguments
PRESET_NAME=""
CLUSTER_NAME=""
EXTRA_ARGS=""
NO_ARCHIVE=false
while [[ $# -gt 0 ]]; do
    case $1 in
        --quick)
            PRESET_NAME="quick"
            shift
            ;;
        --preset|-p)
            PRESET_NAME="$2"
            shift 2
            ;;
        --cluster|-c)
            CLUSTER_NAME="$2"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --no-archive)
            NO_ARCHIVE=true
            shift
            ;;
        *)
            EXTRA_ARGS="$EXTRA_ARGS $1"
            shift
            ;;
    esac
done

# Default preset if none specified
if [[ -z "$PRESET_NAME" ]]; then
    PRESET_NAME="quick"
fi

# Default cluster if none specified
if [[ -z "$CLUSTER_NAME" ]]; then
    CLUSTER_NAME="default"
fi

# Ensure topology exists
if ! ip netns list 2>/dev/null | grep -q "^pp-observer\b"; then
    echo "Setting up pathological-porcupines topology..."
    sudo "$INFRA_DIR/setup-topology.sh"
fi

# Create output directory structure: results/{cluster}/{preset}.csv
CLUSTER_DIR="$OUTPUT_DIR/$CLUSTER_NAME"
mkdir -p "$CLUSTER_DIR"
mkdir -p "$OUTPUT_DIR/archive"

# Output files without timestamp for easy aggregation
OUTPUT_CSV="$CLUSTER_DIR/${PRESET_NAME}.csv"
PCAP_FILE="$CLUSTER_DIR/${PRESET_NAME}.pcap"

# Archive previous results if they exist (unless --no-archive)
if [[ "$NO_ARCHIVE" == "false" && -f "$OUTPUT_CSV" ]]; then
    ARCHIVE_DIR="$OUTPUT_DIR/archive/$TIMESTAMP"
    mkdir -p "$ARCHIVE_DIR"
    echo "Archiving previous results to $ARCHIVE_DIR/"
    cp "$OUTPUT_CSV" "$ARCHIVE_DIR/${PRESET_NAME}.csv" 2>/dev/null || true
    cp "$PCAP_FILE" "$ARCHIVE_DIR/${PRESET_NAME}.pcap" 2>/dev/null || true
fi

echo "================================================"
echo "TCP Flow Control Parameter Sweep"
echo "================================================"
echo "Preset:    $PRESET_NAME"
echo "Cluster:   $CLUSTER_NAME"
echo "Output:    $OUTPUT_CSV"
echo "Topology:  pp-source (10.0.1.1) -> pp-dest (10.0.1.2)"
echo ""

# Start packet capture in observer namespace
echo "Starting packet capture..."
sudo ip netns exec pp-observer tcpdump -i br0 -w "$PCAP_FILE" tcp port 9999 &
TCPDUMP_PID=$!
sleep 0.5

cleanup() {
    echo ""
    echo "Cleaning up..."
    sudo kill $TCPDUMP_PID 2>/dev/null || true
    wait $TCPDUMP_PID 2>/dev/null || true

    # Kill any lingering processes in namespaces
    for ns in pp-source pp-dest; do
        for pid in $(sudo ip netns pids $ns 2>/dev/null); do
            sudo kill $pid 2>/dev/null || true
        done
    done
}
trap cleanup EXIT

# Run the sweep
# We need to run sender and receiver in different namespaces
# The sweep.py script runs both in a single process, so we need a different approach

echo "Running namespace-aware sweep..."
echo ""

# Create a temporary directory for inter-process communication
# Use disk-backed storage under OUTPUT_DIR to avoid filling tmpfs
WORK_DIR=$(mktemp -d -p "$OUTPUT_DIR" .sweep_work.XXXXXX)

# Python script that orchestrates namespace execution
cat > "$WORK_DIR/ns_sweep.py" << 'PYTHON_SCRIPT'
#!/usr/bin/env python3
"""Namespace-aware sweep orchestrator."""
import argparse
import csv
import itertools
import os
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, asdict
from datetime import datetime
from pathlib import Path

import logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s %(levelname)-8s %(message)s', datefmt='%H:%M:%S')

@dataclass
class ExperimentConfig:
    recv_buf: int
    delay_ms: float
    read_size: int
    send_rate_mbps: float
    duration: float = 10.0
    # Network conditions
    net_delay_ms: float = 0.0
    net_jitter_ms: float = 0.0
    net_loss_pct: float = 0.0
    # ECN/RED queue configuration
    ecn_enabled: bool = False     # Enable ECN marking via RED qdisc
    red_limit_kb: int = 200       # Queue limit in KB
    red_avpkt: int = 1500         # Average packet size
    red_min_kb: int = 30          # Min threshold in KB
    red_max_kb: int = 100         # Max threshold in KB (start marking)
    # Congestion control
    congestion_algo: str = 'cubic'
    # Sender stall configuration (Phase 15: Stuttering Sender)
    sender_stall_ms: float = 0.0           # Duration of each sender stall (0 = no stalling)
    sender_stall_interval_ms: float = 0.0  # Time between stalls (0 = single stall pattern)
    sender_stall_pattern: str = 'periodic' # 'periodic', 'random', or 'burst'
    sender_stall_count: int = 0            # Number of stalls (0 = continuous pattern)

    @property
    def receiver_capacity_bps(self):
        if self.delay_ms <= 0:
            return float('inf')
        return self.read_size / (self.delay_ms / 1000.0)

    @property
    def oversubscription_ratio(self):
        cap = self.receiver_capacity_bps
        if cap == 0:
            return float('inf')
        return (self.send_rate_mbps * 1024 * 1024) / cap

@dataclass
class ExperimentMetrics:
    recv_buf: int = 0
    delay_ms: float = 0.0
    read_size: int = 0
    send_rate_mbps: float = 0.0
    receiver_capacity_kbps: float = 0.0
    oversubscription_ratio: float = 0.0
    # Network conditions (echo from config)
    net_delay_ms: float = 0.0
    net_jitter_ms: float = 0.0
    net_loss_pct: float = 0.0
    congestion_algo: str = ""
    # Transfer metrics
    duration_actual: float = 0.0
    bytes_transferred: int = 0
    actual_throughput_kbps: float = 0.0
    # Zero-window metrics
    zero_window_count: int = 0
    zero_window_duration_ms: float = 0.0
    zero_window_pct: float = 0.0
    # Window metrics
    window_min: int = 0
    window_max: int = 0
    window_mean: float = 0.0
    window_oscillations: int = 0
    # RTT metrics (NEW)
    rtt_min_us: float = 0.0
    rtt_max_us: float = 0.0
    rtt_mean_us: float = 0.0
    rtt_stddev_us: float = 0.0
    rtt_p50_us: float = 0.0
    rtt_p95_us: float = 0.0
    rtt_p99_us: float = 0.0
    rtt_samples: int = 0
    # Packet metrics
    total_packets: int = 0
    retransmit_count: int = 0
    dup_ack_count: int = 0
    # New diagnostic metrics
    fast_retransmit_count: int = 0
    out_of_order_count: int = 0
    zero_window_probe_count: int = 0
    window_full_count: int = 0
    # ECN metrics (Phase 13)
    ecn_ece_count: int = 0        # ECN-Echo flags from receiver (congestion signal)
    ecn_cwr_count: int = 0        # CWR flags from sender (acknowledging ECE)
    ecn_ce_count: int = 0         # CE-marked packets in IP header
    # Sender stall metrics (Phase 15)
    sender_stall_ms: float = 0.0           # Configured stall duration
    sender_stall_interval_ms: float = 0.0  # Configured stall interval
    sender_stall_pattern: str = ""         # Configured stall pattern
    inter_packet_gap_max_ms: float = 0.0   # Maximum gap between packets (sender-side indicator)
    inter_packet_gap_mean_ms: float = 0.0  # Mean inter-packet gap
    inter_packet_gap_p95_ms: float = 0.0   # 95th percentile gap
    idle_periods_count: int = 0            # Count of gaps > 50ms (sender stall indicator)
    timestamp: str = ""
    success: bool = False
    error: str = ""

def run_in_namespace(ns: str, cmd: list, timeout: float = None) -> subprocess.CompletedProcess:
    full_cmd = ['ip', 'netns', 'exec', ns] + cmd
    return subprocess.run(full_cmd, capture_output=True, text=True, timeout=timeout)

def analyze_pcap(pcap_path: str) -> dict:
    metrics = {
        'zero_window_count': 0,
        'window_min': 0,
        'window_max': 0,
        'window_mean': 0.0,
        'window_oscillations': 0,
        'total_packets': 0,
        'retransmit_count': 0,
        'dup_ack_count': 0,
        # New diagnostic metrics
        'fast_retransmit_count': 0,
        'out_of_order_count': 0,
        'zero_window_probe_count': 0,
        'window_full_count': 0,
        # ECN metrics
        'ecn_ece_count': 0,
        'ecn_cwr_count': 0,
        'ecn_ce_count': 0,
        # RTT metrics
        'rtt_min_us': 0.0,
        'rtt_max_us': 0.0,
        'rtt_mean_us': 0.0,
        'rtt_stddev_us': 0.0,
        'rtt_p50_us': 0.0,
        'rtt_p95_us': 0.0,
        'rtt_p99_us': 0.0,
        'rtt_samples': 0,
    }
    if not os.path.exists(pcap_path):
        return metrics

    # Zero-window count
    try:
        result = subprocess.run(
            ['tshark', '-r', pcap_path, '-Y', 'tcp.analysis.zero_window', '-T', 'fields', '-e', 'frame.number'],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            lines = [l for l in result.stdout.strip().split('\n') if l]
            metrics['zero_window_count'] = len(lines)
        elif result.returncode != 0 and result.stderr:
            logging.debug(f"tshark zero_window: {result.stderr.strip()}")
    except Exception as e:
        logging.warning(f"Failed to extract zero_window_count: {e}")

    # Window size statistics
    try:
        result = subprocess.run(
            ['tshark', '-r', pcap_path, '-Y', 'tcp', '-T', 'fields', '-e', 'tcp.window_size'],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            windows = []
            for line in result.stdout.strip().split('\n'):
                if line:
                    try:
                        windows.append(int(line))
                    except ValueError:
                        pass
            if windows:
                metrics['window_min'] = min(windows)
                metrics['window_max'] = max(windows)
                metrics['window_mean'] = sum(windows) / len(windows)
                metrics['total_packets'] = len(windows)
                threshold = metrics['window_max'] * 0.2
                oscillations = 0
                for i in range(1, len(windows)):
                    if abs(windows[i] - windows[i-1]) > threshold:
                        oscillations += 1
                metrics['window_oscillations'] = oscillations
    except Exception as e:
        logging.warning(f"Failed to extract window_size stats: {e}")

    # Retransmission count
    try:
        result = subprocess.run(
            ['tshark', '-r', pcap_path, '-Y', 'tcp.analysis.retransmission', '-T', 'fields', '-e', 'frame.number'],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            lines = [l for l in result.stdout.strip().split('\n') if l]
            metrics['retransmit_count'] = len(lines)
    except Exception as e:
        logging.warning(f"Failed to extract retransmit_count: {e}")

    # Fast retransmission count (triggered by dup ACKs, not timeout)
    try:
        result = subprocess.run(
            ['tshark', '-r', pcap_path, '-Y', 'tcp.analysis.fast_retransmission', '-T', 'fields', '-e', 'frame.number'],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            lines = [l for l in result.stdout.strip().split('\n') if l]
            metrics['fast_retransmit_count'] = len(lines)
    except Exception as e:
        logging.warning(f"Failed to extract fast_retransmit_count: {e}")

    # Duplicate ACK count (early warning of packet loss)
    try:
        result = subprocess.run(
            ['tshark', '-r', pcap_path, '-Y', 'tcp.analysis.duplicate_ack', '-T', 'fields', '-e', 'frame.number'],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            lines = [l for l in result.stdout.strip().split('\n') if l]
            metrics['dup_ack_count'] = len(lines)
    except Exception as e:
        logging.warning(f"Failed to extract dup_ack_count: {e}")

    # Out-of-order segment count (network reordering indicator)
    try:
        result = subprocess.run(
            ['tshark', '-r', pcap_path, '-Y', 'tcp.analysis.out_of_order', '-T', 'fields', '-e', 'frame.number'],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            lines = [l for l in result.stdout.strip().split('\n') if l]
            metrics['out_of_order_count'] = len(lines)
    except Exception as e:
        logging.warning(f"Failed to extract out_of_order_count: {e}")

    # Zero-window probe count (sender probing after zero-window)
    try:
        result = subprocess.run(
            ['tshark', '-r', pcap_path, '-Y', 'tcp.analysis.zero_window_probe', '-T', 'fields', '-e', 'frame.number'],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            lines = [l for l in result.stdout.strip().split('\n') if l]
            metrics['zero_window_probe_count'] = len(lines)
    except Exception as e:
        logging.warning(f"Failed to extract zero_window_probe_count: {e}")

    # Window full count (sender blocked on receiver window)
    try:
        result = subprocess.run(
            ['tshark', '-r', pcap_path, '-Y', 'tcp.analysis.window_full', '-T', 'fields', '-e', 'frame.number'],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            lines = [l for l in result.stdout.strip().split('\n') if l]
            metrics['window_full_count'] = len(lines)
    except Exception as e:
        logging.warning(f"Failed to extract window_full_count: {e}")

    # ECN-Echo count (receiver signaling congestion back to sender)
    try:
        result = subprocess.run(
            ['tshark', '-r', pcap_path, '-Y', 'tcp.flags.ece==1', '-T', 'fields', '-e', 'frame.number'],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            lines = [l for l in result.stdout.strip().split('\n') if l]
            metrics['ecn_ece_count'] = len(lines)
    except Exception as e:
        logging.warning(f"Failed to extract ecn_ece_count: {e}")

    # CWR count (sender acknowledging ECE)
    try:
        result = subprocess.run(
            ['tshark', '-r', pcap_path, '-Y', 'tcp.flags.cwr==1', '-T', 'fields', '-e', 'frame.number'],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            lines = [l for l in result.stdout.strip().split('\n') if l]
            metrics['ecn_cwr_count'] = len(lines)
    except Exception as e:
        logging.warning(f"Failed to extract ecn_cwr_count: {e}")

    # CE (Congestion Experienced) count in IP header
    try:
        result = subprocess.run(
            ['tshark', '-r', pcap_path, '-Y', 'ip.dsfield.ecn==3', '-T', 'fields', '-e', 'frame.number'],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            lines = [l for l in result.stdout.strip().split('\n') if l]
            metrics['ecn_ce_count'] = len(lines)
    except Exception as e:
        logging.warning(f"Failed to extract ecn_ce_count: {e}")

    # RTT extraction from ACKs
    try:
        result = subprocess.run(
            ['tshark', '-r', pcap_path, '-Y', 'tcp.analysis.ack_rtt', '-T', 'fields', '-e', 'tcp.analysis.ack_rtt'],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            rtt_samples = []
            for line in result.stdout.strip().split('\n'):
                if line:
                    try:
                        # tshark returns RTT in seconds, convert to microseconds
                        rtt_us = float(line) * 1_000_000
                        rtt_samples.append(rtt_us)
                    except ValueError:
                        pass
            if rtt_samples:
                metrics['rtt_samples'] = len(rtt_samples)
                metrics['rtt_min_us'] = min(rtt_samples)
                metrics['rtt_max_us'] = max(rtt_samples)
                metrics['rtt_mean_us'] = sum(rtt_samples) / len(rtt_samples)
                # Standard deviation
                mean = metrics['rtt_mean_us']
                variance = sum((x - mean) ** 2 for x in rtt_samples) / len(rtt_samples)
                metrics['rtt_stddev_us'] = variance ** 0.5
                # Percentiles
                sorted_rtt = sorted(rtt_samples)
                n = len(sorted_rtt)
                metrics['rtt_p50_us'] = sorted_rtt[int(n * 0.50)]
                metrics['rtt_p95_us'] = sorted_rtt[min(int(n * 0.95), n - 1)]
                metrics['rtt_p99_us'] = sorted_rtt[min(int(n * 0.99), n - 1)]
    except Exception as e:
        logging.warning(f"Failed to extract RTT metrics: {e}")

    # Inter-packet gap analysis (Phase 15: sender stall detection)
    # Extract frame timestamps to calculate gaps between packets
    try:
        result = subprocess.run(
            ['tshark', '-r', pcap_path, '-Y', 'tcp && ip.src==10.0.1.1', '-T', 'fields', '-e', 'frame.time_epoch'],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            timestamps = []
            for line in result.stdout.strip().split('\n'):
                if line:
                    try:
                        timestamps.append(float(line))
                    except ValueError:
                        pass
            if len(timestamps) > 1:
                # Calculate inter-packet gaps in milliseconds
                gaps_ms = [(timestamps[i+1] - timestamps[i]) * 1000 for i in range(len(timestamps)-1)]
                if gaps_ms:
                    metrics['inter_packet_gap_max_ms'] = max(gaps_ms)
                    metrics['inter_packet_gap_mean_ms'] = sum(gaps_ms) / len(gaps_ms)
                    sorted_gaps = sorted(gaps_ms)
                    n = len(sorted_gaps)
                    metrics['inter_packet_gap_p95_ms'] = sorted_gaps[min(int(n * 0.95), n - 1)]
                    # Count "idle periods" - gaps > 50ms suggest sender stall
                    metrics['idle_periods_count'] = sum(1 for g in gaps_ms if g > 50)
    except Exception as e:
        logging.warning(f"Failed to extract inter-packet gap metrics: {e}")

    return metrics

def apply_network_conditions(config: 'ExperimentConfig'):
    """Apply tc/netem conditions bidirectionally in pp-observer namespace.

    For ECN experiments, we use a two-layer qdisc setup:
    1. Outer netem qdisc for delay/jitter
    2. Inner RED qdisc for ECN marking when queue fills
    """
    # Clear existing qdiscs
    for iface in ['veth-src', 'veth-dst']:
        subprocess.run(
            ['ip', 'netns', 'exec', 'pp-observer', 'tc', 'qdisc', 'del', 'dev', iface, 'root'],
            capture_output=True, timeout=5
        )

    # Enable ECN in source namespace if needed (must be active, not passive)
    if config.ecn_enabled:
        subprocess.run(
            ['ip', 'netns', 'exec', 'pp-source', 'sysctl', '-w', 'net.ipv4.tcp_ecn=1'],
            capture_output=True, timeout=5
        )
        subprocess.run(
            ['ip', 'netns', 'exec', 'pp-dest', 'sysctl', '-w', 'net.ipv4.tcp_ecn=1'],
            capture_output=True, timeout=5
        )

    net_delay_ms = config.net_delay_ms
    net_jitter_ms = config.net_jitter_ms
    net_loss_pct = config.net_loss_pct

    # If ECN is enabled, we need a different qdisc setup
    if config.ecn_enabled:
        for iface in ['veth-src', 'veth-dst']:
            # First add netem as root if we need delay/jitter
            if net_delay_ms > 0:
                cmd = ['ip', 'netns', 'exec', 'pp-observer', 'tc', 'qdisc', 'add', 'dev', iface,
                       'root', 'handle', '1:', 'netem', 'delay', f'{net_delay_ms}ms']
                if net_jitter_ms > 0:
                    cmd.extend([f'{net_jitter_ms}ms', 'distribution', 'normal'])
                if net_loss_pct > 0:
                    cmd.extend(['loss', f'{net_loss_pct}%'])
                subprocess.run(cmd, capture_output=True, timeout=5)

                # Add RED as child qdisc for ECN marking
                # RED params: limit=queue size, min/max=marking thresholds, avpkt=avg packet size
                # ecn flag enables ECN marking instead of dropping
                limit_bytes = config.red_limit_kb * 1024
                min_bytes = config.red_min_kb * 1024
                max_bytes = config.red_max_kb * 1024
                cmd = ['ip', 'netns', 'exec', 'pp-observer', 'tc', 'qdisc', 'add', 'dev', iface,
                       'parent', '1:', 'handle', '2:', 'red',
                       'limit', str(limit_bytes),
                       'min', str(min_bytes),
                       'max', str(max_bytes),
                       'avpkt', str(config.red_avpkt),
                       'burst', str((min_bytes + min_bytes + max_bytes) // (3 * config.red_avpkt) + 1),
                       'probability', '0.1',
                       'ecn']
                subprocess.run(cmd, capture_output=True, timeout=5)
            else:
                # No delay, just RED with ECN
                limit_bytes = config.red_limit_kb * 1024
                min_bytes = config.red_min_kb * 1024
                max_bytes = config.red_max_kb * 1024
                cmd = ['ip', 'netns', 'exec', 'pp-observer', 'tc', 'qdisc', 'add', 'dev', iface,
                       'root', 'handle', '1:', 'red',
                       'limit', str(limit_bytes),
                       'min', str(min_bytes),
                       'max', str(max_bytes),
                       'avpkt', str(config.red_avpkt),
                       'burst', str((min_bytes + min_bytes + max_bytes) // (3 * config.red_avpkt) + 1),
                       'probability', '0.1',
                       'ecn']
                if net_loss_pct > 0:
                    # Add netem as child for packet loss simulation
                    subprocess.run(cmd, capture_output=True, timeout=5)
                    cmd = ['ip', 'netns', 'exec', 'pp-observer', 'tc', 'qdisc', 'add', 'dev', iface,
                           'parent', '1:', 'netem', 'loss', f'{net_loss_pct}%']
                subprocess.run(cmd, capture_output=True, timeout=5)
    # Standard netem-only setup (no ECN)
    elif net_delay_ms > 0 or net_loss_pct > 0:
        for iface in ['veth-src', 'veth-dst']:
            cmd = ['ip', 'netns', 'exec', 'pp-observer', 'tc', 'qdisc', 'add', 'dev', iface, 'root', 'netem']
            if net_delay_ms > 0:
                cmd.extend(['delay', f'{net_delay_ms}ms'])
                if net_jitter_ms > 0:
                    cmd.extend([f'{net_jitter_ms}ms', 'distribution', 'normal'])
            if net_loss_pct > 0:
                cmd.extend(['loss', f'{net_loss_pct}%'])
            subprocess.run(cmd, capture_output=True, timeout=5)

def clear_network_conditions():
    """Remove all tc/netem conditions."""
    for iface in ['veth-src', 'veth-dst']:
        subprocess.run(
            ['ip', 'netns', 'exec', 'pp-observer', 'tc', 'qdisc', 'del', 'dev', iface, 'root'],
            capture_output=True, timeout=5
        )

def run_experiment(config: ExperimentConfig, work_dir: str) -> ExperimentMetrics:
    metrics = ExperimentMetrics(
        recv_buf=config.recv_buf,
        delay_ms=config.delay_ms,
        read_size=config.read_size,
        send_rate_mbps=config.send_rate_mbps,
        receiver_capacity_kbps=config.receiver_capacity_bps / 1024,
        oversubscription_ratio=config.oversubscription_ratio,
        net_delay_ms=config.net_delay_ms,
        net_jitter_ms=config.net_jitter_ms,
        net_loss_pct=config.net_loss_pct,
        congestion_algo=config.congestion_algo,
        timestamp=datetime.now().isoformat(),
    )

    pcap_path = os.path.join(work_dir, f'exp_{int(time.time()*1000)}.pcap')
    server_script = os.path.join(work_dir, 'server.py')
    client_script = os.path.join(work_dir, 'client.py')

    # Initialize process handles for cleanup
    tcpdump_proc = None
    server_proc = None
    client_proc = None
    tcpdump_actual_pid = None

    # Write server script
    with open(server_script, 'w') as f:
        f.write(f'''
import socket
import time
import sys

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, {config.recv_buf})
sock.bind(('0.0.0.0', 9999))
sock.listen(1)
print("READY", flush=True)

conn, addr = sock.accept()
conn.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, {config.recv_buf})
total = 0
start = time.monotonic()
end = start + {config.duration}
delay = {config.delay_ms} / 1000.0

while time.monotonic() < end:
    if delay > 0:
        time.sleep(delay)
    try:
        data = conn.recv({config.read_size})
        if not data:
            break
        total += len(data)
    except:
        break

elapsed = time.monotonic() - start
print(f"RESULT:{{total}}:{{elapsed}}", flush=True)
conn.close()
sock.close()
''')

    # Write client script with optional sender stall support
    sender_stall_ms = config.sender_stall_ms
    sender_stall_interval_ms = config.sender_stall_interval_ms
    sender_stall_pattern = config.sender_stall_pattern

    with open(client_script, 'w') as f:
        f.write(f'''
import socket
import time
import sys
import random

time.sleep(0.3)  # Wait for server
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

# Set congestion control algorithm BEFORE connect
TCP_CONGESTION = 13
algo = '{config.congestion_algo}'
algo_bytes = (algo + chr(0) * (16 - len(algo))).encode()[:16]
try:
    sock.setsockopt(socket.IPPROTO_TCP, TCP_CONGESTION, algo_bytes)
except Exception as e:
    print(f"Warning: Could not set congestion control: {{e}}", file=sys.stderr)

sock.connect(('10.0.1.2', 9999))

chunk = b'X' * 8192
rate = {config.send_rate_mbps} * 1024 * 1024
interval = 8192 / rate if rate > 0 else 0
start = time.monotonic()
end = start + {config.duration}
next_send = start
total = 0

# Sender stall configuration (Phase 15)
sender_stall_ms = {sender_stall_ms}
sender_stall_interval_ms = {sender_stall_interval_ms}
sender_stall_pattern = '{sender_stall_pattern}'
next_stall = start + (sender_stall_interval_ms / 1000.0) if sender_stall_ms > 0 else float('inf')
stall_count = 0

while time.monotonic() < end:
    now = time.monotonic()

    # Check if we should stall (sender-side pause)
    if sender_stall_ms > 0 and now >= next_stall:
        stall_duration = sender_stall_ms / 1000.0
        if sender_stall_pattern == 'random':
            # Random stall duration: 50-150% of configured
            stall_duration *= (0.5 + random.random())
        elif sender_stall_pattern == 'burst':
            # Burst: multiple short stalls in quick succession
            for _ in range(3):  # burst of 3 stalls
                time.sleep(stall_duration / 3)
                # Send one chunk between burst stalls
                try:
                    sent = sock.send(chunk)
                    total += sent
                except:
                    break
            stall_duration = 0  # Already handled
        if stall_duration > 0:
            time.sleep(stall_duration)
        stall_count += 1
        # Schedule next stall
        if sender_stall_pattern == 'random':
            next_stall = time.monotonic() + (sender_stall_interval_ms / 1000.0) * (0.5 + random.random())
        else:
            next_stall = time.monotonic() + (sender_stall_interval_ms / 1000.0)
        continue

    if now < next_send:
        time.sleep(next_send - now)
    next_send = time.monotonic() + interval
    try:
        sent = sock.send(chunk)
        total += sent
    except:
        break

elapsed = time.monotonic() - start
print(f"RESULT:{{total}}:{{elapsed}}:{{stall_count}}", flush=True)
sock.close()
''')

    try:
        # Apply network conditions (if any)
        apply_network_conditions(config)
        time.sleep(0.1)

        # Start pcap capture in observer namespace
        tcpdump_proc = subprocess.Popen(
            ['ip', 'netns', 'exec', 'pp-observer', 'tcpdump', '-i', 'br0', '-w', pcap_path, 'tcp', 'port', '9999'],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        time.sleep(0.3)
        # Find the actual tcpdump PID inside the namespace
        try:
            result = subprocess.run(
                ['ip', 'netns', 'exec', 'pp-observer', 'pgrep', '-f', f'tcpdump.*{pcap_path}'],
                capture_output=True, text=True, timeout=2
            )
            tcpdump_actual_pid = result.stdout.strip().split('\n')[0] if result.stdout.strip() else None
        except Exception as e:
            logging.debug(f"Could not find tcpdump PID: {e}")
            tcpdump_actual_pid = None

        # Start server in pp-dest
        server_proc = subprocess.Popen(
            ['ip', 'netns', 'exec', 'pp-dest', 'python3', server_script],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )

        # Wait for server ready
        ready_line = server_proc.stdout.readline()
        if 'READY' not in ready_line:
            raise Exception("Server didn't start")

        # Start client in pp-source
        client_proc = subprocess.Popen(
            ['ip', 'netns', 'exec', 'pp-source', 'python3', client_script],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )

        # Wait for completion
        client_out, _ = client_proc.communicate(timeout=config.duration + 10)
        server_out, _ = server_proc.communicate(timeout=5)

        # Stop capture
        tcpdump_proc.terminate()
        tcpdump_proc.wait(timeout=2)
        time.sleep(0.3)

        # Parse results
        for line in server_out.split('\n'):
            if line.startswith('RESULT:'):
                parts = line.split(':')
                metrics.bytes_transferred = int(parts[1])
                metrics.duration_actual = float(parts[2])
                if metrics.duration_actual > 0:
                    metrics.actual_throughput_kbps = (metrics.bytes_transferred / metrics.duration_actual) / 1024

        # Analyze pcap
        if os.path.exists(pcap_path):
            pcap_metrics = analyze_pcap(pcap_path)
            metrics.zero_window_count = pcap_metrics['zero_window_count']
            metrics.window_min = pcap_metrics['window_min']
            metrics.window_max = pcap_metrics['window_max']
            metrics.window_mean = pcap_metrics['window_mean']
            metrics.window_oscillations = pcap_metrics['window_oscillations']
            metrics.total_packets = pcap_metrics['total_packets']
            metrics.retransmit_count = pcap_metrics['retransmit_count']
            # New diagnostic metrics
            metrics.fast_retransmit_count = pcap_metrics['fast_retransmit_count']
            metrics.dup_ack_count = pcap_metrics['dup_ack_count']
            metrics.out_of_order_count = pcap_metrics['out_of_order_count']
            metrics.zero_window_probe_count = pcap_metrics['zero_window_probe_count']
            metrics.window_full_count = pcap_metrics['window_full_count']
            # ECN metrics
            metrics.ecn_ece_count = pcap_metrics['ecn_ece_count']
            metrics.ecn_cwr_count = pcap_metrics['ecn_cwr_count']
            metrics.ecn_ce_count = pcap_metrics['ecn_ce_count']
            # RTT metrics
            metrics.rtt_min_us = pcap_metrics['rtt_min_us']
            metrics.rtt_max_us = pcap_metrics['rtt_max_us']
            metrics.rtt_mean_us = pcap_metrics['rtt_mean_us']
            metrics.rtt_stddev_us = pcap_metrics['rtt_stddev_us']
            metrics.rtt_p50_us = pcap_metrics['rtt_p50_us']
            metrics.rtt_p95_us = pcap_metrics['rtt_p95_us']
            metrics.rtt_p99_us = pcap_metrics['rtt_p99_us']
            metrics.rtt_samples = pcap_metrics['rtt_samples']
            # Inter-packet gap metrics (Phase 15: sender stall detection)
            metrics.inter_packet_gap_max_ms = pcap_metrics.get('inter_packet_gap_max_ms', 0.0)
            metrics.inter_packet_gap_mean_ms = pcap_metrics.get('inter_packet_gap_mean_ms', 0.0)
            metrics.inter_packet_gap_p95_ms = pcap_metrics.get('inter_packet_gap_p95_ms', 0.0)
            metrics.idle_periods_count = pcap_metrics.get('idle_periods_count', 0)

        # Record sender stall configuration in metrics
        metrics.sender_stall_ms = config.sender_stall_ms
        metrics.sender_stall_interval_ms = config.sender_stall_interval_ms
        metrics.sender_stall_pattern = config.sender_stall_pattern

        metrics.success = True

    except Exception as e:
        metrics.error = str(e)
        metrics.success = False

    finally:
        # Kill any lingering processes
        for proc_name, proc in [('tcpdump', tcpdump_proc),
                                ('server', server_proc),
                                ('client', client_proc)]:
            if proc is not None:
                try:
                    proc.terminate()
                    proc.wait(timeout=1)
                except:
                    try:
                        proc.kill()
                    except:
                        pass

        # Kill the per-experiment tcpdump using its actual PID
        # This avoids killing the outer session-wide tcpdump
        if tcpdump_actual_pid:
            try:
                subprocess.run(
                    ['ip', 'netns', 'exec', 'pp-observer', 'kill', '-9', tcpdump_actual_pid],
                    capture_output=True, timeout=2
                )
            except:
                pass

        # Kill python processes in namespaces (belt and suspenders)
        # Note: We use 'ip netns pids' to get PIDs actually in the namespace,
        # then kill only those. Using 'pkill -f python3' inside a namespace
        # would kill ALL python3 processes system-wide, not just those in the ns.
        for ns in ['pp-dest', 'pp-source']:
            try:
                result = subprocess.run(
                    ['ip', 'netns', 'pids', ns],
                    capture_output=True, text=True, timeout=2
                )
                for pid in result.stdout.strip().split():
                    if pid:
                        subprocess.run(['kill', '-9', pid], capture_output=True, timeout=1)
            except:
                pass

        # Clear network conditions
        clear_network_conditions()

        # Brief pause to ensure port is released
        time.sleep(0.3)

        # Cleanup temp files
        try:
            os.unlink(pcap_path)
        except:
            pass
        try:
            os.unlink(server_script)
        except:
            pass
        try:
            os.unlink(client_script)
        except:
            pass

    return metrics

PRESETS = {
    'quick': {
        'recv_bufs': [4096, 16384],
        'delays_ms': [25, 100],
        'read_sizes': [4096],
        'send_rates': [0.25, 1.0],
        'net_delays': [0],
        'net_jitters': [0],
        'net_losses': [0],
        'congestion_algos': ['cubic'],
        'duration': 8.0,
    },
    'receiver-full': {
        'recv_bufs': [4096, 8192, 16384, 32768, 65536],
        'delays_ms': [10, 25, 50, 100, 200],
        'read_sizes': [2048, 4096, 8192],
        'send_rates': [0.1, 0.25, 0.5, 1.0, 2.0],
        'net_delays': [0],
        'net_jitters': [0],
        'net_losses': [0],
        'congestion_algos': ['cubic'],
        'duration': 10.0,
    },
    'network-rtt': {
        'recv_bufs': [16384],
        'delays_ms': [50],
        'read_sizes': [4096],
        'send_rates': [0.25, 0.5, 1.0, 2.0],
        'net_delays': [0, 10, 25, 50, 100, 200, 300],
        'net_jitters': [0],
        'net_losses': [0],
        'congestion_algos': ['cubic'],
        'duration': 10.0,
    },
    'network-loss': {
        'recv_bufs': [16384],
        'delays_ms': [50],
        'read_sizes': [4096],
        'send_rates': [0.25, 0.5, 1.0, 2.0],
        'net_delays': [50],
        'net_jitters': [0],
        'net_losses': [0, 0.1, 0.5, 1.0, 2.0, 5.0],
        'congestion_algos': ['cubic'],
        'duration': 10.0,
    },
    'network-jitter': {
        'recv_bufs': [16384],
        'delays_ms': [50],
        'read_sizes': [4096],
        'send_rates': [0.25, 0.5, 1.0, 2.0],
        'net_delays': [50],
        'net_jitters': [0, 5, 10, 20, 50],
        'net_losses': [0],
        'congestion_algos': ['cubic'],
        'duration': 10.0,
    },
    'congestion': {
        'recv_bufs': [8192, 16384, 32768],
        'delays_ms': [50],
        'read_sizes': [4096],
        'send_rates': [0.25, 0.5, 1.0, 2.0],
        'net_delays': [0],
        'net_jitters': [0],
        'net_losses': [0],
        'congestion_algos': ['cubic', 'reno'],
        'duration': 10.0,
    },
    'interaction-rtt-buf': {
        'recv_bufs': [8192, 16384, 32768, 65536],
        'delays_ms': [50],
        'read_sizes': [4096],
        'send_rates': [0.5, 1.0],
        'net_delays': [0, 50, 100, 200],
        'net_jitters': [0],
        'net_losses': [0],
        'congestion_algos': ['cubic'],
        'duration': 10.0,
    },
    'interaction-loss-cc': {
        'recv_bufs': [16384],
        'delays_ms': [50],
        'read_sizes': [4096],
        'send_rates': [0.5, 1.0],
        'net_delays': [50],
        'net_jitters': [0],
        'net_losses': [0, 0.5, 1.0, 2.0],
        'congestion_algos': ['cubic', 'reno'],
        'duration': 10.0,
    },
    # Low-RTT focused sweeps with higher resolution at lower delays
    'low-rtt-fine': {
        # Fine-grained RTT from 1-50ms, more resolution at low end
        'recv_bufs': [16384],
        'delays_ms': [50],
        'read_sizes': [4096],
        'send_rates': [0.5, 1.0, 2.0],
        'net_delays': [0, 1, 2, 3, 5, 7, 10, 15, 20, 30, 40, 50],
        'net_jitters': [0],
        'net_losses': [0],
        'congestion_algos': ['cubic'],
        'duration': 10.0,
    },
    'high-rate': {
        # Higher send rates to stress buffers more
        'recv_bufs': [16384, 32768, 65536],
        'delays_ms': [25, 50],
        'read_sizes': [4096, 8192],
        'send_rates': [2.0, 5.0, 10.0, 20.0],
        'net_delays': [0, 5, 10, 20],
        'net_jitters': [0],
        'net_losses': [0],
        'congestion_algos': ['cubic'],
        'duration': 10.0,
    },
    'low-rtt-high-rate': {
        # Combined: fine RTT resolution + high rates
        'recv_bufs': [32768, 65536],
        'delays_ms': [25],
        'read_sizes': [8192],
        'send_rates': [5.0, 10.0, 20.0],
        'net_delays': [0, 2, 5, 10, 15, 20, 30],
        'net_jitters': [0],
        'net_losses': [0],
        'congestion_algos': ['cubic'],
        'duration': 10.0,
    },
    'buffer-scaling': {
        # How do larger buffers affect zero-window threshold?
        'recv_bufs': [8192, 16384, 32768, 65536, 131072],
        'delays_ms': [25, 50],
        'read_sizes': [4096, 8192],
        'send_rates': [1.0, 2.0, 5.0, 10.0],
        'net_delays': [0, 5, 10],
        'net_jitters': [0],
        'net_losses': [0],
        'congestion_algos': ['cubic'],
        'duration': 10.0,
    },
    # ========================================================================
    # CLUSTER 1: LAN VIDEO WITH STUTTERING RECEIVER (~8 hours total)
    # ========================================================================
    # Scenario: Fast network (LAN), variable receiver (CPU stalls)
    # Goal: Map stall tolerance, buffer sizing for NVR/decoder scenarios
    #
    'lan-starvation': {
        # Core LAN starvation exploration
        # Comprehensive sweep of buffer, stall, read size, send rate
        'recv_bufs': [16384, 32768, 65536, 131072, 262144, 524288],  # 16KB-512KB (6)
        'delays_ms': [1, 2, 3, 5, 7, 10, 15, 20],       # fine stall granularity (8)
        'read_sizes': [4096, 8192, 16384],              # 4-16KB reads (3)
        'send_rates': [2.0, 5.0, 10.0, 20.0],           # 2-20 Mbps streams (4)
        'net_delays': [0, 2, 5],                        # minimal LAN RTT (3)
        'net_jitters': [0],
        'net_losses': [0],
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 6 * 8 * 3 * 4 * 3 = 1728 experiments (~5.8 hours)
    },
    'lan-starvation-fine': {
        # Fine-grained stall duration mapping at key bitrates
        'recv_bufs': [32768, 65536, 131072, 262144, 524288],  # 32KB-512KB (5)
        'delays_ms': [1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 15, 20],  # very fine (12)
        'read_sizes': [4096, 8192],                     # 4-8KB reads (2)
        'send_rates': [10.0],                           # 10 Mbps - single HD stream (1)
        'net_delays': [0, 1, 2, 5],                     # LAN range (4)
        'net_jitters': [0],
        'net_losses': [0],
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 5 * 12 * 2 * 1 * 4 = 480 experiments (~1.6 hours)
    },
    'lan-multi-bitrate': {
        # Multi-stream NVR scenario: what happens at different aggregate bitrates?
        'recv_bufs': [65536, 131072, 262144, 524288],   # 64KB-512KB (4)
        'delays_ms': [2, 5, 10, 15],                    # typical stall range (4)
        'read_sizes': [4096, 8192, 16384],              # 4-16KB reads (3)
        'send_rates': [4.0, 8.0, 16.0, 32.0],           # 4-32 Mbps (1-8 cameras) (4)
        'net_delays': [0, 2],                           # LAN (2)
        'net_jitters': [0],
        'net_losses': [0],
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 4 * 4 * 3 * 4 * 2 = 384 experiments (~1.3 hours)
    },
    # Cluster 1 Total: 1728 + 480 + 384 = 2592 experiments (~8.6 hours)

    # ========================================================================
    # CLUSTER 2: WAN/INTERNET VIDEO WITH NETWORK IMPAIRMENTS (~8 hours total)
    # ========================================================================
    # Scenario: Variable network (Starlink, LTE, Internet), capable receiver
    # Goal: Map network tolerance, buffer sizing for BDP, loss thresholds
    #
    'wan-loss': {
        # Packet loss effects with capable receiver
        'recv_bufs': [65536, 131072, 262144],           # 64-256KB (3)
        'delays_ms': [1, 2],                            # fast receiver (2)
        'read_sizes': [8192, 16384],                    # 8-16KB reads (2)
        'send_rates': [2.0, 5.0, 10.0, 20.0],           # video rates (4)
        'net_delays': [5, 10, 20, 50],                  # WAN RTT range (4)
        'net_jitters': [0, 5, 10],                      # some jitter (3)
        'net_losses': [0, 0.01, 0.05, 0.1, 0.5, 1.0, 2.0, 5.0],  # fine loss (8)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 3 * 2 * 2 * 4 * 4 * 3 * 8 = 4608 experiments - too many, split below
    },
    'wan-loss-core': {
        # Core loss exploration - reduced factorial
        'recv_bufs': [131072, 262144],                  # 128-256KB (2)
        'delays_ms': [1],                               # fast receiver (1)
        'read_sizes': [16384],                          # 16KB reads (1)
        'send_rates': [5.0, 10.0, 20.0],                # video rates (3)
        'net_delays': [5, 10, 25, 50],                  # WAN RTT (4)
        'net_jitters': [0, 10, 25],                     # jitter range (3)
        'net_losses': [0, 0.05, 0.1, 0.5, 1.0, 2.0],    # loss range (6)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 2 * 1 * 1 * 3 * 4 * 3 * 6 = 432 experiments (~1.4 hours)
    },
    'wan-bdp': {
        # Buffer sizing relative to BDP for WAN
        'recv_bufs': [16384, 32768, 65536, 131072, 262144, 524288],  # 16KB-512KB (6)
        'delays_ms': [2, 5],                            # minimal starvation (2)
        'read_sizes': [8192, 16384],                    # 8-16KB reads (2)
        'send_rates': [5.0, 10.0, 20.0],                # video rates (3)
        'net_delays': [5, 10, 20, 30, 50, 75, 100],     # wide RTT range (7)
        'net_jitters': [0, 10],                         # minimal jitter (2)
        'net_losses': [0],
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 6 * 2 * 2 * 3 * 7 * 2 = 1008 experiments (~3.4 hours)
    },
    # ========== REALISTIC NETWORK PROFILE PRESETS ==========
    'starlink': {
        # Starlink/LEO satellite network profiles
        # Models excellent -> normal -> degraded -> severe conditions
        'recv_bufs': [65536, 131072, 262144, 524288],   # 64KB-512KB (4) - larger for high BDP
        'delays_ms': [2, 5, 10],                        # receiver starvation scenarios (3)
        'read_sizes': [8192, 16384],                    # 8-16KB reads (2)
        'send_rates': [5.0, 10.0, 20.0],                # drone/vehicle video rates (3)
        # Network conditions modeled on Starlink profiles:
        # Excellent: 25ms, Normal: 50ms, Degraded: 100ms, Severe: 250ms
        'net_delays': [25, 50, 100, 250],               # one-way delays (4)
        # Jitter profiles: Excellent: 5ms, Normal: 20ms, Degraded: 40ms, Severe: 75ms
        'net_jitters': [5, 20, 40, 75],                 # jitter (4)
        # Loss profiles: Excellent: 0.05%, Normal: 0.3%, Degraded: 1.5%, Severe: 3%
        'net_losses': [0.05, 0.3, 1.5, 3.0],            # packet loss (4)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 4 * 3 * 2 * 3 * 4 * 4 * 4 = 4608 experiments - TOO MANY
        # We'll use representative combinations instead - see starlink-profiles
    },
    'starlink-profiles': {
        # Starlink with matched condition profiles (not full factorial)
        # Each run uses a coherent (delay, jitter, loss) tuple representing a condition
        # This is implemented as separate presets for each condition
        'recv_bufs': [65536, 131072, 262144],           # 64KB-256KB (3)
        'delays_ms': [2, 5, 10],                        # receiver scenarios (3)
        'read_sizes': [8192, 16384],                    # 8-16KB reads (2)
        'send_rates': [5.0, 10.0, 20.0],                # video rates (3)
        # Excellent Starlink: 25ms delay, 5ms jitter, 0.05% loss
        'net_delays': [25],
        'net_jitters': [5],
        'net_losses': [0.05],
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 3 * 3 * 2 * 3 = 54 experiments (~18 min) - just excellent condition
    },
    'starlink-excellent': {
        # Starlink excellent conditions: clear sky, good coverage
        'recv_bufs': [65536, 131072, 262144],           # 64KB-256KB (3)
        'delays_ms': [2, 5, 10],                        # receiver scenarios (3)
        'read_sizes': [8192, 16384],                    # 8-16KB reads (2)
        'send_rates': [5.0, 10.0, 20.0],                # video rates (3)
        'net_delays': [25],                             # 25ms one-way = 50ms RTT
        'net_jitters': [5],                             # low jitter
        'net_losses': [0, 0.05, 0.1],                   # minimal loss (3)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 3 * 3 * 2 * 3 * 1 * 1 * 3 = 162 experiments (~54 min)
    },
    'starlink-normal': {
        # Starlink normal conditions: typical operation
        'recv_bufs': [65536, 131072, 262144],           # 64KB-256KB (3)
        'delays_ms': [2, 5, 10],                        # receiver scenarios (3)
        'read_sizes': [8192, 16384],                    # 8-16KB reads (2)
        'send_rates': [5.0, 10.0, 20.0],                # video rates (3)
        'net_delays': [50],                             # 50ms one-way = 100ms RTT
        'net_jitters': [20],                            # moderate jitter
        'net_losses': [0.1, 0.3, 0.5],                  # some loss (3)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 3 * 3 * 2 * 3 * 1 * 1 * 3 = 162 experiments (~54 min)
    },
    'starlink-degraded': {
        # Starlink degraded conditions: obstructions, handoffs
        'recv_bufs': [131072, 262144, 524288],          # 128KB-512KB (3) - need larger buffers
        'delays_ms': [2, 5, 10],                        # receiver scenarios (3)
        'read_sizes': [8192, 16384],                    # 8-16KB reads (2)
        'send_rates': [5.0, 10.0, 20.0],                # video rates (3)
        'net_delays': [100],                            # 100ms one-way = 200ms RTT
        'net_jitters': [40],                            # high jitter
        'net_losses': [1.0, 1.5, 2.0],                  # significant loss (3)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 3 * 3 * 2 * 3 * 1 * 1 * 3 = 162 experiments (~54 min)
    },
    'starlink-severe': {
        # Starlink severe conditions: weather, edge of coverage
        'recv_bufs': [262144, 524288, 1048576],         # 256KB-1MB (3) - large buffers needed
        'delays_ms': [2, 5, 10],                        # receiver scenarios (3)
        'read_sizes': [8192, 16384],                    # 8-16KB reads (2)
        'send_rates': [2.0, 5.0, 10.0],                 # lower rates more realistic (3)
        'net_delays': [250],                            # 250ms one-way = 500ms RTT
        'net_jitters': [75],                            # very high jitter
        'net_losses': [2.0, 3.0, 5.0],                  # heavy loss (3)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 3 * 3 * 2 * 3 * 1 * 1 * 3 = 162 experiments (~54 min)
    },
    'lte-profiles': {
        # LTE/mobile network - representative conditions (reduced)
        'recv_bufs': [131072, 262144],                  # 128-256KB (2)
        'delays_ms': [2, 5],                            # minimal receiver stall (2)
        'read_sizes': [16384],                          # 16KB reads (1)
        'send_rates': [2.0, 5.0, 10.0],                 # mobile video rates (3)
        # LTE range: excellent 30ms -> degraded 150ms
        'net_delays': [30, 75, 150],                    # RTT range (3)
        'net_jitters': [10, 30],                        # jitter range (2)
        'net_losses': [0.1, 1.0, 3.0],                  # loss range (3)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 2 * 2 * 1 * 3 * 3 * 2 * 3 = 216 experiments (~0.7 hours)
    },
    # Cluster 2 Total: 432 + 1008 + 648 + 216 = 2304 experiments (~7.7 hours)

    # ========================================================================
    # DIAGNOSTIC VALIDATION PRESETS
    # ========================================================================
    # Purpose: Validate the diagnostic decision tree signatures
    # Key question: Can we reliably distinguish receiver vs network problems?
    #
    'diag-receiver-pure': {
        # Pure receiver problem: fast network, slow receiver
        # Expected: Many zero-window, few retransmits, low RTT
        'recv_bufs': [16384, 32768, 65536, 131072],        # 16KB-128KB (4)
        'delays_ms': [5, 10, 15, 20, 30, 50],              # receiver stall (6)
        'read_sizes': [4096, 8192],                        # read sizes (2)
        'send_rates': [5.0, 10.0, 20.0],                   # video rates (3)
        'net_delays': [0],                                 # NO network delay (1)
        'net_jitters': [0],                                # NO jitter (1)
        'net_losses': [0],                                 # NO loss (1)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 4 * 6 * 2 * 3 * 1 * 1 * 1 = 144 experiments (~48 min)
    },
    'diag-network-pure': {
        # Pure network problem: capable receiver, impaired network
        # Expected: Few zero-window, many retransmits, high RTT
        'recv_bufs': [131072, 262144],                     # large buffers (2)
        'delays_ms': [1],                                  # fast receiver (1)
        'read_sizes': [16384],                             # large reads (1)
        'send_rates': [5.0, 10.0, 20.0],                   # video rates (3)
        'net_delays': [25, 50, 100, 150],                  # network RTT (4)
        'net_jitters': [0, 20, 50],                        # jitter range (3)
        'net_losses': [0.1, 0.5, 1.0, 2.0],                # loss range (4)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 2 * 1 * 1 * 3 * 4 * 3 * 4 = 288 experiments (~96 min)
    },
    'diag-compound': {
        # Both problems present: slow receiver + impaired network
        # Expected: Both signatures - can we still diagnose?
        'recv_bufs': [32768, 65536, 131072],               # buffer range (3)
        'delays_ms': [5, 10, 20],                          # moderate stall (3)
        'read_sizes': [8192],                              # medium reads (1)
        'send_rates': [5.0, 10.0],                         # video rates (2)
        'net_delays': [25, 50, 100],                       # network RTT (3)
        'net_jitters': [0, 25],                            # some jitter (2)
        'net_losses': [0.5, 1.0, 2.0],                     # loss range (3)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 3 * 3 * 1 * 2 * 3 * 2 * 3 = 324 experiments (~108 min)
    },
    # Diagnostic Total: 144 + 288 + 324 = 756 experiments (~4.2 hours)

    # =========================================================================
    # Phase 2: Buffer Sizing Experiments
    # Goal: Create practical buffer sizing tables
    # =========================================================================

    'buf-stall-absorption': {
        # Experiment 2.1: How much buffer needed to survive receiver stalls?
        # Perfect network, vary buffer and stall duration
        # Output: Table of (stall_duration, min_buffer) to survive without zero-window
        'recv_bufs': [16384, 32768, 65536, 131072, 262144, 524288],  # 16KB-512KB (6)
        'delays_ms': [5, 10, 15, 20, 25, 30, 40, 50, 75, 100],       # stall durations (10)
        'read_sizes': [8192],                                         # standard read (1)
        'send_rates': [10.0],                                         # 10 Mbps video (1)
        'net_delays': [0],                                            # perfect network (1)
        'net_jitters': [0],                                           # no jitter (1)
        'net_losses': [0],                                            # no loss (1)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 6 * 10 * 1 * 1 * 1 * 1 * 1 = 60 experiments (~20 min)
    },
    'buf-bdp-coverage': {
        # Experiment 2.2: How much buffer needed for various RTT?
        # Fast receiver, vary buffer and RTT
        # Output: Table of (RTT, min_buffer) for 90% throughput efficiency
        'recv_bufs': [16384, 32768, 65536, 131072, 262144, 524288, 1048576],  # 16KB-1MB (7)
        'delays_ms': [1],                                             # fast receiver (1)
        'read_sizes': [16384],                                        # large reads (1)
        'send_rates': [10.0],                                         # 10 Mbps target (1)
        'net_delays': [10, 25, 50, 75, 100, 150, 200, 300],          # RTT range (8)
        'net_jitters': [0],                                           # no jitter (1)
        'net_losses': [0],                                            # no loss (1)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 7 * 1 * 1 * 1 * 8 * 1 * 1 = 56 experiments (~19 min)
    },
    'buf-jitter-absorption': {
        # Experiment 2.3: How much buffer needed to absorb jitter?
        # Fast receiver, vary buffer and jitter
        # Output: Table of (jitter, min_buffer) for stable throughput
        'recv_bufs': [65536, 131072, 262144, 524288],                 # 64KB-512KB (4)
        'delays_ms': [1],                                             # fast receiver (1)
        'read_sizes': [16384],                                        # large reads (1)
        'send_rates': [10.0],                                         # 10 Mbps target (1)
        'net_delays': [50],                                           # baseline 50ms RTT (1)
        'net_jitters': [10, 20, 30, 50, 75, 100],                     # jitter range (6)
        'net_losses': [0],                                            # no loss (1)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 4 * 1 * 1 * 1 * 1 * 6 * 1 = 24 experiments (~8 min)
    },
    # Buffer Sizing Total: 60 + 56 + 24 = 140 experiments (~47 min)

    # =========================================================================
    # Phase 2b: Congestion Control Algorithm Comparison
    # Goal: Understand how algorithm choice affects jitter sensitivity
    # =========================================================================

    'cc-jitter-cubic': {
        # Q1: Baseline - CUBIC jitter sensitivity with fine resolution
        # 2ms steps from 0-32ms to find exact cliff location
        'recv_bufs': [262144],                                        # 256KB buffer (1)
        'delays_ms': [1],                                             # fast receiver (1)
        'read_sizes': [16384],                                        # large reads (1)
        'send_rates': [10.0],                                         # 10 Mbps (1)
        'net_delays': [25],                                           # 50ms RTT (1)
        'net_jitters': [0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32],  # 2ms steps (17)
        'net_losses': [0],                                            # no loss (1)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 1 * 1 * 1 * 1 * 1 * 17 * 1 = 17 experiments (~6 min)
    },
    'cc-jitter-bbr': {
        # Q1: Does BBR survive high jitter better than CUBIC?
        # Same fine resolution to compare cliff behavior
        'recv_bufs': [262144],                                        # 256KB buffer (1)
        'delays_ms': [1],                                             # fast receiver (1)
        'read_sizes': [16384],                                        # large reads (1)
        'send_rates': [10.0],                                         # 10 Mbps (1)
        'net_delays': [25],                                           # 50ms RTT (1)
        'net_jitters': [0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32],  # 2ms steps (17)
        'net_losses': [0],                                            # no loss (1)
        'congestion_algos': ['bbr'],
        'duration': 10.0,
        # 1 * 1 * 1 * 1 * 1 * 17 * 1 = 17 experiments (~6 min)
    },
    'cc-jitter-reno': {
        # Baseline comparison with classic Reno
        'recv_bufs': [262144],                                        # 256KB buffer (1)
        'delays_ms': [1],                                             # fast receiver (1)
        'read_sizes': [16384],                                        # large reads (1)
        'send_rates': [10.0],                                         # 10 Mbps (1)
        'net_delays': [25],                                           # 50ms RTT (1)
        'net_jitters': [0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32],  # 2ms steps (17)
        'net_losses': [0],                                            # no loss (1)
        'congestion_algos': ['reno'],
        'duration': 10.0,
        # 1 * 1 * 1 * 1 * 1 * 17 * 1 = 17 experiments (~6 min)
    },
    'cc-starlink-comparison': {
        # Q2: Which algorithm achieves highest throughput on Starlink-like conditions?
        # Test all three algorithms on degraded Starlink profile
        'recv_bufs': [262144, 524288],                                # 256KB-512KB (2)
        'delays_ms': [1],                                             # fast receiver (1)
        'read_sizes': [16384],                                        # large reads (1)
        'send_rates': [10.0],                                         # 10 Mbps (1)
        'net_delays': [50],                                           # 100ms RTT (1)
        'net_jitters': [0, 10, 20, 30],                               # typical Starlink range (4)
        'net_losses': [0, 0.5, 1.5],                                  # loss range (3)
        'congestion_algos': ['cubic', 'bbr', 'reno'],
        'duration': 10.0,
        # 2 * 1 * 1 * 1 * 1 * 4 * 3 * 3 = 72 experiments (~24 min)
    },
    'cc-compound-detection': {
        # Q3: Does BBR reveal masked receiver problems?
        # Re-run compound scenario with different algorithms
        'recv_bufs': [131072],                                        # 128KB buffer (1)
        'delays_ms': [10, 20],                                        # receiver stall (2)
        'read_sizes': [8192],                                         # standard reads (1)
        'send_rates': [10.0],                                         # 10 Mbps (1)
        'net_delays': [25, 50],                                       # network RTT (2)
        'net_jitters': [0],                                           # no jitter (1)
        'net_losses': [0.5, 1.0],                                     # loss (2)
        'congestion_algos': ['cubic', 'bbr'],
        'duration': 10.0,
        # 1 * 2 * 1 * 1 * 2 * 1 * 2 * 2 = 16 experiments (~5 min)
    },
    'cc-bdp-comparison': {
        # Q4: Do algorithms differ in buffer requirements?
        # Compare BDP coverage across algorithms
        'recv_bufs': [65536, 131072, 262144, 524288],                 # buffer range (4)
        'delays_ms': [1],                                             # fast receiver (1)
        'read_sizes': [16384],                                        # large reads (1)
        'send_rates': [10.0],                                         # 10 Mbps (1)
        'net_delays': [25, 50, 100],                                  # RTT range (3)
        'net_jitters': [0],                                           # no jitter (1)
        'net_losses': [0],                                            # no loss (1)
        'congestion_algos': ['cubic', 'bbr'],
        'duration': 10.0,
        # 4 * 1 * 1 * 1 * 3 * 1 * 1 * 2 = 24 experiments (~8 min)
    },
    # CC Comparison Total: 17 + 17 + 17 + 72 + 16 + 24 = 163 experiments (~54 min)

    # =========================================================================
    # Reproducibility and Noise Floor Experiments
    # Goal: Understand measurement variance before drawing conclusions
    # =========================================================================

    'reproduce-baseline': {
        # Run same condition 10 times to measure noise floor
        # Uses iteration count via command line: --iterations 10
        'recv_bufs': [262144],                                        # 256KB buffer (1)
        'delays_ms': [1],                                             # fast receiver (1)
        'read_sizes': [16384],                                        # large reads (1)
        'send_rates': [10.0],                                         # 10 Mbps (1)
        'net_delays': [25],                                           # 50ms RTT (1)
        'net_jitters': [0],                                           # no jitter (1)
        'net_losses': [0],                                            # no loss (1)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 1 experiment x 10 iterations = 10 runs (~3 min)
    },
    'reproduce-cliff-region': {
        # Test around the cliff region multiple times
        # Key jitter values: 4, 6, 8, 10ms - where we see the cliff
        'recv_bufs': [262144],                                        # 256KB buffer (1)
        'delays_ms': [1],                                             # fast receiver (1)
        'read_sizes': [16384],                                        # large reads (1)
        'send_rates': [10.0],                                         # 10 Mbps (1)
        'net_delays': [25],                                           # 50ms RTT (1)
        'net_jitters': [4, 6, 8, 10],                                 # cliff region (4)
        'net_losses': [0],                                            # no loss (1)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 4 experiments x 5 iterations = 20 runs (~7 min)
    },
    'reproduce-compare-rtt': {
        # Compare same jitter at different base RTTs
        # This tests whether cliff is absolute or RTT-relative
        'recv_bufs': [262144],                                        # 256KB buffer (1)
        'delays_ms': [1],                                             # fast receiver (1)
        'read_sizes': [16384],                                        # large reads (1)
        'send_rates': [10.0],                                         # 10 Mbps (1)
        'net_delays': [25, 50],                                       # 50ms and 100ms RTT (2)
        'net_jitters': [0, 4, 8, 12, 16, 20],                         # jitter range (6)
        'net_losses': [0],                                            # no loss (1)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 12 experiments x 3 iterations = 36 runs (~12 min)
    },
    # Reproducibility Total: 10 + 20 + 36 = 66 runs (~22 min)

    # =========================================================================
    # Phase 2d: RTT × CC Algorithm Characterization
    # Goal: Determine if BBR shifts the jitter cliff and if 20% rule holds
    # =========================================================================

    'cc-rtt-sweep': {
        # Comprehensive sweep: RTT × Jitter × CC Algorithm
        # Tests whether cliff location (as % of RTT) varies by algorithm
        'recv_bufs': [262144],                                        # 256KB buffer (1)
        'delays_ms': [1],                                             # fast receiver (1)
        'read_sizes': [16384],                                        # large reads (1)
        'send_rates': [10.0],                                         # 10 Mbps (1)
        'net_delays': [12, 25, 50],                                   # 25ms, 50ms, 100ms RTT (3)
        'net_jitters': [0, 4, 8, 12, 16, 20, 24],                     # enough to find cliffs (7)
        'net_losses': [0],                                            # no loss (1)
        'congestion_algos': ['cubic', 'bbr'],                         # compare these two (2)
        'duration': 10.0,
        # 1 * 1 * 1 * 1 * 3 * 7 * 1 * 2 = 42 experiments
        # With 3 iterations: 126 runs (~42 min)
    },
    # Phase 2d Total: 126 runs with iterations (~42 min)

    # =========================================================================
    # Phase 3: Transient Stall Recovery Experiments
    # Goal: Understand occasional hiccup behavior
    # =========================================================================

    'transient-single-stall': {
        # Experiment 3.1: How quickly does TCP recover from a single stall?
        # Perfect network, single stall of varying duration
        # Note: This uses continuous stall as proxy; true transient needs code changes
        'recv_bufs': [131072],                                        # 128KB buffer (1)
        'delays_ms': [10, 25, 50, 100, 200],                         # stall durations (5)
        'read_sizes': [8192],                                         # standard read (1)
        'send_rates': [10.0],                                         # 10 Mbps (1)
        'net_delays': [0],                                            # perfect network (1)
        'net_jitters': [0],                                           # no jitter (1)
        'net_losses': [0],                                            # no loss (1)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 1 * 5 * 1 * 1 * 1 * 1 * 1 = 5 experiments (~2 min)
    },
    'transient-recovery-cc': {
        # Q5: Which algorithm recovers fastest from stalls?
        # Compare recovery across algorithms
        'recv_bufs': [131072],                                        # 128KB buffer (1)
        'delays_ms': [10, 25, 50],                                    # stall durations (3)
        'read_sizes': [8192],                                         # standard read (1)
        'send_rates': [10.0],                                         # 10 Mbps (1)
        'net_delays': [0, 25],                                        # network conditions (2)
        'net_jitters': [0],                                           # no jitter (1)
        'net_losses': [0],                                            # no loss (1)
        'congestion_algos': ['cubic', 'bbr', 'reno'],
        'duration': 10.0,
        # 1 * 3 * 1 * 1 * 2 * 1 * 1 * 3 = 18 experiments (~6 min)
    },
    'transient-stall-network': {
        # Experiment 3.3: Does transient stall trigger zero-window with network impairment?
        # This tests whether brief stalls can reveal receiver problems
        # that would otherwise be masked by network throttling
        'recv_bufs': [262144],                                        # 256KB buffer (1)
        'delays_ms': [25, 50, 100],                                   # stall durations (3)
        'read_sizes': [8192],                                         # standard read (1)
        'send_rates': [10.0],                                         # 10 Mbps (1)
        'net_delays': [0, 25, 50],                                    # network RTT (3)
        'net_jitters': [0],                                           # no jitter (1)
        'net_losses': [0, 0.5, 1.0],                                  # loss range (3)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 1 * 3 * 1 * 1 * 3 * 1 * 3 = 27 experiments (~9 min)
    },
    # Transient Total: 5 + 18 + 27 = 50 experiments (~17 min)

    # =========================================================================
    # Phase 5: Loss Tolerance Analysis
    # Goal: Answer "At what loss rate does video streaming become unreliable?"
    # =========================================================================

    'loss-tolerance': {
        # Loss × RTT × CC sweep - pruned to essential parameters
        'recv_bufs': [262144],                    # 256KB fixed (1)
        'delays_ms': [1],                          # fast receiver (1)
        'read_sizes': [16384],                     # 16KB (1)
        'send_rates': [10.0],                      # 10 Mbps reference (1)
        'net_delays': [10, 25, 50, 100],          # RTT sweep (4)
        'net_jitters': [0],                        # no jitter (1)
        'net_losses': [0, 0.1, 0.25, 0.5, 1, 2, 5],  # loss sweep (7)
        'congestion_algos': ['cubic', 'bbr'],      # both CC (2)
        'duration': 10.0,
        # 1 * 1 * 1 * 1 * 4 * 1 * 7 * 2 = 56 experiments
        # With 3 iterations: 168 runs (~1 hour)
    },

    # =========================================================================
    # Phase 6: Starlink Deployment Guide (Quick Profiles)
    # Goal: Create concrete recommendations for video over Starlink
    # =========================================================================

    'starlink-quick-excellent': {
        # Excellent Starlink: 25ms delay, 5ms jitter, 0.05% loss
        'recv_bufs': [131072, 262144],             # 128KB, 256KB (2)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [5.0, 10.0, 20.0],           # video rates (3)
        'net_delays': [25],                         # 50ms RTT (1)
        'net_jitters': [5],                         # low jitter (1)
        'net_losses': [0.05],                       # minimal loss (1)
        'congestion_algos': ['cubic', 'bbr'],       # both CC (2)
        'duration': 10.0,
        # 2 * 1 * 1 * 3 * 1 * 1 * 1 * 2 = 12 experiments
    },
    'starlink-quick-normal': {
        # Normal Starlink: 50ms delay, 20ms jitter, 0.3% loss
        'recv_bufs': [131072, 262144],
        'delays_ms': [1],
        'read_sizes': [16384],
        'send_rates': [5.0, 10.0, 20.0],
        'net_delays': [50],                         # 100ms RTT
        'net_jitters': [20],                        # moderate jitter
        'net_losses': [0.3],                        # some loss
        'congestion_algos': ['cubic', 'bbr'],
        'duration': 10.0,
        # 12 experiments
    },
    'starlink-quick-degraded': {
        # Degraded Starlink: 100ms delay, 40ms jitter, 1.5% loss
        'recv_bufs': [131072, 262144],
        'delays_ms': [1],
        'read_sizes': [16384],
        'send_rates': [5.0, 10.0, 20.0],
        'net_delays': [100],                        # 200ms RTT
        'net_jitters': [40],                        # high jitter
        'net_losses': [1.5],                        # significant loss
        'congestion_algos': ['cubic', 'bbr'],
        'duration': 10.0,
        # 12 experiments
    },
    'starlink-quick-severe': {
        # Severe Starlink: 250ms delay, 75ms jitter, 3% loss
        'recv_bufs': [131072, 262144],
        'delays_ms': [1],
        'read_sizes': [16384],
        'send_rates': [5.0, 10.0, 20.0],
        'net_delays': [250],                        # 500ms RTT
        'net_jitters': [75],                        # very high jitter
        'net_losses': [3.0],                        # heavy loss
        'congestion_algos': ['cubic', 'bbr'],
        'duration': 10.0,
        # 12 experiments
    },
    # Starlink Quick Total: 12 * 4 = 48 experiments
    # With 3 iterations: 144 runs (~50 min)

    # =========================================================================
    # Phase 7: LTE Deployment Guide (Quick)
    # Goal: Brief recommendations for mobile network video
    # =========================================================================

    'lte-quick': {
        # LTE/mobile network conditions
        'recv_bufs': [262144],                     # 256KB fixed (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [5.0, 10.0],                 # mobile rates (2)
        'net_delays': [30, 75, 150],               # excellent→degraded (3)
        'net_jitters': [15, 45],                   # low/high jitter (2)
        'net_losses': [0.5, 2.0],                  # moderate/high loss (2)
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 1 * 1 * 1 * 2 * 3 * 2 * 2 = 24 experiments
        # With 3 iterations: 72 runs (~25 min)
    },

    # =========================================================================
    # Phase 8: Stall Tolerance Table
    # Goal: Create comprehensive (bitrate, buffer, RTT) → max_stall lookup
    # =========================================================================

    'stall-tolerance-targeted': {
        # Extended stall tolerance for missing bitrates
        'recv_bufs': [65536, 131072, 262144],      # 3 key sizes (3)
        'delays_ms': [5, 10, 20, 50],               # 4 key stall points (4)
        'read_sizes': [8192],                       # fixed (1)
        'send_rates': [5.0, 10.0, 20.0],           # 3 bitrates (3)
        'net_delays': [0, 25],                      # LAN + WAN (2)
        'net_jitters': [0],
        'net_losses': [0],
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 3 * 4 * 1 * 3 * 2 = 72 experiments
        # With 3 iterations: 216 runs (~1.2 hours)
    },

    # =========================================================================
    # Phase 9: H5 Hypothesis Validation
    # Goal: Test if read size affects zero-window granularity
    # =========================================================================

    'read-size-granularity': {
        # Test H5 about read size effects on zero-window granularity
        'recv_bufs': [65536],                       # 64KB buffer (1)
        'delays_ms': [10, 20],                      # stall durations (2)
        'read_sizes': [1024, 4096, 16384, 65536],  # 4 sizes (4)
        'send_rates': [10.0],                       # 10 Mbps (1)
        'net_delays': [0],                          # LAN (1)
        'net_jitters': [0],
        'net_losses': [0],
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 1 * 2 * 4 * 1 = 8 experiments
        # With 3 iterations: 24 runs (~10 min)
    },

    # =========================================================================
    # Phase 10: Multi-Stream NVR Sizing (Optional)
    # Goal: Answer "How do I size buffers for 32 cameras at 4 Mbps each?"
    # =========================================================================

    'nvr-aggregate': {
        # High aggregate bitrate for NVR scenarios
        'recv_bufs': [262144, 524288, 1048576],    # 256KB-1MB (3)
        'delays_ms': [5, 10],                       # typical stalls (2)
        'read_sizes': [16384],                      # fixed (1)
        'send_rates': [32.0, 64.0, 128.0],         # 8, 16, 32 cameras @ 4Mbps (3)
        'net_delays': [0],                          # LAN only (1)
        'net_jitters': [0],
        'net_losses': [0],
        'congestion_algos': ['cubic'],
        'duration': 10.0,
        # 3 * 2 * 1 * 3 * 1 = 18 experiments
        # With 3 iterations: 54 runs (~20 min)
    },

    # =========================================================================
    # Phase 11: Chaos Zone Investigation
    # Goal: Understand why CV is 30-115% at jitter 10-25% of RTT
    # =========================================================================

    'chaos-zone-capture': {
        # Targeted chaos zone experiments with full packet capture
        # Focus on 100ms RTT where chaos is worst observed
        'recv_bufs': [262144],                      # 256KB fixed (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [10.0],                       # 10 Mbps reference (1)
        'net_delays': [50],                         # 100ms RTT (1)
        'net_jitters': [8, 12, 16],                 # chaos zone jitter values (3)
        'net_losses': [0],                          # no loss (1)
        'congestion_algos': ['cubic', 'bbr'],       # both CC for comparison (2)
        'duration': 10.0,
        # 1 * 1 * 1 * 1 * 1 * 3 * 1 * 2 = 6 experiments
        # With 3 iterations: 18 runs (~10 min)
        # NOTE: Keep pcap files for detailed analysis!
    },

    # =========================================================================
    # Phase 13: ECN Investigation
    # Goal: Distinguish congestion (ECE flags) from packet loss (no ECE)
    # =========================================================================

    'ecn-congestion-only': {
        # ECN with router congestion (RED qdisc marks packets), no random loss
        # Expected: High ECE/CWR counts, zero random retransmits
        # This represents: "Router queue is building up, please slow down"
        'recv_bufs': [262144],                      # 256KB fixed (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [10.0, 20.0],                 # push queue to fill (2)
        'net_delays': [25, 50],                     # 50ms, 100ms RTT (2)
        'net_jitters': [0],                         # no jitter (1)
        'net_losses': [0],                          # NO random loss (1)
        'congestion_algos': ['cubic', 'bbr'],       # both CC (2)
        'ecn_enabled': True,
        'red_limit_kb': 100,                        # Small queue to force marking
        'red_min_kb': 20,
        'red_max_kb': 60,
        'duration': 10.0,
        # 1 * 1 * 1 * 2 * 2 * 1 * 1 * 2 = 8 experiments
        # With 3 iterations: 24 runs (~10 min)
    },
    'ecn-loss-only': {
        # Random packet loss WITHOUT ECN (just netem loss)
        # Expected: Retransmits from loss, NO ECE flags
        # This represents: "Packets dropped (wireless interference, etc)"
        'recv_bufs': [262144],                      # 256KB fixed (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [10.0],                       # 10 Mbps (1)
        'net_delays': [25, 50],                     # 50ms, 100ms RTT (2)
        'net_jitters': [0],                         # no jitter (1)
        'net_losses': [0.5, 1.0, 2.0],              # loss rates (3)
        'congestion_algos': ['cubic', 'bbr'],       # both CC (2)
        'ecn_enabled': False,                       # NO ECN - pure loss
        'duration': 10.0,
        # 1 * 1 * 1 * 1 * 2 * 1 * 3 * 2 = 12 experiments
        # With 3 iterations: 36 runs (~15 min)
    },
    'ecn-mixed': {
        # Both congestion (ECN) AND random loss
        # Expected: BOTH ECE flags AND loss-triggered retransmits
        # This represents: "Congested AND lossy network (degraded Starlink)"
        'recv_bufs': [262144],                      # 256KB fixed (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [10.0, 20.0],                 # push queue (2)
        'net_delays': [50],                         # 100ms RTT (1)
        'net_jitters': [0, 20],                     # some jitter (2)
        'net_losses': [0.5, 1.0],                   # moderate loss (2)
        'congestion_algos': ['cubic', 'bbr'],       # both CC (2)
        'ecn_enabled': True,
        'red_limit_kb': 100,
        'red_min_kb': 20,
        'red_max_kb': 60,
        'duration': 10.0,
        # 1 * 1 * 1 * 2 * 1 * 2 * 2 * 2 = 16 experiments
        # With 3 iterations: 48 runs (~20 min)
    },
    'ecn-receiver-vs-network': {
        # Can we distinguish receiver problem from network congestion using ECN?
        # Receiver problem: zero-window, no ECE
        # Network congestion: ECE flags, no zero-window
        'recv_bufs': [65536, 131072],               # smaller buffers to trigger ZW (2)
        'delays_ms': [1, 10, 20],                   # fast receiver vs slow (3)
        'read_sizes': [8192],                       # 8KB (1)
        'send_rates': [10.0],                       # 10 Mbps (1)
        'net_delays': [25, 50],                     # RTT range (2)
        'net_jitters': [0],                         # no jitter (1)
        'net_losses': [0],                          # no loss (1)
        'congestion_algos': ['cubic'],              # CUBIC only (1)
        'ecn_enabled': True,
        'red_limit_kb': 100,
        'red_min_kb': 20,
        'red_max_kb': 60,
        'duration': 10.0,
        # 2 * 3 * 1 * 1 * 2 * 1 * 1 * 1 = 12 experiments
        # With 3 iterations: 36 runs (~15 min)
    },
    # Phase 13 Total: 24 + 36 + 48 + 36 = 144 runs (~1 hour)

    # =========================================================================
    # Phase 14: Realistic Starlink Profiles
    # Based on actual measurements from APNIC, WirelessMoves, and Starlink data
    # Sources:
    #   - APNIC: https://blog.apnic.net/2024/05/17/a-transport-protocols-view-of-starlink/
    #   - WirelessMoves: https://blog.wirelessmoves.com/2024/07/analyzing-packet-loss-in-starlink.html
    #   - Starlink: https://starlink.com/public-files/StarlinkLatency.pdf
    # =========================================================================

    'starlink-realistic-baseline': {
        # Real Starlink baseline: 25-30ms median RTT, ~7ms jitter, 0.1-0.2% loss
        # This matches APNIC measurements within each 15-second tracking interval
        'recv_bufs': [262144],                      # 256KB (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [10.0, 50.0, 100.0],          # video to bulk transfer (3)
        'net_delays': [12, 15],                     # 24-30ms RTT (2) - real Starlink median
        'net_jitters': [3, 7],                      # real jitter: avg 6.7ms (2)
        'net_losses': [0.1, 0.2],                   # real loss: 0.1-0.2% baseline (2)
        'congestion_algos': ['cubic', 'bbr'],       # both CC (2)
        'duration': 15.0,                           # 15s to capture one handover cycle
        # 1 * 1 * 1 * 3 * 2 * 2 * 2 * 2 = 48 experiments
        # With 3 iterations: 144 runs (~45 min)
    },
    'starlink-realistic-handover': {
        # Simulates the 15-second handover spike: RTT jumps 30-50ms, brief loss burst
        # APNIC: "latency shifts from 30ms to 80ms" at handover
        # Note: netem can't perfectly simulate periodic spikes, so we test the spike conditions
        'recv_bufs': [262144],                      # 256KB (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [10.0, 50.0],                 # video rates (2)
        'net_delays': [25, 40],                     # 50-80ms RTT during handover (2)
        'net_jitters': [15, 25],                    # handover spike: 30-50ms additional (2)
        'net_losses': [0.5, 1.0],                   # handover loss: brief burst ~1% (2)
        'congestion_algos': ['cubic', 'bbr'],       # both CC (2)
        'duration': 10.0,
        # 1 * 1 * 1 * 2 * 2 * 2 * 2 * 2 = 32 experiments
        # With 3 iterations: 96 runs (~30 min)
    },
    'starlink-realistic-degraded': {
        # Real degraded conditions: obstruction, weather, congestion
        # Still within realistic Starlink bounds (RTT < 100ms)
        'recv_bufs': [262144],                      # 256KB (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [10.0, 25.0],                 # SD/HD video (2)
        'net_delays': [30, 45],                     # 60-90ms RTT - degraded but real (2)
        'net_jitters': [10, 20],                    # higher jitter from obstructions (2)
        'net_losses': [1.0, 2.0],                   # elevated loss: 1-2% (2)
        'congestion_algos': ['cubic', 'bbr'],       # both CC (2)
        'duration': 10.0,
        # 1 * 1 * 1 * 2 * 2 * 2 * 2 * 2 = 32 experiments
        # With 3 iterations: 96 runs (~30 min)
    },
    'starlink-realistic-all': {
        # Combined realistic profiles for comprehensive testing
        # Covers: baseline, handover, degraded conditions
        'recv_bufs': [262144],                      # 256KB (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [10.0, 50.0],                 # video rates (2)
        'net_delays': [15, 25, 40],                 # baseline/handover/degraded RTT (3)
        'net_jitters': [7, 15, 25],                 # baseline/handover/degraded jitter (3)
        'net_losses': [0.2, 1.0, 2.0],              # baseline/handover/degraded loss (3)
        'congestion_algos': ['cubic', 'bbr'],       # both CC (2)
        'duration': 10.0,
        # 1 * 1 * 1 * 2 * 3 * 3 * 3 * 2 = 108 experiments
        # With 3 iterations: 324 runs (~2 hours)
    },
    # Phase 14 Total: 144 + 96 + 96 + 324 = 660 runs (or run individual presets)

    # =========================================================================
    # Phase 15: Stuttering Sender Experiments
    # Goal: Characterize TCP behavior when sender stalls intermittently
    # =========================================================================

    'sender-stall-periodic': {
        # Periodic sender stalls - models encoder pauses, disk I/O
        'recv_bufs': [262144],                      # 256KB - fast receiver (1)
        'delays_ms': [1],                           # minimal receiver delay (1)
        'read_sizes': [16384],                      # 16KB reads (1)
        'send_rates': [10.0, 20.0],                 # video bitrates (2)
        'net_delays': [0, 25, 50],                  # LAN, WAN, high-lat (3)
        'net_jitters': [0],                         # no network jitter (1)
        'net_losses': [0],                          # no network loss (1)
        'congestion_algos': ['cubic', 'bbr'],       # both CC (2)
        'sender_stall_ms_list': [10, 20, 50, 100, 200],  # stall durations (5)
        'sender_stall_interval_ms_list': [200, 500, 1000],  # stall frequency (3)
        'sender_stall_pattern': 'periodic',
        'duration': 15.0,
        # 1 * 1 * 1 * 2 * 3 * 1 * 1 * 2 * 5 * 3 = 180 experiments
        # With 2 iterations: 360 runs (~2.5 hours)
    },

    'sender-stall-quick': {
        # Quick sender stall test for validation
        'recv_bufs': [262144],                      # 256KB (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [10.0],                       # single bitrate (1)
        'net_delays': [0, 25],                      # LAN + WAN (2)
        'net_jitters': [0],                         # no jitter (1)
        'net_losses': [0],                          # no loss (1)
        'congestion_algos': ['cubic', 'bbr'],       # both CC (2)
        'sender_stall_ms_list': [50, 100],          # 2 stall durations (2)
        'sender_stall_interval_ms_list': [500],     # single interval (1)
        'sender_stall_pattern': 'periodic',
        'duration': 10.0,
        # 1 * 1 * 1 * 1 * 2 * 1 * 1 * 2 * 2 * 1 = 8 experiments
        # Quick validation in ~5 minutes
    },

    'sender-stall-vs-receiver': {
        # Direct comparison: sender stall vs receiver stall
        # Run this preset TWICE: once with sender_stall, once without (receiver stall)
        'recv_bufs': [262144],                      # 256KB (1)
        'delays_ms': [1, 50, 100],                  # fast/med/slow receiver (3) - when no sender stall
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [10.0],                       # single bitrate (1)
        'net_delays': [0, 25],                      # LAN + WAN (2)
        'net_jitters': [0],                         # no jitter (1)
        'net_losses': [0],                          # no loss (1)
        'congestion_algos': ['cubic'],              # single CC for comparison (1)
        'sender_stall_ms_list': [0, 50, 100],       # no stall, 50ms, 100ms (3)
        'sender_stall_interval_ms_list': [500],     # fixed interval (1)
        'sender_stall_pattern': 'periodic',
        'duration': 10.0,
        # 1 * 3 * 1 * 1 * 2 * 1 * 1 * 1 * 3 * 1 = 18 experiments
        # With 3 iterations: 54 runs (~30 min)
    },

    'sender-stall-burst': {
        # Burst sender stalls - models I-frame generation, disk flush
        'recv_bufs': [262144],                      # 256KB (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [10.0, 20.0],                 # video bitrates (2)
        'net_delays': [0, 25],                      # LAN + WAN (2)
        'net_jitters': [0],                         # no jitter (1)
        'net_losses': [0],                          # no loss (1)
        'congestion_algos': ['cubic', 'bbr'],       # both CC (2)
        'sender_stall_ms_list': [50, 100, 200],     # stall durations (3)
        'sender_stall_interval_ms_list': [1000, 2000],  # burst intervals (2)
        'sender_stall_pattern': 'burst',
        'sender_burst_size': 3,                     # 3 stalls per burst
        'duration': 15.0,
        # 1 * 1 * 1 * 2 * 2 * 1 * 1 * 2 * 3 * 2 = 48 experiments
        # With 3 iterations: 144 runs (~1 hour)
    },

    'sender-stall-recovery': {
        # Focus on recovery behavior after long stalls
        'recv_bufs': [262144],                      # 256KB (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [10.0],                       # single bitrate (1)
        'net_delays': [0, 25, 50],                  # varying RTT (3)
        'net_jitters': [0],                         # no jitter (1)
        'net_losses': [0],                          # no loss (1)
        'congestion_algos': ['cubic', 'bbr'],       # both CC (2)
        'sender_stall_ms_list': [100, 500, 1000, 2000],  # long stalls (4)
        'sender_stall_interval_ms_list': [5000],    # infrequent (1)
        'sender_stall_pattern': 'periodic',
        'duration': 20.0,
        # 1 * 1 * 1 * 1 * 3 * 1 * 1 * 2 * 4 * 1 = 24 experiments
        # With 3 iterations: 72 runs (~45 min)
    },

    'sender-stall-high-rtt': {
        # Test sender stall diagnostic at higher RTT values
        # Goal: Find the RTT ceiling where >60ms IPG threshold fails
        'recv_bufs': [262144],                      # 256KB (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [10.0],                       # single bitrate (1)
        'net_delays': [50, 75, 100],                # higher RTT (3)
        'net_jitters': [0],                         # no jitter (1)
        'net_losses': [0],                          # no loss (1)
        'congestion_algos': ['cubic'],              # single CC (1)
        'sender_stall_ms_list': [0, 100],           # healthy vs stall (2)
        'sender_stall_interval_ms_list': [500],     # fixed interval (1)
        'sender_stall_pattern': 'periodic',
        'duration': 10.0,
        # 1 * 1 * 1 * 1 * 3 * 1 * 1 * 1 * 2 * 1 = 6 experiments
        # With 3 iterations: 18 runs (~10 min)
    },

    # =========================================================================
    # VERIFICATION EXPERIMENTS (Added 2025-12-28)
    # Goal: Establish definitive baseline values and verify blog claims
    # These presets replace ad-hoc experiments with consistent parameters
    # =========================================================================

    'baseline-verification': {
        # Establish definitive baseline throughput at each RTT level
        # No jitter, no loss - just measure TCP throughput vs RTT
        'recv_bufs': [262144],                      # 256KB fixed (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [5.0, 10.0],                  # SD and HD video (2)
        'net_delays': [5, 12, 25, 50],              # 10/24/50/100ms RTT (4)
        'net_jitters': [0],                         # no jitter (1)
        'net_losses': [0],                          # no loss (1)
        'congestion_algos': ['cubic', 'bbr'],       # both CC (2)
        'duration': 10.0,
        # 1 * 1 * 1 * 2 * 4 * 1 * 1 * 2 = 16 experiments
        # With 5 iterations: 80 runs (~30 min)
    },

    'loss-tolerance-clean': {
        # Loss tolerance at 50ms RTT (as per RERUN-PLAN)
        # Isolates loss effect - no jitter
        'recv_bufs': [262144],                      # 256KB fixed (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [5.0, 10.0],                  # SD and HD video (2)
        'net_delays': [25],                         # 50ms RTT fixed (1)
        'net_jitters': [0],                         # no jitter (1)
        'net_losses': [0, 0.1, 0.25, 0.5, 1, 2, 5], # loss sweep (7)
        'congestion_algos': ['cubic', 'bbr'],       # both CC (2)
        'duration': 10.0,
        # 1 * 1 * 1 * 2 * 1 * 1 * 7 * 2 = 28 experiments
        # With 5 iterations: 140 runs (~50 min)
    },

    'jitter-cliff-verification': {
        # Verify jitter cliff location at multiple RTT values
        # Zero loss to isolate jitter effect
        'recv_bufs': [262144],                      # 256KB fixed (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [5.0, 10.0],                  # SD and HD video (2)
        'net_delays': [12, 25, 50],                 # 24/50/100ms RTT (3)
        'net_jitters': [0, 4, 8, 12, 16, 20, 24],   # jitter sweep (7)
        'net_losses': [0],                          # no loss (1)
        'congestion_algos': ['cubic', 'bbr'],       # both CC (2)
        'duration': 10.0,
        # 1 * 1 * 1 * 2 * 3 * 7 * 1 * 2 = 84 experiments
        # With 5 iterations: 420 runs (~140 min)
    },

    'chaos-zone-statistical': {
        # Deep analysis of chaos zone with high iteration count
        # 50ms RTT, jitter 8-16ms (16-32% of RTT)
        'recv_bufs': [262144],                      # 256KB fixed (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [5.0, 10.0],                  # SD and HD video (2)
        'net_delays': [25],                         # 50ms RTT (1)
        'net_jitters': [8, 10, 12, 14, 16],         # chaos zone (5)
        'net_losses': [0],                          # no loss (1)
        'congestion_algos': ['cubic', 'bbr'],       # both CC (2)
        'duration': 10.0,
        # 1 * 1 * 1 * 2 * 1 * 5 * 1 * 2 = 20 experiments
        # With 20 iterations: 400 runs (~135 min)
    },

    'starlink-canonical-baseline': {
        # Starlink baseline with exact cited values
        # RTT: 27ms (median from Starlink official)
        # Jitter: 7ms (APNIC average)
        # Loss: sweep around cited 0.13% (WirelessMoves)
        'recv_bufs': [262144],                      # 256KB fixed (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [5.0, 10.0],                  # SD and HD video (2)
        'net_delays': [13],                         # ~27ms RTT (1)
        'net_jitters': [7],                         # APNIC average (1)
        'net_losses': [0.1, 0.125, 0.15, 0.175, 0.2],  # sweep around 0.13% (5)
        'congestion_algos': ['cubic', 'bbr'],       # both CC (2)
        'duration': 15.0,
        # 1 * 1 * 1 * 2 * 1 * 1 * 5 * 2 = 20 experiments
        # With 10 iterations: 200 runs (~75 min)
    },

    'starlink-canonical-handover': {
        # Starlink handover conditions
        # RTT: 60ms (30ms baseline + 30ms spike during handover)
        # Jitter: 40ms (APNIC: latency shifts 30-50ms at handover)
        # Loss: sweep around cited 1%
        'recv_bufs': [262144],                      # 256KB fixed (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [5.0, 10.0],                  # SD and HD video (2)
        'net_delays': [30],                         # 60ms RTT (1)
        'net_jitters': [40],                        # handover spike (1)
        'net_losses': [0.5, 0.75, 1.0, 1.25, 1.5],  # sweep around 1% (5)
        'congestion_algos': ['cubic', 'bbr'],       # both CC (2)
        'duration': 15.0,
        # 1 * 1 * 1 * 2 * 1 * 1 * 5 * 2 = 20 experiments
        # With 10 iterations: 200 runs (~75 min)
    },

    'starlink-canonical-degraded': {
        # Starlink degraded conditions (obstruction, weather)
        # RTT: 80ms (elevated but still realistic)
        # Jitter: 15ms (higher from obstructions)
        # Loss: sweep around cited 1.5%
        'recv_bufs': [262144],                      # 256KB fixed (1)
        'delays_ms': [1],                           # fast receiver (1)
        'read_sizes': [16384],                      # 16KB (1)
        'send_rates': [5.0, 10.0],                  # SD and HD video (2)
        'net_delays': [40],                         # 80ms RTT (1)
        'net_jitters': [15],                        # obstruction jitter (1)
        'net_losses': [1.0, 1.25, 1.5, 1.75, 2.0],  # sweep around 1.5% (5)
        'congestion_algos': ['cubic', 'bbr'],       # both CC (2)
        'duration': 15.0,
        # 1 * 1 * 1 * 2 * 1 * 1 * 5 * 2 = 20 experiments
        # With 10 iterations: 200 runs (~75 min)
    },
}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--preset', '-p', choices=list(PRESETS.keys()), default='quick',
                        help='Sweep preset (default: quick)')
    parser.add_argument('--quick', action='store_true', help='Shortcut for --preset quick')
    parser.add_argument('--output', '-o', required=True)
    parser.add_argument('--work-dir', required=True)
    parser.add_argument('--iterations', '-i', type=int, default=1,
                        help='Number of times to repeat each experiment (default: 1)')
    args = parser.parse_args()

    preset_name = 'quick' if args.quick else args.preset
    preset = PRESETS[preset_name]

    logging.info(f"Using preset: {preset_name}")

    # Extract ECN configuration from preset (with defaults)
    ecn_enabled = preset.get('ecn_enabled', False)
    red_limit_kb = preset.get('red_limit_kb', 200)
    red_min_kb = preset.get('red_min_kb', 30)
    red_max_kb = preset.get('red_max_kb', 100)

    if ecn_enabled:
        logging.info(f"ECN enabled: RED queue limit={red_limit_kb}KB, min={red_min_kb}KB, max={red_max_kb}KB")

    # Extract sender stall configuration (Phase 15)
    sender_stall_ms_list = preset.get('sender_stall_ms_list', [0])
    sender_stall_interval_ms_list = preset.get('sender_stall_interval_ms_list', [0])
    sender_stall_pattern = preset.get('sender_stall_pattern', 'periodic')
    sender_burst_size = preset.get('sender_burst_size', 1)

    has_sender_stall = sender_stall_ms_list != [0]
    if has_sender_stall:
        logging.info(f"Sender stall enabled: durations={sender_stall_ms_list}ms, intervals={sender_stall_interval_ms_list}ms, pattern={sender_stall_pattern}")

    configs = []
    for iteration in range(args.iterations):
        for recv_buf, delay_ms, read_size, send_rate, net_delay, net_jitter, net_loss, cc_algo, stall_ms, stall_interval in itertools.product(
            preset['recv_bufs'],
            preset['delays_ms'],
            preset['read_sizes'],
            preset['send_rates'],
            preset['net_delays'],
            preset['net_jitters'],
            preset['net_losses'],
            preset['congestion_algos'],
            sender_stall_ms_list,
            sender_stall_interval_ms_list,
        ):
            configs.append(ExperimentConfig(
                recv_buf=recv_buf,
                delay_ms=delay_ms,
                read_size=read_size,
                send_rate_mbps=send_rate,
                duration=preset['duration'],
                net_delay_ms=net_delay,
                net_jitter_ms=net_jitter,
                net_loss_pct=net_loss,
                ecn_enabled=ecn_enabled,
                red_limit_kb=red_limit_kb,
                red_min_kb=red_min_kb,
                red_max_kb=red_max_kb,
                congestion_algo=cc_algo,
                sender_stall_ms=stall_ms,
                sender_stall_interval_ms=stall_interval,
                sender_stall_pattern=sender_stall_pattern,
                sender_stall_count=0,  # 0 = continuous pattern throughout test
            ))

    logging.info(f"Running {len(configs)} experiments ({args.iterations} iteration(s) per condition)")

    fieldnames = list(ExperimentMetrics.__dataclass_fields__.keys())
    results = []

    with open(args.output, 'w', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writeheader()

        for i, config in enumerate(configs):
            # Build log message with relevant params
            net_info = ""
            if config.net_delay_ms > 0 or config.net_loss_pct > 0:
                net_info = f", net={config.net_delay_ms}ms"
                if config.net_jitter_ms > 0:
                    net_info += f"±{config.net_jitter_ms}ms"
                if config.net_loss_pct > 0:
                    net_info += f" loss={config.net_loss_pct}%"
            cc_info = f", cc={config.congestion_algo}" if config.congestion_algo != 'cubic' else ""
            stall_info = ""
            if config.sender_stall_ms > 0:
                stall_info = f", sender_stall={config.sender_stall_ms}ms@{config.sender_stall_interval_ms}ms"

            logging.info(f"[{i+1}/{len(configs)}] buf={config.recv_buf}, delay={config.delay_ms}ms, "
                        f"rate={config.send_rate_mbps}MB/s{net_info}{cc_info}{stall_info}")

            metrics = run_experiment(config, args.work_dir)
            results.append(metrics)
            writer.writerow(asdict(metrics))
            csvfile.flush()

            if metrics.success:
                rtt_info = f", rtt_mean={metrics.rtt_mean_us/1000:.1f}ms" if metrics.rtt_samples > 0 else ""
                ecn_info = ""
                if config.ecn_enabled and (metrics.ecn_ece_count > 0 or metrics.ecn_cwr_count > 0):
                    ecn_info = f", ECE={metrics.ecn_ece_count}, CWR={metrics.ecn_cwr_count}"
                gap_info = ""
                if config.sender_stall_ms > 0:
                    gap_info = f", max_gap={metrics.inter_packet_gap_max_ms:.1f}ms, idle={metrics.idle_periods_count}"
                logging.info(f"  -> zero_window={metrics.zero_window_count}, "
                            f"throughput={metrics.actual_throughput_kbps:.0f}KB/s{rtt_info}{ecn_info}{gap_info}")
            else:
                logging.warning(f"  -> FAILED: {metrics.error}")

            time.sleep(0.5)

    successful = sum(1 for r in results if r.success)
    with_zw = sum(1 for r in results if r.success and r.zero_window_count > 0)
    logging.info(f"Complete: {successful}/{len(results)} successful, {with_zw} with zero-window")

if __name__ == '__main__':
    main()
PYTHON_SCRIPT

chmod +x "$WORK_DIR/ns_sweep.py"

# Run the namespace-aware sweep
sudo python3 "$WORK_DIR/ns_sweep.py" --preset "$PRESET_NAME" --output "$OUTPUT_CSV" --work-dir "$WORK_DIR" $EXTRA_ARGS

# Stop tcpdump
sudo kill $TCPDUMP_PID 2>/dev/null || true
wait $TCPDUMP_PID 2>/dev/null || true

# Cleanup temp dir
rm -rf "$WORK_DIR"

echo ""
echo "================================================"
echo "Sweep complete!"
echo "================================================"
echo "Results: $OUTPUT_CSV"
echo "PCAP:    $PCAP_FILE"
echo ""
echo "To analyze:"
echo "  python3 $SCRIPT_DIR/analyze.py $OUTPUT_CSV -o $OUTPUT_DIR/"
