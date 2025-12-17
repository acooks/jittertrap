#!/usr/bin/env python3
"""
RTP Sequence Gap Sender - Demonstrates packet loss via sequence discontinuities

Sends RTP packets with intentional sequence number gaps to simulate packet loss.
The receiver detects these gaps and counts missing packets.

Part of Pathological Porcupines: Network Application Failure Simulations
"""

import argparse
import logging
import random
import socket
import sys
import time
from pathlib import Path

# Add common utilities path
sys.path.insert(0, str(Path(__file__).parent.parent.parent))
from common.network import create_udp_socket
from common.protocol import RTPPacket
from common.logging_utils import setup_logging, format_bytes


# Default configuration
DEFAULT_HOST = "10.0.1.2"
DEFAULT_PORT = 5004  # Standard RTP port for JitterTrap detection
DEFAULT_DURATION = 15          # seconds
DEFAULT_FRAME_RATE = 30        # fps
DEFAULT_GAP_INTERVAL = 2.0     # seconds between gap events
DEFAULT_GAP_SIZE_MIN = 1       # minimum packets to skip
DEFAULT_GAP_SIZE_MAX = 3       # maximum packets to skip
DEFAULT_PAYLOAD_SIZE = 1200    # bytes


def setup_argparse() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="RTP Sequence Gap Sender - simulates packet loss via sequence gaps",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Default: skip 1-3 packets every 2s (uses port 5004 for JitterTrap RTP detection)
    python sender.py --host 10.0.1.2

    # More frequent gaps
    python sender.py --host 10.0.1.2 --gap-interval 1.0

    # Larger gaps (simulate burst loss)
    python sender.py --host 10.0.1.2 --gap-min 5 --gap-max 10

JitterTrap observation:
    - seq_loss counter increments
    - Gap events visible in flow details
    - Loss rate calculated from gap size/interval
        """
    )
    parser.add_argument("--host", "-H", default=DEFAULT_HOST,
                        help=f"Destination address (default: {DEFAULT_HOST})")
    parser.add_argument("--port", "-p", type=int, default=DEFAULT_PORT,
                        help=f"Destination port (default: {DEFAULT_PORT})")
    parser.add_argument("--duration", "-d", type=float, default=DEFAULT_DURATION,
                        help=f"Test duration in seconds (default: {DEFAULT_DURATION})")
    parser.add_argument("--frame-rate", type=float, default=DEFAULT_FRAME_RATE,
                        help=f"Frames per second (default: {DEFAULT_FRAME_RATE})")
    parser.add_argument("--gap-interval", type=float, default=DEFAULT_GAP_INTERVAL,
                        help=f"Seconds between gap events (default: {DEFAULT_GAP_INTERVAL})")
    parser.add_argument("--gap-min", type=int, default=DEFAULT_GAP_SIZE_MIN,
                        help=f"Minimum gap size in packets (default: {DEFAULT_GAP_SIZE_MIN})")
    parser.add_argument("--gap-max", type=int, default=DEFAULT_GAP_SIZE_MAX,
                        help=f"Maximum gap size in packets (default: {DEFAULT_GAP_SIZE_MAX})")
    parser.add_argument("--payload-size", type=int, default=DEFAULT_PAYLOAD_SIZE,
                        help=f"RTP payload size in bytes (default: {DEFAULT_PAYLOAD_SIZE})")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    parser.add_argument("--quiet", "-q", action="store_true",
                        help="Quiet mode (errors only)")
    return parser.parse_args()


class SequenceGapSender:
    """
    RTP sender that creates intentional sequence number gaps.

    Simulates packet loss by skipping sequence numbers, which the
    receiver can detect as missing packets.
    """

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.sock = None
        self.rtp = None
        self.total_packets = 0
        self.total_bytes = 0
        self.gap_events = []  # (time, gap_size) of created gaps
        self.start_time = None

    def setup(self) -> None:
        """Initialize UDP socket and RTP state."""
        self.sock = create_udp_socket()
        self.rtp = RTPPacket(
            payload_type=RTPPacket.PT_H264,
            clock_rate=RTPPacket.CLOCK_RATE_VIDEO,
        )

        logging.info(f"Sending RTP to {self.args.host}:{self.args.port}")
        logging.info(f"Frame rate: {self.args.frame_rate} fps")
        logging.info(f"Sequence gaps: {self.args.gap_min}-{self.args.gap_max} packets every {self.args.gap_interval}s")

    def run(self) -> int:
        """Main execution. Returns exit code."""
        self.setup()
        self.start_time = time.monotonic()
        end_time = self.start_time + self.args.duration

        frame_interval_sec = 1.0 / self.args.frame_rate
        next_frame_time = self.start_time
        next_gap_time = self.start_time + self.args.gap_interval
        last_status_time = self.start_time

        dest = (self.args.host, self.args.port)
        payload = b'\x00' * self.args.payload_size

        logging.info(f"Starting RTP transmission for {self.args.duration}s...")

        try:
            while True:
                now = time.monotonic()
                if now >= end_time:
                    break

                # Check if it's time for a sequence gap
                if now >= next_gap_time:
                    gap_size = random.randint(self.args.gap_min, self.args.gap_max)
                    elapsed = now - self.start_time
                    self.gap_events.append((elapsed, gap_size))
                    logging.info(f"CREATING SEQUENCE GAP: skipping {gap_size} sequence number(s)")
                    self.rtp.skip_sequence(gap_size)
                    next_gap_time = now + self.args.gap_interval

                # Wait for next frame time
                if now < next_frame_time:
                    time.sleep(next_frame_time - now)
                    now = time.monotonic()

                # Calculate timestamp
                frame_num = int((now - self.start_time) * self.args.frame_rate)
                timestamp = int(frame_num * (RTPPacket.CLOCK_RATE_VIDEO / self.args.frame_rate))

                # Build and send packet
                packet = self.rtp.build(payload, timestamp=timestamp, marker=True)

                try:
                    self.sock.sendto(packet, dest)
                    self.total_packets += 1
                    self.total_bytes += len(packet)
                except OSError as e:
                    logging.warning(f"Send failed: {e}")

                next_frame_time += frame_interval_sec

                # Periodic status
                if now - last_status_time >= 2.0:
                    elapsed = now - self.start_time
                    fps = self.total_packets / elapsed
                    total_gaps = sum(g[1] for g in self.gap_events)
                    logging.info(f"Elapsed: {elapsed:.1f}s, Packets: {self.total_packets} ({fps:.1f} fps), "
                                f"Gap events: {len(self.gap_events)}, Total skipped: {total_gaps}")
                    last_status_time = now

        except KeyboardInterrupt:
            logging.info("Interrupted by user")
        finally:
            self.cleanup()

        return self.report_results()

    def report_results(self) -> int:
        """Report test results. Returns exit code."""
        elapsed = time.monotonic() - self.start_time
        fps = self.total_packets / elapsed if elapsed > 0 else 0
        total_gaps = sum(g[1] for g in self.gap_events)

        logging.info("")
        logging.info("=" * 50)
        logging.info("RTP SEQUENCE GAP SENDER RESULTS")
        logging.info("=" * 50)
        logging.info(f"Duration: {elapsed:.1f}s")
        logging.info(f"Packets sent: {self.total_packets}")
        logging.info(f"Total data: {format_bytes(self.total_bytes)}")
        logging.info(f"Average frame rate: {fps:.1f} fps")
        logging.info("")
        logging.info(f"Gap events created: {len(self.gap_events)}")
        logging.info(f"Total packets skipped: {total_gaps}")

        if self.gap_events:
            logging.info("Gap details:")
            for i, (t, size) in enumerate(self.gap_events):
                logging.info(f"  Gap {i+1}: {size} packet(s) at {t:.1f}s")

        logging.info("")

        # Calculate expected loss rate
        if self.total_packets + total_gaps > 0:
            loss_rate = total_gaps / (self.total_packets + total_gaps) * 100
            logging.info(f"Simulated loss rate: {loss_rate:.2f}%")
        logging.info("")

        # Self-checks
        passed = True
        checks = []

        # Check 1: Did we create expected number of gap events?
        expected_events = int(self.args.duration / self.args.gap_interval)
        if len(self.gap_events) >= expected_events - 1:
            checks.append(("Gap events", True,
                          f"{len(self.gap_events)} events (expected ~{expected_events})"))
        else:
            checks.append(("Gap events", False,
                          f"Only {len(self.gap_events)}, expected ~{expected_events}"))
            passed = False

        # Check 2: Did we skip packets?
        if total_gaps >= len(self.gap_events):
            checks.append(("Packets skipped", True, f"{total_gaps} packets skipped"))
        else:
            checks.append(("Packets skipped", False, f"Only {total_gaps} packets skipped"))
            passed = False

        # Report checks
        logging.info("Self-check results:")
        for name, result, detail in checks:
            status = "PASS" if result else "FAIL"
            logging.info(f"  [{status}] {name}: {detail}")

        logging.info("")
        logging.info("Expected observations in JitterTrap:")
        logging.info("  - seq_loss counter increments on each gap event")
        logging.info(f"  - Loss events every ~{self.args.gap_interval}s")
        logging.info("  - Sequence discontinuity visible in flow details")
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
        sender = SequenceGapSender(args)
        return sender.run()
    except Exception as e:
        logging.error(f"Error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
