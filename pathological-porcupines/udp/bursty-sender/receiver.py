#!/usr/bin/env python3
"""
Bursty UDP Receiver - Measures inter-packet gaps and builds histogram

Receives packets from the bursty sender and calculates IPG distribution
to verify bimodal pattern.

Part of Pathological Porcupines: Network Application Failure Simulations
"""

import argparse
import logging
import socket
import struct
import sys
import time
from collections import defaultdict
from pathlib import Path

# Add common utilities path
sys.path.insert(0, str(Path(__file__).parent.parent.parent))
from common.network import create_udp_socket
from common.logging_utils import setup_logging, format_bytes


# Default configuration
DEFAULT_PORT = 9999
DEFAULT_DURATION = 15  # slightly longer than sender to catch all packets


def setup_argparse() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Bursty UDP Receiver - measures IPG distribution",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Basic receiver
    python receiver.py --port 9999

    # Longer duration
    python receiver.py --port 9999 --duration 20

Expected output:
    - IPG histogram showing bimodal distribution
    - Self-check verifies peaks at expected intervals
        """
    )
    parser.add_argument("--port", "-p", type=int, default=DEFAULT_PORT,
                        help=f"Listen port (default: {DEFAULT_PORT})")
    parser.add_argument("--duration", "-d", type=float, default=DEFAULT_DURATION,
                        help=f"Receive duration in seconds (default: {DEFAULT_DURATION})")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    parser.add_argument("--quiet", "-q", action="store_true",
                        help="Quiet mode (errors only)")
    return parser.parse_args()


class BurstyReceiver:
    """
    UDP receiver that measures inter-packet gap distribution.

    Builds histogram of IPG values to detect bimodal patterns from bursty traffic.
    """

    # Histogram bucket boundaries in milliseconds
    BUCKET_EDGES = [0, 2, 5, 10, 20, 50, 100, 200, 500, 1000]

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.sock = None
        self.total_packets = 0
        self.total_bytes = 0
        self.last_recv_time = None
        self.ipg_histogram = defaultdict(int)  # bucket_ms -> count
        self.ipg_values = []  # Raw IPG values for analysis
        self.start_time = None
        self.out_of_order = 0
        self.last_seq = -1

    def get_bucket(self, ipg_ms: float) -> str:
        """Map IPG value to histogram bucket."""
        for i, edge in enumerate(self.BUCKET_EDGES[1:], 1):
            if ipg_ms < edge:
                return f"<{edge}ms"
        return f">={self.BUCKET_EDGES[-1]}ms"

    def setup(self) -> None:
        """Initialize UDP socket."""
        self.sock = create_udp_socket(timeout=2.0)
        self.sock.bind(('0.0.0.0', self.args.port))
        logging.info(f"Receiver listening on port {self.args.port}")
        logging.info(f"Will receive for {self.args.duration}s")

    def parse_packet(self, data: bytes) -> dict:
        """Parse packet header."""
        if len(data) >= 20:
            seq, burst, pos, timestamp = struct.unpack(">IIIQ", data[:20])
            return {
                'seq': seq,
                'burst': burst,
                'position': pos,
                'timestamp_ns': timestamp,
            }
        return None

    def run(self) -> int:
        """Main execution. Returns exit code."""
        self.setup()
        self.start_time = time.monotonic()
        end_time = self.start_time + self.args.duration
        last_status_time = self.start_time

        logging.info("Waiting for packets...")

        try:
            while time.monotonic() < end_time:
                try:
                    data, addr = self.sock.recvfrom(65536)
                except socket.timeout:
                    if self.total_packets > 0:
                        # Had packets, now idle - might be done
                        logging.debug("Receive timeout, may be end of transmission")
                    continue

                now = time.monotonic()

                # Calculate IPG if not first packet
                if self.last_recv_time is not None:
                    ipg_ms = (now - self.last_recv_time) * 1000
                    self.ipg_values.append(ipg_ms)
                    bucket = self.get_bucket(ipg_ms)
                    self.ipg_histogram[bucket] += 1

                self.last_recv_time = now
                self.total_packets += 1
                self.total_bytes += len(data)

                # Parse and check sequence
                pkt = self.parse_packet(data)
                if pkt:
                    if pkt['seq'] < self.last_seq:
                        self.out_of_order += 1
                    self.last_seq = pkt['seq']

                # Periodic status
                if now - last_status_time >= 2.0:
                    elapsed = now - self.start_time
                    pps = self.total_packets / elapsed
                    logging.info(f"Elapsed: {elapsed:.1f}s, "
                                f"Packets: {self.total_packets} ({pps:.0f}/s)")
                    last_status_time = now

        except KeyboardInterrupt:
            logging.info("Interrupted by user")
        finally:
            self.cleanup()

        return self.report_results()

    def report_results(self) -> int:
        """Report test results and IPG histogram. Returns exit code."""
        elapsed = time.monotonic() - self.start_time

        logging.info("")
        logging.info("=" * 50)
        logging.info("BURSTY RECEIVER TEST RESULTS")
        logging.info("=" * 50)
        logging.info(f"Duration: {elapsed:.1f}s")
        logging.info(f"Total packets: {self.total_packets}")
        logging.info(f"Total data: {format_bytes(self.total_bytes)}")
        if self.out_of_order > 0:
            logging.info(f"Out of order: {self.out_of_order}")
        logging.info("")

        if not self.ipg_values:
            logging.error("No IPG data collected - no packets received?")
            return 1

        # IPG statistics
        ipg_sorted = sorted(self.ipg_values)
        logging.info("IPG Statistics:")
        logging.info(f"  Min: {min(self.ipg_values):.2f}ms")
        logging.info(f"  Max: {max(self.ipg_values):.2f}ms")
        logging.info(f"  Median: {ipg_sorted[len(ipg_sorted)//2]:.2f}ms")
        avg_ipg = sum(self.ipg_values) / len(self.ipg_values)
        logging.info(f"  Mean: {avg_ipg:.2f}ms")
        logging.info("")

        # IPG Histogram
        logging.info("IPG Histogram:")
        total_gaps = sum(self.ipg_histogram.values())

        # Sort buckets properly
        bucket_order = [f"<{e}ms" for e in self.BUCKET_EDGES[1:]]
        bucket_order.append(f">={self.BUCKET_EDGES[-1]}ms")

        for bucket in bucket_order:
            count = self.ipg_histogram.get(bucket, 0)
            pct = (count / total_gaps * 100) if total_gaps > 0 else 0
            bar = '#' * int(pct / 2)
            if count > 0:
                logging.info(f"  {bucket:>10s}: {count:5d} ({pct:5.1f}%) {bar}")

        logging.info("")

        # Analyze bimodal distribution
        passed = True
        checks = []

        # Check 1: Did we receive enough packets?
        if self.total_packets >= 50:
            checks.append(("Packet count", True, f"{self.total_packets} packets"))
        else:
            checks.append(("Packet count", False,
                          f"Only {self.total_packets} packets received"))
            passed = False

        # Check 2: Bimodal distribution - look for two dominant buckets
        # Count packets in "small IPG" (<10ms) vs "large IPG" (>20ms)
        small_ipg = sum(1 for v in self.ipg_values if v < 10)
        large_ipg = sum(1 for v in self.ipg_values if v > 20)
        middle_ipg = len(self.ipg_values) - small_ipg - large_ipg

        small_pct = small_ipg / len(self.ipg_values) * 100
        large_pct = large_ipg / len(self.ipg_values) * 100
        bimodal_pct = small_pct + large_pct

        if bimodal_pct >= 70:
            checks.append(("Bimodal distribution", True,
                          f"{bimodal_pct:.1f}% in <10ms or >20ms buckets"))
        else:
            checks.append(("Bimodal distribution", False,
                          f"Only {bimodal_pct:.1f}% in bimodal buckets (need 70%)"))
            passed = False

        # Check 3: Two clear peaks
        if small_ipg > 10 and large_ipg > 5:
            checks.append(("Dual peaks", True,
                          f"Small IPG: {small_ipg}, Large IPG: {large_ipg}"))
        else:
            checks.append(("Dual peaks", False,
                          f"Small: {small_ipg}, Large: {large_ipg} (need both)"))

        # Report checks
        logging.info("Self-check results:")
        for name, result, detail in checks:
            status = "PASS" if result else "FAIL"
            logging.info(f"  [{status}] {name}: {detail}")

        logging.info("")
        logging.info("IPG Distribution Summary:")
        logging.info(f"  Small IPG (<10ms): {small_ipg} ({small_pct:.1f}%) - within burst")
        logging.info(f"  Medium IPG (10-20ms): {middle_ipg} - transition")
        logging.info(f"  Large IPG (>20ms): {large_ipg} ({large_pct:.1f}%) - between bursts")
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
        receiver = BurstyReceiver(args)
        return receiver.run()
    except Exception as e:
        logging.error(f"Error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
