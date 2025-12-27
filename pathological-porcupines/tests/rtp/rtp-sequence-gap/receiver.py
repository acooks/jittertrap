#!/usr/bin/env python3
"""
RTP Sequence Gap Receiver - Detects packet loss via sequence discontinuities

Receives RTP packets and detects sequence number gaps, counting missing packets
and tracking loss events.

Part of Pathological Porcupines: Network Application Failure Simulations
"""

import argparse
import logging
import socket
import sys
import time
from pathlib import Path

# Add common utilities path
sys.path.insert(0, str(Path(__file__).parent.parent.parent.parent))
from common.network import create_udp_socket
from common.protocol import parse_rtp_header
from common.logging_utils import setup_logging, format_bytes


# Default configuration
DEFAULT_PORT = 5004  # Standard RTP port for JitterTrap detection
DEFAULT_DURATION = 20  # slightly longer than sender


def setup_argparse() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="RTP Sequence Gap Receiver - detects packet loss",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Basic receiver (uses default port 5004 for JitterTrap RTP detection)
    python receiver.py

    # Longer duration
    python receiver.py --duration 30

Expected output:
    - Gap events detected with count of missing packets
    - Total loss count and loss rate
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


class SequenceGapReceiver:
    """
    RTP receiver that detects sequence number gaps (packet loss).

    Tracks sequence numbers and reports any discontinuities as loss events.
    """

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.sock = None
        self.total_packets = 0
        self.total_bytes = 0
        self.start_time = None

        # Sequence tracking
        self.last_seq = None
        self.gap_events = []  # (time, expected_seq, received_seq, gap_size)
        self.total_lost = 0
        self.out_of_order = 0
        self.duplicates = 0
        self.ssrc = None

        # For sequence history (detect reordering vs loss)
        self.seq_history = set()
        self.history_size = 1000

    def setup(self) -> None:
        """Initialize UDP socket."""
        self.sock = create_udp_socket(timeout=2.0)
        self.sock.bind(('0.0.0.0', self.args.port))
        logging.info(f"Receiver listening on port {self.args.port}")
        logging.info(f"Will receive for {self.args.duration}s")

    def check_sequence(self, seq: int, arrival_time: float) -> None:
        """Check sequence number for gaps, reordering, or duplicates."""
        elapsed = arrival_time - self.start_time

        # Check for duplicate
        if seq in self.seq_history:
            self.duplicates += 1
            logging.debug(f"Duplicate packet: seq={seq}")
            return

        # Add to history (maintain fixed size)
        self.seq_history.add(seq)
        if len(self.seq_history) > self.history_size:
            # Remove oldest (approximation)
            oldest = min(self.seq_history)
            self.seq_history.discard(oldest)

        if self.last_seq is None:
            # First packet
            self.last_seq = seq
            return

        # Calculate expected sequence (with wraparound)
        expected_seq = (self.last_seq + 1) & 0xFFFF

        if seq == expected_seq:
            # Normal case - in order
            self.last_seq = seq
            return

        # Check for reordering vs gap
        # If seq < expected but within a small window, it's likely reordering
        seq_diff = (seq - expected_seq) & 0xFFFF

        if seq_diff > 0x8000:
            # Negative difference (with wraparound) - late/reordered packet
            self.out_of_order += 1
            logging.debug(f"Out of order packet: expected {expected_seq}, got {seq}")
            return

        if seq_diff > 0 and seq_diff < 100:
            # Forward gap - packet loss detected
            gap_size = seq_diff
            self.gap_events.append((elapsed, expected_seq, seq, gap_size))
            self.total_lost += gap_size
            logging.info(f"SEQUENCE GAP detected: expected {expected_seq}, got {seq} "
                        f"({gap_size} packet(s) lost) at {elapsed:.1f}s")
            self.last_seq = seq
        elif seq_diff >= 100:
            # Large jump - could be wraparound issue or major loss
            logging.warning(f"Large sequence jump: {self.last_seq} -> {seq}")
            self.last_seq = seq

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
                self.check_sequence(seq, arrival_time)

                # Periodic status
                now = time.monotonic()
                if now - last_status_time >= 2.0:
                    elapsed = now - self.start_time
                    pps = self.total_packets / elapsed
                    logging.info(f"Elapsed: {elapsed:.1f}s, Packets: {self.total_packets} ({pps:.1f}/s), "
                                f"Lost: {self.total_lost}, Gap events: {len(self.gap_events)}")
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
        logging.info("RTP SEQUENCE GAP RECEIVER RESULTS")
        logging.info("=" * 50)
        logging.info(f"Duration: {elapsed:.1f}s")
        logging.info(f"Packets received: {self.total_packets}")
        logging.info(f"Total data: {format_bytes(self.total_bytes)}")
        logging.info("")

        # Loss statistics
        logging.info("Packet Loss Statistics:")
        logging.info(f"  Gap events detected: {len(self.gap_events)}")
        logging.info(f"  Total packets lost: {self.total_lost}")
        logging.info(f"  Out of order packets: {self.out_of_order}")
        logging.info(f"  Duplicate packets: {self.duplicates}")

        if self.total_packets + self.total_lost > 0:
            total_expected = self.total_packets + self.total_lost
            loss_rate = self.total_lost / total_expected * 100
            logging.info(f"  Loss rate: {loss_rate:.2f}%")
        logging.info("")

        # Gap event details
        if self.gap_events:
            logging.info("Gap Event Details:")
            for i, (t, expected, got, size) in enumerate(self.gap_events):
                logging.info(f"  Gap {i+1}: at {t:.1f}s, seq {expected}->{got}, {size} packet(s) lost")

            if len(self.gap_events) >= 2:
                intervals = []
                for i in range(1, len(self.gap_events)):
                    intervals.append(self.gap_events[i][0] - self.gap_events[i-1][0])
                avg_interval = sum(intervals) / len(intervals)
                logging.info(f"  Average gap interval: {avg_interval:.1f}s")
            logging.info("")

        # Self-checks
        passed = True
        checks = []

        # Check 1: Did we receive packets?
        if self.total_packets >= 100:
            checks.append(("Packet count", True, f"{self.total_packets} packets"))
        else:
            checks.append(("Packet count", False,
                          f"Only {self.total_packets} packets"))
            passed = False

        # Check 2: Did we detect gap events?
        if len(self.gap_events) >= 3:
            checks.append(("Gap events detected", True,
                          f"{len(self.gap_events)} events"))
        else:
            checks.append(("Gap events detected", False,
                          f"Only {len(self.gap_events)} events (expected ≥3)"))
            passed = False

        # Check 3: Did we count lost packets?
        if self.total_lost >= len(self.gap_events):
            checks.append(("Lost packet count", True,
                          f"{self.total_lost} packets lost"))
        else:
            checks.append(("Lost packet count", False,
                          f"Only {self.total_lost} lost"))

        # Report checks
        logging.info("Self-check results:")
        for name, result, detail in checks:
            status = "PASS" if result else "FAIL"
            logging.info(f"  [{status}] {name}: {detail}")

        logging.info("")
        logging.info("Observations in JitterTrap should show:")
        logging.info("  - seq_loss counter matching our total")
        logging.info("  - Loss events in flow details")
        logging.info(f"  - ~{len(self.gap_events)} loss events during test")
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
        receiver = SequenceGapReceiver(args)
        return receiver.run()
    except Exception as e:
        logging.error(f"Error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
