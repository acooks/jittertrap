#!/usr/bin/env python3
"""
RTP Jitter Spike Sender - Demonstrates periodic jitter spikes in RTP stream

Sends RTP packets at regular intervals with periodic large delays injected,
simulating network jitter spikes or processing delays.

Part of Pathological Porcupines: Network Application Failure Simulations
"""

import argparse
import logging
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
DEFAULT_PORT = 9999
DEFAULT_DURATION = 15          # seconds
DEFAULT_FRAME_RATE = 30        # fps (33.33ms interval)
DEFAULT_SPIKE_INTERVAL = 3.0   # seconds between jitter spikes
DEFAULT_SPIKE_DELAY = 200      # ms of additional delay during spike
DEFAULT_PAYLOAD_SIZE = 1200    # bytes (typical video packet)


def setup_argparse() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="RTP Jitter Spike Sender - injects periodic delay spikes",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Default: 30fps with 200ms spike every 3s
    python sender.py --host 10.0.1.2 --port 9999

    # More frequent spikes
    python sender.py --spike-interval 2.0 --spike-delay 150

    # Larger spikes (severe)
    python sender.py --spike-delay 500 --spike-interval 5.0

JitterTrap observation:
    - Jitter histogram shows outliers >100ms
    - IPG shows periodic spikes
    - Throughput dips during spike recovery
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
    parser.add_argument("--spike-interval", type=float, default=DEFAULT_SPIKE_INTERVAL,
                        help=f"Seconds between jitter spikes (default: {DEFAULT_SPIKE_INTERVAL})")
    parser.add_argument("--spike-delay", type=float, default=DEFAULT_SPIKE_DELAY,
                        help=f"Additional delay in ms during spike (default: {DEFAULT_SPIKE_DELAY})")
    parser.add_argument("--payload-size", type=int, default=DEFAULT_PAYLOAD_SIZE,
                        help=f"RTP payload size in bytes (default: {DEFAULT_PAYLOAD_SIZE})")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    parser.add_argument("--quiet", "-q", action="store_true",
                        help="Quiet mode (errors only)")
    return parser.parse_args()


class JitterSpikeSender:
    """
    RTP sender that injects periodic jitter spikes.

    Simulates network congestion, processing delays, or GC pauses
    that cause periodic large jitter in media streams.
    """

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.sock = None
        self.rtp = None
        self.total_packets = 0
        self.total_bytes = 0
        self.spike_count = 0
        self.start_time = None

    def setup(self) -> None:
        """Initialize UDP socket and RTP state."""
        self.sock = create_udp_socket()
        self.rtp = RTPPacket(
            payload_type=RTPPacket.PT_H264,
            clock_rate=RTPPacket.CLOCK_RATE_VIDEO,
        )

        frame_interval = 1000 / self.args.frame_rate
        logging.info(f"Sending RTP to {self.args.host}:{self.args.port}")
        logging.info(f"Frame rate: {self.args.frame_rate} fps ({frame_interval:.1f}ms interval)")
        logging.info(f"Jitter spikes: {self.args.spike_delay}ms delay every {self.args.spike_interval}s")

    def run(self) -> int:
        """Main execution. Returns exit code."""
        self.setup()
        self.start_time = time.monotonic()
        end_time = self.start_time + self.args.duration

        frame_interval_sec = 1.0 / self.args.frame_rate
        next_frame_time = self.start_time
        next_spike_time = self.start_time + self.args.spike_interval
        last_status_time = self.start_time

        dest = (self.args.host, self.args.port)
        payload = b'\x00' * self.args.payload_size

        logging.info(f"Starting RTP transmission for {self.args.duration}s...")

        try:
            while True:
                now = time.monotonic()
                if now >= end_time:
                    break

                # Check if it's time for a jitter spike
                if now >= next_spike_time:
                    spike_delay_sec = self.args.spike_delay / 1000.0
                    logging.info(f"INJECTING JITTER SPIKE: {self.args.spike_delay}ms delay")
                    time.sleep(spike_delay_sec)
                    self.spike_count += 1
                    next_spike_time = now + spike_delay_sec + self.args.spike_interval
                    # Recalculate current time after delay
                    now = time.monotonic()

                # Wait for next frame time if needed
                if now < next_frame_time:
                    time.sleep(next_frame_time - now)
                    now = time.monotonic()

                # Send RTP packet
                # Calculate timestamp based on frame number (not wall clock)
                # This way timestamp advances steadily while arrival time has jitter
                frame_num = int((now - self.start_time) * self.args.frame_rate)
                timestamp = int(frame_num * (RTPPacket.CLOCK_RATE_VIDEO / self.args.frame_rate))

                packet = self.rtp.build(payload, timestamp=timestamp, marker=True)

                try:
                    self.sock.sendto(packet, dest)
                    self.total_packets += 1
                    self.total_bytes += len(packet)
                except OSError as e:
                    logging.warning(f"Send failed: {e}")

                # Schedule next frame
                next_frame_time += frame_interval_sec

                # Periodic status
                if now - last_status_time >= 2.0:
                    elapsed = now - self.start_time
                    fps = self.total_packets / elapsed
                    logging.info(f"Elapsed: {elapsed:.1f}s, Packets: {self.total_packets} "
                                f"({fps:.1f} fps), Spikes: {self.spike_count}")
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

        logging.info("")
        logging.info("=" * 50)
        logging.info("RTP JITTER SPIKE SENDER RESULTS")
        logging.info("=" * 50)
        logging.info(f"Duration: {elapsed:.1f}s")
        logging.info(f"Total packets: {self.total_packets}")
        logging.info(f"Total data: {format_bytes(self.total_bytes)}")
        logging.info(f"Average frame rate: {fps:.1f} fps")
        logging.info(f"Jitter spikes injected: {self.spike_count}")
        logging.info("")

        # Self-checks
        passed = True
        checks = []

        # Check 1: Did we send expected number of packets?
        expected_packets = self.args.duration * self.args.frame_rate
        if self.total_packets >= expected_packets * 0.8:
            checks.append(("Packet count", True,
                          f"{self.total_packets} (expected ~{expected_packets:.0f})"))
        else:
            checks.append(("Packet count", False,
                          f"Only {self.total_packets}, expected ~{expected_packets:.0f}"))
            passed = False

        # Check 2: Did we inject expected number of spikes?
        expected_spikes = int(self.args.duration / self.args.spike_interval)
        if self.spike_count >= expected_spikes - 1:
            checks.append(("Spike count", True,
                          f"{self.spike_count} spikes (expected ~{expected_spikes})"))
        else:
            checks.append(("Spike count", False,
                          f"Only {self.spike_count}, expected ~{expected_spikes}"))
            passed = False

        # Report checks
        logging.info("Self-check results:")
        for name, result, detail in checks:
            status = "PASS" if result else "FAIL"
            logging.info(f"  [{status}] {name}: {detail}")

        logging.info("")
        logging.info("Expected observations in JitterTrap:")
        logging.info("  - Jitter histogram shows outliers at high values")
        logging.info(f"  - IPG spikes of ~{self.args.spike_delay}ms every ~{self.args.spike_interval}s")
        logging.info("  - RTP jitter calculation shows periodic spikes")
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
        sender = JitterSpikeSender(args)
        return sender.run()
    except Exception as e:
        logging.error(f"Error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
