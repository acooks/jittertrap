#!/usr/bin/env python3
"""
RTP Jitter Spike Receiver - Measures RFC 3550 jitter and detects spikes

Receives RTP packets, calculates interarrival jitter per RFC 3550,
and detects jitter spike events.

Part of Pathological Porcupines: Network Application Failure Simulations
"""

import argparse
import logging
import socket
import sys
import time
from collections import defaultdict
from pathlib import Path

# Add common utilities path
sys.path.insert(0, str(Path(__file__).parent.parent.parent.parent))
from common.network import create_udp_socket
from common.protocol import parse_rtp_header, calculate_rtp_jitter, RTPPacket
from common.logging_utils import setup_logging, format_bytes


# Default configuration
DEFAULT_PORT = 5004  # Standard RTP port for JitterTrap detection
DEFAULT_DURATION = 20  # slightly longer than sender
DEFAULT_SPIKE_THRESHOLD = 100  # ms - jitter above this is a spike


def setup_argparse() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="RTP Jitter Spike Receiver - measures RFC 3550 jitter",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Basic receiver (port 5004 for JitterTrap RTP detection)
    python receiver.py

    # Different spike threshold
    python receiver.py --spike-threshold 80

Expected output:
    - Jitter statistics and histogram
    - Count of jitter spike events
    - RFC 3550 jitter calculation
        """
    )
    parser.add_argument("--port", "-p", type=int, default=DEFAULT_PORT,
                        help=f"Listen port (default: {DEFAULT_PORT})")
    parser.add_argument("--duration", "-d", type=float, default=DEFAULT_DURATION,
                        help=f"Receive duration in seconds (default: {DEFAULT_DURATION})")
    parser.add_argument("--spike-threshold", type=float, default=DEFAULT_SPIKE_THRESHOLD,
                        help=f"Jitter threshold for spike detection in ms (default: {DEFAULT_SPIKE_THRESHOLD})")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    parser.add_argument("--quiet", "-q", action="store_true",
                        help="Quiet mode (errors only)")
    return parser.parse_args()


class JitterSpikeReceiver:
    """
    RTP receiver that measures jitter and detects spikes.

    Implements RFC 3550 jitter calculation and tracks spike events.
    """

    # Jitter histogram bucket boundaries in milliseconds
    JITTER_BUCKETS = [0, 5, 10, 20, 50, 100, 200, 500]

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.sock = None
        self.total_packets = 0
        self.total_bytes = 0
        self.start_time = None

        # Jitter tracking
        self.prev_arrival_ms = None
        self.prev_timestamp = None
        self.jitter = 0.0  # RFC 3550 smoothed jitter
        self.jitter_samples = []  # Raw instantaneous jitter values
        self.jitter_histogram = defaultdict(int)

        # Spike detection
        self.spike_events = []  # (time, jitter_value) of spikes
        self.clock_rate = RTPPacket.CLOCK_RATE_VIDEO

        # Packet tracking
        self.last_seq = None
        self.out_of_order = 0
        self.gaps = 0
        self.ssrc = None

    def get_jitter_bucket(self, jitter_ms: float) -> str:
        """Map jitter value to histogram bucket."""
        for i, edge in enumerate(self.JITTER_BUCKETS[1:], 1):
            if jitter_ms < edge:
                return f"<{edge}ms"
        return f">={self.JITTER_BUCKETS[-1]}ms"

    def setup(self) -> None:
        """Initialize UDP socket."""
        self.sock = create_udp_socket(timeout=2.0)
        self.sock.bind(('0.0.0.0', self.args.port))
        logging.info(f"Receiver listening on port {self.args.port}")
        logging.info(f"Spike threshold: {self.args.spike_threshold}ms")

    def run(self) -> int:
        """Main execution. Returns exit code."""
        self.setup()
        self.start_time = time.monotonic()
        end_time = self.start_time + self.args.duration
        last_status_time = self.start_time

        logging.info("Waiting for RTP packets...")

        try:
            while time.monotonic() < end_time:
                try:
                    data, addr = self.sock.recvfrom(65536)
                except socket.timeout:
                    if self.total_packets > 0:
                        logging.debug("Receive timeout")
                    continue

                arrival_time = time.monotonic()
                arrival_ms = (arrival_time - self.start_time) * 1000

                # Parse RTP header
                try:
                    version, marker, pt, seq, timestamp, ssrc, payload = parse_rtp_header(data)
                except ValueError as e:
                    logging.warning(f"Invalid RTP packet: {e}")
                    continue

                self.total_packets += 1
                self.total_bytes += len(data)

                # Track SSRC
                if self.ssrc is None:
                    self.ssrc = ssrc
                    logging.info(f"Tracking SSRC: 0x{ssrc:08X}")
                elif ssrc != self.ssrc:
                    logging.debug(f"Different SSRC: 0x{ssrc:08X}")
                    continue

                # Check sequence
                if self.last_seq is not None:
                    expected_seq = (self.last_seq + 1) & 0xFFFF
                    if seq != expected_seq:
                        if seq < self.last_seq and (self.last_seq - seq) < 1000:
                            self.out_of_order += 1
                        else:
                            gap = (seq - expected_seq) & 0xFFFF
                            if gap < 100:  # Reasonable gap
                                self.gaps += gap
                self.last_seq = seq

                # Calculate jitter if not first packet
                if self.prev_arrival_ms is not None:
                    # Calculate instantaneous jitter (deviation from expected)
                    arrival_diff = arrival_ms - self.prev_arrival_ms
                    ts_diff_ms = ((timestamp - self.prev_timestamp) & 0xFFFFFFFF) * 1000 / self.clock_rate

                    # Handle timestamp wraparound
                    if ts_diff_ms > 2000000000:  # Wraparound
                        ts_diff_ms -= 4294967296 * 1000 / self.clock_rate

                    instantaneous_jitter = abs(arrival_diff - ts_diff_ms)
                    self.jitter_samples.append(instantaneous_jitter)

                    # Update RFC 3550 smoothed jitter
                    self.jitter = calculate_rtp_jitter(
                        arrival_ms, timestamp, self.clock_rate,
                        self.prev_arrival_ms, self.prev_timestamp, self.jitter
                    )

                    # Update histogram
                    bucket = self.get_jitter_bucket(instantaneous_jitter)
                    self.jitter_histogram[bucket] += 1

                    # Detect spike
                    if instantaneous_jitter >= self.args.spike_threshold:
                        elapsed = arrival_time - self.start_time
                        self.spike_events.append((elapsed, instantaneous_jitter))
                        logging.info(f"JITTER SPIKE detected: {instantaneous_jitter:.1f}ms at {elapsed:.1f}s")

                self.prev_arrival_ms = arrival_ms
                self.prev_timestamp = timestamp

                # Periodic status
                now = time.monotonic()
                if now - last_status_time >= 2.0:
                    elapsed = now - self.start_time
                    fps = self.total_packets / elapsed
                    logging.info(f"Elapsed: {elapsed:.1f}s, Packets: {self.total_packets} ({fps:.1f} fps), "
                                f"Jitter: {self.jitter:.1f}ms, Spikes: {len(self.spike_events)}")
                    last_status_time = now

        except KeyboardInterrupt:
            logging.info("Interrupted by user")
        finally:
            self.cleanup()

        return self.report_results()

    def report_results(self) -> int:
        """Report test results. Returns exit code."""
        elapsed = time.monotonic() - self.start_time

        logging.info("")
        logging.info("=" * 50)
        logging.info("RTP JITTER SPIKE RECEIVER RESULTS")
        logging.info("=" * 50)
        logging.info(f"Duration: {elapsed:.1f}s")
        logging.info(f"Total packets: {self.total_packets}")
        logging.info(f"Total data: {format_bytes(self.total_bytes)}")
        if self.out_of_order > 0:
            logging.info(f"Out of order: {self.out_of_order}")
        if self.gaps > 0:
            logging.info(f"Sequence gaps: {self.gaps} packets")
        logging.info("")

        if not self.jitter_samples:
            logging.error("No jitter data collected")
            return 1

        # Jitter statistics
        jitter_sorted = sorted(self.jitter_samples)
        logging.info("Jitter Statistics (instantaneous):")
        logging.info(f"  Min: {min(self.jitter_samples):.2f}ms")
        logging.info(f"  Max: {max(self.jitter_samples):.2f}ms")
        logging.info(f"  Median: {jitter_sorted[len(jitter_sorted)//2]:.2f}ms")
        avg_jitter = sum(self.jitter_samples) / len(self.jitter_samples)
        logging.info(f"  Mean: {avg_jitter:.2f}ms")
        logging.info(f"  RFC 3550 smoothed: {self.jitter:.2f}ms")
        logging.info("")

        # Jitter histogram
        logging.info("Jitter Histogram:")
        total = sum(self.jitter_histogram.values())

        bucket_order = [f"<{e}ms" for e in self.JITTER_BUCKETS[1:]]
        bucket_order.append(f">={self.JITTER_BUCKETS[-1]}ms")

        for bucket in bucket_order:
            count = self.jitter_histogram.get(bucket, 0)
            pct = (count / total * 100) if total > 0 else 0
            bar = '#' * int(pct / 2)
            if count > 0:
                logging.info(f"  {bucket:>10s}: {count:5d} ({pct:5.1f}%) {bar}")

        logging.info("")

        # Spike analysis
        logging.info(f"Spike Events (>{self.args.spike_threshold}ms): {len(self.spike_events)}")
        if self.spike_events:
            for i, (t, j) in enumerate(self.spike_events):
                logging.info(f"  Spike {i+1}: {j:.1f}ms at {t:.1f}s")

            if len(self.spike_events) >= 2:
                intervals = []
                for i in range(1, len(self.spike_events)):
                    intervals.append(self.spike_events[i][0] - self.spike_events[i-1][0])
                avg_interval = sum(intervals) / len(intervals)
                logging.info(f"  Average spike interval: {avg_interval:.1f}s")

        logging.info("")

        # Self-checks
        passed = True
        checks = []

        # Check 1: Did we receive enough packets?
        if self.total_packets >= 100:
            checks.append(("Packet count", True, f"{self.total_packets} packets"))
        else:
            checks.append(("Packet count", False,
                          f"Only {self.total_packets} packets"))
            passed = False

        # Check 2: Did we detect jitter spikes?
        if len(self.spike_events) >= 2:
            checks.append(("Jitter spikes detected", True,
                          f"{len(self.spike_events)} spikes"))
        else:
            checks.append(("Jitter spikes detected", False,
                          f"Only {len(self.spike_events)} spikes (expected ≥2)"))
            passed = False

        # Check 3: Maximum jitter above threshold?
        max_jitter = max(self.jitter_samples)
        if max_jitter >= self.args.spike_threshold:
            checks.append(("Max jitter above threshold", True,
                          f"{max_jitter:.1f}ms >= {self.args.spike_threshold}ms"))
        else:
            checks.append(("Max jitter above threshold", False,
                          f"{max_jitter:.1f}ms < {self.args.spike_threshold}ms threshold"))
            passed = False

        # Report checks
        logging.info("Self-check results:")
        for name, result, detail in checks:
            status = "PASS" if result else "FAIL"
            logging.info(f"  [{status}] {name}: {detail}")

        logging.info("")
        logging.info("Observations in JitterTrap should show:")
        logging.info(f"  - Jitter histogram with outliers >{self.args.spike_threshold}ms")
        logging.info("  - Periodic IPG spikes matching sender interval")
        logging.info("  - RFC 3550 jitter metric elevated during spikes")
        logging.info("")

        return 0 if passed else 1

    def cleanup(self) -> None:
        """Clean up resources."""
        if self.sock:
            self.sock.close()
        logging.info("Receiver shutdown complete")


def main() -> int:
    args = setup_argparse()
    setup_logging(verbose=args.verbose, quiet=args.quiet)

    try:
        receiver = JitterSpikeReceiver(args)
        return receiver.run()
    except Exception as e:
        logging.error(f"Error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
