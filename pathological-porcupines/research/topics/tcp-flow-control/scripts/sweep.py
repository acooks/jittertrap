#!/usr/bin/env python3
"""
TCP Flow Control Parameter Sweep

Systematically explores the relationship between:
- Receive buffer size
- Processing delay (time between recv() calls)
- Send rate (relative to receiver capacity)
- Read chunk size

Captures metrics on TCP zero-window events, throughput, and latency.

Part of Pathological Porcupines: Network Application Failure Simulations
"""

import argparse
import csv
import itertools
import json
import logging
import os
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass, field, asdict
from datetime import datetime
from pathlib import Path
from typing import Optional

# Add common utilities path
sys.path.insert(0, str(Path(__file__).parent.parent.parent))
from common.logging_utils import setup_logging, format_bytes, format_rate


@dataclass
class ExperimentConfig:
    """Configuration for a single experiment run."""
    recv_buf: int           # SO_RCVBUF size in bytes
    delay_ms: float         # Delay between recv() calls in milliseconds
    read_size: int          # Bytes to read per recv()
    send_rate_mbps: float   # Target send rate in MB/s
    duration: float = 10.0  # Test duration in seconds

    @property
    def receiver_capacity_bps(self) -> float:
        """Theoretical maximum receive rate in bytes/sec."""
        if self.delay_ms <= 0:
            return float('inf')
        return self.read_size / (self.delay_ms / 1000.0)

    @property
    def send_rate_bps(self) -> float:
        """Target send rate in bytes/sec."""
        return self.send_rate_mbps * 1024 * 1024

    @property
    def oversubscription_ratio(self) -> float:
        """How much faster sender is vs receiver capacity."""
        if self.receiver_capacity_bps == 0:
            return float('inf')
        return self.send_rate_bps / self.receiver_capacity_bps


@dataclass
class ExperimentMetrics:
    """Metrics collected from a single experiment run."""
    # Configuration echo
    recv_buf: int = 0
    delay_ms: float = 0.0
    read_size: int = 0
    send_rate_mbps: float = 0.0

    # Derived config values
    receiver_capacity_kbps: float = 0.0
    oversubscription_ratio: float = 0.0

    # Basic transfer metrics
    duration_actual: float = 0.0
    bytes_transferred: int = 0
    actual_throughput_kbps: float = 0.0

    # Zero-window metrics (from pcap analysis)
    zero_window_count: int = 0
    zero_window_duration_ms: float = 0.0
    zero_window_pct: float = 0.0

    # Window dynamics
    window_min: int = 0
    window_max: int = 0
    window_mean: float = 0.0
    window_oscillations: int = 0  # Count of significant window changes

    # Packet metrics
    total_packets: int = 0
    retransmit_count: int = 0
    dup_ack_count: int = 0

    # Timing
    timestamp: str = ""

    # Status
    success: bool = False
    error: str = ""


class Receiver:
    """TCP receiver with configurable slow processing."""

    def __init__(self, config: ExperimentConfig, port: int = 9999):
        self.config = config
        self.port = port
        self.server_socket = None
        self.client_socket = None
        self.running = False
        self.total_bytes = 0
        self.thread = None

    def start(self):
        """Start receiver in background thread."""
        self.running = True
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()
        time.sleep(0.2)  # Allow socket to bind

    def stop(self):
        """Stop receiver."""
        self.running = False
        if self.client_socket:
            try:
                self.client_socket.close()
            except:
                pass
        if self.server_socket:
            try:
                self.server_socket.close()
            except:
                pass
        if self.thread:
            self.thread.join(timeout=2.0)

    def _run(self):
        """Main receiver loop."""
        try:
            self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, self.config.recv_buf)
            self.server_socket.settimeout(1.0)
            self.server_socket.bind(('0.0.0.0', self.port))
            self.server_socket.listen(1)

            try:
                self.client_socket, _ = self.server_socket.accept()
                self.client_socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, self.config.recv_buf)
            except socket.timeout:
                return

            delay_sec = self.config.delay_ms / 1000.0

            while self.running:
                if delay_sec > 0:
                    time.sleep(delay_sec)
                try:
                    data = self.client_socket.recv(self.config.read_size)
                    if not data:
                        break
                    self.total_bytes += len(data)
                except (socket.timeout, ConnectionResetError, OSError):
                    break

        except Exception as e:
            logging.debug(f"Receiver error: {e}")


class Sender:
    """TCP sender with configurable send rate."""

    def __init__(self, config: ExperimentConfig, host: str = '127.0.0.1', port: int = 9999):
        self.config = config
        self.host = host
        self.port = port
        self.socket = None
        self.total_bytes = 0
        self.blocked_time = 0.0
        self.block_count = 0

    def run(self) -> tuple[int, float, int]:
        """
        Run sender for configured duration.
        Returns: (bytes_sent, blocked_time, block_count)
        """
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        try:
            self.socket.connect((self.host, self.port))
        except ConnectionRefusedError:
            return 0, 0.0, 0

        chunk_size = 8192
        data = b'X' * chunk_size
        target_bps = self.config.send_rate_bps
        target_interval = chunk_size / target_bps if target_bps > 0 else 0

        start_time = time.monotonic()
        end_time = start_time + self.config.duration
        next_send = start_time

        try:
            while time.monotonic() < end_time:
                now = time.monotonic()
                if now < next_send:
                    time.sleep(next_send - now)
                next_send = time.monotonic() + target_interval

                send_start = time.monotonic()
                try:
                    sent = self.socket.send(data)
                except (BrokenPipeError, ConnectionResetError):
                    break
                send_elapsed = time.monotonic() - send_start

                if send_elapsed > 0.05:  # >50ms considered blocked
                    self.blocked_time += send_elapsed
                    self.block_count += 1

                self.total_bytes += sent

        finally:
            self.socket.close()

        return self.total_bytes, self.blocked_time, self.block_count


def analyze_pcap(pcap_path: str) -> dict:
    """
    Analyze pcap file for TCP flow control metrics.
    Returns dict with zero-window counts, window stats, etc.
    """
    metrics = {
        'zero_window_count': 0,
        'zero_window_duration_ms': 0.0,
        'window_min': 0,
        'window_max': 0,
        'window_mean': 0.0,
        'window_values': [],
        'total_packets': 0,
        'retransmit_count': 0,
        'dup_ack_count': 0,
    }

    if not os.path.exists(pcap_path):
        return metrics

    # Count zero-window packets
    try:
        result = subprocess.run(
            ['tshark', '-r', pcap_path, '-Y', 'tcp.analysis.zero_window', '-T', 'fields', '-e', 'frame.number'],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            lines = [l for l in result.stdout.strip().split('\n') if l]
            metrics['zero_window_count'] = len(lines)
    except:
        pass

    # Get window size statistics
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
                metrics['window_values'] = windows
                metrics['window_min'] = min(windows)
                metrics['window_max'] = max(windows)
                metrics['window_mean'] = sum(windows) / len(windows)
                metrics['total_packets'] = len(windows)

                # Count oscillations (significant changes > 20% of max)
                threshold = metrics['window_max'] * 0.2
                oscillations = 0
                for i in range(1, len(windows)):
                    if abs(windows[i] - windows[i-1]) > threshold:
                        oscillations += 1
                metrics['window_oscillations'] = oscillations
    except:
        pass

    # Count retransmissions
    try:
        result = subprocess.run(
            ['tshark', '-r', pcap_path, '-Y', 'tcp.analysis.retransmission', '-T', 'fields', '-e', 'frame.number'],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            lines = [l for l in result.stdout.strip().split('\n') if l]
            metrics['retransmit_count'] = len(lines)
    except:
        pass

    # Count duplicate ACKs
    try:
        result = subprocess.run(
            ['tshark', '-r', pcap_path, '-Y', 'tcp.analysis.duplicate_ack', '-T', 'fields', '-e', 'frame.number'],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            lines = [l for l in result.stdout.strip().split('\n') if l]
            metrics['dup_ack_count'] = len(lines)
    except:
        pass

    return metrics


def run_experiment(config: ExperimentConfig, capture_pcap: bool = True) -> ExperimentMetrics:
    """
    Run a single experiment with given configuration.
    Returns collected metrics.
    """
    metrics = ExperimentMetrics(
        recv_buf=config.recv_buf,
        delay_ms=config.delay_ms,
        read_size=config.read_size,
        send_rate_mbps=config.send_rate_mbps,
        receiver_capacity_kbps=config.receiver_capacity_bps / 1024,
        oversubscription_ratio=config.oversubscription_ratio,
        timestamp=datetime.now().isoformat(),
    )

    pcap_path = None
    tcpdump_proc = None

    try:
        # Start packet capture if requested
        if capture_pcap:
            pcap_fd, pcap_path = tempfile.mkstemp(suffix='.pcap')
            os.close(pcap_fd)
            tcpdump_proc = subprocess.Popen(
                ['tcpdump', '-i', 'lo', '-w', pcap_path, 'port', '9999'],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
            )
            time.sleep(0.3)  # Allow tcpdump to start

        # Start receiver
        receiver = Receiver(config)
        receiver.start()

        # Run sender
        sender = Sender(config)
        start_time = time.monotonic()
        bytes_sent, blocked_time, block_count = sender.run()
        actual_duration = time.monotonic() - start_time

        # Stop receiver
        time.sleep(0.2)  # Allow final packets
        receiver.stop()

        # Stop capture
        if tcpdump_proc:
            tcpdump_proc.terminate()
            tcpdump_proc.wait(timeout=2)
            time.sleep(0.2)

        # Collect metrics
        metrics.duration_actual = actual_duration
        metrics.bytes_transferred = bytes_sent
        if actual_duration > 0:
            metrics.actual_throughput_kbps = (bytes_sent / actual_duration) / 1024

        # Analyze pcap
        if pcap_path and os.path.exists(pcap_path):
            pcap_metrics = analyze_pcap(pcap_path)
            metrics.zero_window_count = pcap_metrics['zero_window_count']
            metrics.window_min = pcap_metrics['window_min']
            metrics.window_max = pcap_metrics['window_max']
            metrics.window_mean = pcap_metrics['window_mean']
            metrics.window_oscillations = pcap_metrics.get('window_oscillations', 0)
            metrics.total_packets = pcap_metrics['total_packets']
            metrics.retransmit_count = pcap_metrics['retransmit_count']
            metrics.dup_ack_count = pcap_metrics['dup_ack_count']

            if actual_duration > 0:
                # Estimate zero-window duration (rough: assume each event ~RTT duration)
                metrics.zero_window_duration_ms = metrics.zero_window_count * 10  # ~10ms per event estimate
                metrics.zero_window_pct = (metrics.zero_window_duration_ms / 1000) / actual_duration * 100

        metrics.success = True

    except Exception as e:
        metrics.error = str(e)
        metrics.success = False

    finally:
        if pcap_path and os.path.exists(pcap_path):
            os.unlink(pcap_path)

    return metrics


def generate_sweep_configs(
    recv_bufs: list[int],
    delays_ms: list[float],
    read_sizes: list[int],
    send_rates_mbps: list[float],
    duration: float = 10.0
) -> list[ExperimentConfig]:
    """Generate all combinations of experiment configurations."""
    configs = []
    for recv_buf, delay_ms, read_size, send_rate in itertools.product(
        recv_bufs, delays_ms, read_sizes, send_rates_mbps
    ):
        configs.append(ExperimentConfig(
            recv_buf=recv_buf,
            delay_ms=delay_ms,
            read_size=read_size,
            send_rate_mbps=send_rate,
            duration=duration,
        ))
    return configs


def run_sweep(
    configs: list[ExperimentConfig],
    output_path: str,
    capture_pcap: bool = True,
    progress_callback=None
) -> list[ExperimentMetrics]:
    """
    Run parameter sweep across all configurations.
    Saves results incrementally to CSV.
    """
    results = []

    # Prepare CSV file
    fieldnames = list(ExperimentMetrics.__dataclass_fields__.keys())

    with open(output_path, 'w', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writeheader()

        for i, config in enumerate(configs):
            if progress_callback:
                progress_callback(i, len(configs), config)

            logging.info(f"[{i+1}/{len(configs)}] Running: "
                        f"buf={format_bytes(config.recv_buf)}, "
                        f"delay={config.delay_ms}ms, "
                        f"read={format_bytes(config.read_size)}, "
                        f"rate={config.send_rate_mbps}MB/s "
                        f"(oversub={config.oversubscription_ratio:.1f}x)")

            metrics = run_experiment(config, capture_pcap=capture_pcap)
            results.append(metrics)

            # Write incrementally
            row = asdict(metrics)
            writer.writerow(row)
            csvfile.flush()

            if metrics.success:
                logging.info(f"  -> zero_window={metrics.zero_window_count}, "
                            f"throughput={metrics.actual_throughput_kbps:.0f}KB/s, "
                            f"oscillations={metrics.window_oscillations}")
            else:
                logging.warning(f"  -> FAILED: {metrics.error}")

            # Brief pause between experiments
            time.sleep(0.5)

    return results


def default_sweep_params():
    """Return default parameter ranges for sweep."""
    return {
        'recv_bufs': [4096, 8192, 16384, 32768, 65536],
        'delays_ms': [10, 25, 50, 100, 200],
        'read_sizes': [2048, 4096, 8192],
        'send_rates_mbps': [0.1, 0.25, 0.5, 1.0, 2.0],
        'duration': 10.0,
    }


def quick_sweep_params():
    """Return reduced parameter ranges for quick testing."""
    return {
        'recv_bufs': [8192, 32768],
        'delays_ms': [25, 100],
        'read_sizes': [4096],
        'send_rates_mbps': [0.25, 1.0],
        'duration': 5.0,
    }


def main():
    parser = argparse.ArgumentParser(
        description="TCP Flow Control Parameter Sweep",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Quick test run (8 experiments)
    python sweep.py --quick --output results_quick.csv

    # Full sweep (375 experiments)
    python sweep.py --output results_full.csv

    # Custom parameters
    python sweep.py --recv-bufs 4096 8192 --delays 10 50 --rates 0.5 1.0 --output custom.csv
        """
    )

    parser.add_argument('--output', '-o', default='sweep_results.csv',
                        help='Output CSV file path')
    parser.add_argument('--quick', action='store_true',
                        help='Run quick sweep with reduced parameters')
    parser.add_argument('--recv-bufs', type=int, nargs='+',
                        help='Receive buffer sizes to test (bytes)')
    parser.add_argument('--delays', type=float, nargs='+',
                        help='Processing delays to test (ms)')
    parser.add_argument('--read-sizes', type=int, nargs='+',
                        help='Read chunk sizes to test (bytes)')
    parser.add_argument('--rates', type=float, nargs='+',
                        help='Send rates to test (MB/s)')
    parser.add_argument('--duration', type=float, default=10.0,
                        help='Duration per experiment (seconds)')
    parser.add_argument('--no-pcap', action='store_true',
                        help='Skip pcap capture (faster but fewer metrics)')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Verbose output')

    args = parser.parse_args()
    setup_logging(verbose=args.verbose)

    # Determine parameters
    if args.quick:
        params = quick_sweep_params()
    else:
        params = default_sweep_params()

    # Override with command-line args
    if args.recv_bufs:
        params['recv_bufs'] = args.recv_bufs
    if args.delays:
        params['delays_ms'] = args.delays
    if args.read_sizes:
        params['read_sizes'] = args.read_sizes
    if args.rates:
        params['send_rates_mbps'] = args.rates
    if args.duration:
        params['duration'] = args.duration

    # Generate configurations
    configs = generate_sweep_configs(**params)

    logging.info(f"TCP Flow Control Parameter Sweep")
    logging.info(f"================================")
    logging.info(f"Configurations: {len(configs)}")
    logging.info(f"Duration per experiment: {params['duration']}s")
    logging.info(f"Estimated total time: {len(configs) * (params['duration'] + 1):.0f}s")
    logging.info(f"Output: {args.output}")
    logging.info(f"")
    logging.info(f"Parameters:")
    logging.info(f"  Receive buffers: {[format_bytes(b) for b in params['recv_bufs']]}")
    logging.info(f"  Delays: {params['delays_ms']} ms")
    logging.info(f"  Read sizes: {[format_bytes(s) for s in params['read_sizes']]}")
    logging.info(f"  Send rates: {params['send_rates_mbps']} MB/s")
    logging.info(f"")

    # Run sweep
    start_time = time.monotonic()
    results = run_sweep(configs, args.output, capture_pcap=not args.no_pcap)
    elapsed = time.monotonic() - start_time

    # Summary
    successful = sum(1 for r in results if r.success)
    with_zero_window = sum(1 for r in results if r.success and r.zero_window_count > 0)

    logging.info(f"")
    logging.info(f"================================")
    logging.info(f"Sweep Complete")
    logging.info(f"================================")
    logging.info(f"Total time: {elapsed:.0f}s")
    logging.info(f"Successful: {successful}/{len(results)}")
    logging.info(f"With zero-window events: {with_zero_window}/{successful}")
    logging.info(f"Results saved to: {args.output}")

    return 0 if successful == len(results) else 1


if __name__ == '__main__':
    sys.exit(main())
