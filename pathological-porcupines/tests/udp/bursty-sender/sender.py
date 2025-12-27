#!/usr/bin/env python3
"""
Bursty UDP Sender - Demonstrates bimodal inter-packet gap patterns

Sends packets in bursts with tight spacing, followed by longer gaps.
Creates a distinctive bimodal IPG histogram visible in JitterTrap.

Part of Pathological Porcupines: Network Application Failure Simulations
"""

import argparse
import logging
import socket
import struct
import sys
import time
from pathlib import Path

# Add common utilities path
sys.path.insert(0, str(Path(__file__).parent.parent.parent.parent))
from common.network import create_udp_socket
from common.timing import BurstTimer
from common.logging_utils import setup_logging, format_bytes


# Default configuration
DEFAULT_HOST = "10.0.1.2"
DEFAULT_PORT = 9999
DEFAULT_DURATION = 10         # seconds
DEFAULT_BURST_SIZE = 10       # packets per burst
DEFAULT_BURST_INTERVAL = 100  # ms between burst starts
DEFAULT_PACKET_INTERVAL = 1   # ms between packets in burst
DEFAULT_PAYLOAD_SIZE = 512    # bytes


def setup_argparse() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Bursty UDP Sender - creates bimodal IPG histogram",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Default: 10 packets/burst, 100ms between bursts, 1ms in burst
    python sender.py --host 10.0.1.2 --port 9999

    # More aggressive bursts
    python sender.py --burst-size 20 --burst-interval 200 --packet-interval 0.5

    # Wider gap between bursts (more visible bimodal)
    python sender.py --burst-interval 200 --packet-interval 1

JitterTrap observation:
    - IPG histogram shows two distinct peaks
    - One peak at ~1ms (within-burst interval)
    - One peak at ~90ms (between-burst gap)
        """
    )
    parser.add_argument("--host", "-H", default=DEFAULT_HOST,
                        help=f"Destination address (default: {DEFAULT_HOST})")
    parser.add_argument("--port", "-p", type=int, default=DEFAULT_PORT,
                        help=f"Destination port (default: {DEFAULT_PORT})")
    parser.add_argument("--duration", "-d", type=float, default=DEFAULT_DURATION,
                        help=f"Test duration in seconds (default: {DEFAULT_DURATION})")
    parser.add_argument("--burst-size", type=int, default=DEFAULT_BURST_SIZE,
                        help=f"Packets per burst (default: {DEFAULT_BURST_SIZE})")
    parser.add_argument("--burst-interval", type=float, default=DEFAULT_BURST_INTERVAL,
                        help=f"Milliseconds between burst starts (default: {DEFAULT_BURST_INTERVAL})")
    parser.add_argument("--packet-interval", type=float, default=DEFAULT_PACKET_INTERVAL,
                        help=f"Milliseconds between packets in burst (default: {DEFAULT_PACKET_INTERVAL})")
    parser.add_argument("--payload-size", type=int, default=DEFAULT_PAYLOAD_SIZE,
                        help=f"Payload size in bytes (default: {DEFAULT_PAYLOAD_SIZE})")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    parser.add_argument("--quiet", "-q", action="store_true",
                        help="Quiet mode (errors only)")
    return parser.parse_args()


class BurstySender:
    """
    UDP sender that generates bursty traffic patterns.

    Creates bimodal inter-packet gap distribution:
    - Within burst: tight spacing (configurable, default 1ms)
    - Between bursts: longer gap (burst_interval - burst_duration)
    """

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.sock = None
        self.total_packets = 0
        self.total_bytes = 0
        self.burst_count = 0
        self.start_time = None

    def setup(self) -> None:
        """Initialize UDP socket."""
        self.sock = create_udp_socket()
        logging.info(f"Sending bursts to {self.args.host}:{self.args.port}")
        logging.info(f"Burst pattern: {self.args.burst_size} packets @ "
                     f"{self.args.packet_interval}ms, every {self.args.burst_interval}ms")

        # Calculate expected gaps for logging
        burst_duration = self.args.burst_size * self.args.packet_interval
        inter_burst_gap = self.args.burst_interval - burst_duration
        logging.info(f"Expected IPG histogram peaks:")
        logging.info(f"  - Within burst: ~{self.args.packet_interval}ms")
        logging.info(f"  - Between bursts: ~{max(0, inter_burst_gap):.1f}ms")

    def create_packet(self, seq: int, burst_num: int, packet_in_burst: int) -> bytes:
        """Create a UDP packet with header and payload."""
        # Header: 4-byte seq, 4-byte burst, 4-byte pos, 8-byte timestamp
        timestamp_ns = time.monotonic_ns()
        header = struct.pack(">IIIQ", seq, burst_num, packet_in_burst, timestamp_ns)

        # Pad to desired size
        padding_size = max(0, self.args.payload_size - len(header))
        payload = header + (b'\x00' * padding_size)

        return payload

    def run(self) -> int:
        """Main execution. Returns exit code."""
        self.setup()
        self.start_time = time.monotonic()
        end_time = self.start_time + self.args.duration
        last_burst_log = self.start_time

        # Convert packet interval from ms to us for BurstTimer
        packet_interval_us = self.args.packet_interval * 1000

        timer = BurstTimer(
            burst_size=self.args.burst_size,
            burst_interval_ms=self.args.burst_interval,
            packet_interval_us=packet_interval_us,
        )

        dest = (self.args.host, self.args.port)

        logging.info(f"Starting burst transmission for {self.args.duration}s...")

        try:
            for packet_num in timer:
                now = time.monotonic()
                if now >= end_time:
                    break

                # Determine burst info
                burst_num = packet_num // self.args.burst_size
                packet_in_burst = packet_num % self.args.burst_size

                # Create and send packet
                packet = self.create_packet(packet_num, burst_num, packet_in_burst)

                try:
                    self.sock.sendto(packet, dest)
                    self.total_packets += 1
                    self.total_bytes += len(packet)
                except OSError as e:
                    logging.warning(f"Send failed: {e}")
                    continue

                # Track bursts
                if packet_in_burst == 0:
                    self.burst_count += 1

                # Periodic status
                if now - last_burst_log >= 2.0:
                    elapsed = now - self.start_time
                    pps = self.total_packets / elapsed
                    logging.info(f"Elapsed: {elapsed:.1f}s, Bursts: {self.burst_count}, "
                                 f"Packets: {self.total_packets} ({pps:.0f}/s)")
                    last_burst_log = now

        except KeyboardInterrupt:
            logging.info("Interrupted by user")
        finally:
            self.cleanup()

        return self.report_results()

    def report_results(self) -> int:
        """Report test results. Returns exit code."""
        elapsed = time.monotonic() - self.start_time
        pps = self.total_packets / elapsed if elapsed > 0 else 0
        bps = self.total_bytes / elapsed if elapsed > 0 else 0

        logging.info("")
        logging.info("=" * 50)
        logging.info("BURSTY SENDER TEST RESULTS")
        logging.info("=" * 50)
        logging.info(f"Duration: {elapsed:.1f}s")
        logging.info(f"Total packets: {self.total_packets}")
        logging.info(f"Total data: {format_bytes(self.total_bytes)}")
        logging.info(f"Bursts sent: {self.burst_count}")
        logging.info(f"Average rate: {pps:.1f} packets/s, {format_bytes(int(bps))}/s")
        logging.info("")

        # Self-checks
        passed = True
        checks = []

        # Check 1: Did we send enough packets?
        expected_bursts = self.args.duration * 1000 / self.args.burst_interval
        expected_packets = expected_bursts * self.args.burst_size
        if self.total_packets >= expected_packets * 0.8:
            checks.append(("Packet count", True,
                          f"{self.total_packets} packets (expected ~{expected_packets:.0f})"))
        else:
            checks.append(("Packet count", False,
                          f"Only {self.total_packets}, expected ~{expected_packets:.0f}"))
            passed = False

        # Check 2: Did we complete multiple bursts?
        if self.burst_count >= 5:
            checks.append(("Burst count", True, f"{self.burst_count} bursts"))
        else:
            checks.append(("Burst count", False, f"Only {self.burst_count} bursts"))
            passed = False

        # Check 3: Rate reasonable?
        expected_pps = expected_packets / self.args.duration
        if pps >= expected_pps * 0.7:
            checks.append(("Packet rate", True, f"{pps:.1f} pps"))
        else:
            checks.append(("Packet rate", False,
                          f"{pps:.1f} pps, expected ~{expected_pps:.1f}"))

        # Report checks
        logging.info("Self-check results:")
        for name, result, detail in checks:
            status = "PASS" if result else "FAIL"
            logging.info(f"  [{status}] {name}: {detail}")

        logging.info("")
        logging.info("Expected observations in JitterTrap:")
        logging.info("  - IPG histogram shows bimodal distribution")
        logging.info(f"  - Peak 1: ~{self.args.packet_interval}ms (within burst)")
        burst_gap = self.args.burst_interval - (self.args.burst_size * self.args.packet_interval)
        logging.info(f"  - Peak 2: ~{max(0, burst_gap):.0f}ms (between bursts)")
        logging.info("")

        return 0 if passed else 1

    def cleanup(self) -> None:
        """Clean up resources."""
        if self.sock:
            self.sock.close()
        logging.info("Sender shutdown complete")


def main() -> int:
    args = setup_argparse()
    setup_logging(verbose=args.verbose, quiet=args.quiet)

    try:
        sender = BurstySender(args)
        return sender.run()
    except Exception as e:
        logging.error(f"Error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
