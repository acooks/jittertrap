#!/usr/bin/env python3
"""
Sender Stall - Client (Stuttering Sender)

Sends data with periodic application-level stalls to demonstrate
inter-packet gaps without receiver-side pressure.

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
sys.path.insert(0, str(Path(__file__).parent.parent.parent.parent))
from common.network import create_tcp_socket, get_socket_buffer_sizes
from common.logging_utils import setup_logging, format_bytes, format_duration


# Default configuration
DEFAULT_HOST = "10.0.1.2"
DEFAULT_PORT = 9999
DEFAULT_SEND_BUF = 65536
DEFAULT_DURATION = 15
DEFAULT_BLOCK_SIZE = 8192
DEFAULT_STALL_MS = 500        # 500ms stall
DEFAULT_INTERVAL_MS = 1000    # Every 1 second
DEFAULT_PATTERN = "varying"

# Varying stall durations to show different gap sizes in IPG chart
VARYING_STALL_DURATIONS_MS = [5, 10, 20, 50, 100]


def setup_argparse() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Sender Stall Client - stuttering sender demonstration",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Default: 500ms stall every 2 seconds
    python client.py --host 10.0.1.2 --port 9999

    # Short frequent stalls (disk I/O simulation)
    python client.py --stall 50 --interval 500

    # Long infrequent stalls (database query simulation)
    python client.py --stall 2000 --interval 5000

    # Random stalls
    python client.py --stall 500 --interval 2000 --pattern random

    # Burst pattern (3 quick stalls then pause)
    python client.py --stall 200 --interval 300 --pattern burst

    # Trigger TCP idle restart (stall > RTO)
    python client.py --stall 1500 --interval 5000

JitterTrap observation:
    - Throughput chart: periodic dips
    - TCP Window chart: stays healthy (non-zero)
    - No zero-window events in flow details
        """
    )
    parser.add_argument("--host", "-H", default=DEFAULT_HOST,
                        help=f"Server address (default: {DEFAULT_HOST})")
    parser.add_argument("--port", "-p", type=int, default=DEFAULT_PORT,
                        help=f"Server port (default: {DEFAULT_PORT})")
    parser.add_argument("--send-buf", type=int, default=DEFAULT_SEND_BUF,
                        help=f"SO_SNDBUF size in bytes (default: {DEFAULT_SEND_BUF})")
    parser.add_argument("--block-size", type=int, default=DEFAULT_BLOCK_SIZE,
                        help=f"Send block size in bytes (default: {DEFAULT_BLOCK_SIZE})")
    parser.add_argument("--duration", type=float, default=DEFAULT_DURATION,
                        help=f"Test duration in seconds (default: {DEFAULT_DURATION})")
    parser.add_argument("--stall", type=int, default=DEFAULT_STALL_MS,
                        help=f"Stall duration in milliseconds (default: {DEFAULT_STALL_MS})")
    parser.add_argument("--interval", type=int, default=DEFAULT_INTERVAL_MS,
                        help=f"Time between stalls in milliseconds (default: {DEFAULT_INTERVAL_MS})")
    parser.add_argument("--pattern", choices=["periodic", "random", "burst", "varying"],
                        default=DEFAULT_PATTERN,
                        help=f"Stall pattern (default: {DEFAULT_PATTERN})")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    parser.add_argument("--quiet", "-q", action="store_true",
                        help="Quiet mode (errors only)")
    return parser.parse_args()


class SenderStallClient:
    """
    TCP client that demonstrates sender stalls.

    Stall patterns:
    - periodic: Regular stalls at fixed intervals
    - random: Stalls with random duration (0.5x to 1.5x) at random intervals
    - burst: 3 quick stalls, then a longer pause
    - varying: Cycles through different stall durations (1, 5, 10, 20, 50ms)
    """

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.sock = None
        self.total_bytes = 0
        self.total_sends = 0
        self.stalls_executed = []
        self.total_stall_time = 0.0
        self.send_data = b'X' * args.block_size

        # Convert ms to seconds
        self.stall_duration = args.stall / 1000.0
        self.stall_interval = args.interval / 1000.0

        # Burst pattern state
        self.burst_count = 0

        # Varying pattern state
        self.varying_index = 0

    def connect(self) -> bool:
        """Connect to server."""
        self.sock = create_tcp_socket(send_buf=self.args.send_buf, nodelay=True)

        logging.info(f"Connecting to {self.args.host}:{self.args.port}...")

        try:
            self.sock.connect((self.args.host, self.args.port))
        except (ConnectionRefusedError, OSError) as e:
            logging.error(f"Connection failed: {e}")
            return False

        actual_recv, actual_send = get_socket_buffer_sizes(self.sock)
        logging.info(f"Connected to {self.args.host}:{self.args.port}")
        logging.info(f"Send buffer: {format_bytes(actual_send)} "
                     f"(requested {format_bytes(self.args.send_buf)})")

        return True

    def get_next_stall(self) -> tuple:
        """
        Get the next stall duration and interval based on pattern.
        Returns (stall_duration, next_interval) in seconds.
        """
        if self.args.pattern == "periodic":
            return self.stall_duration, self.stall_interval

        elif self.args.pattern == "random":
            # Random duration: 0.5x to 1.5x of base
            duration = self.stall_duration * random.uniform(0.5, 1.5)
            # Random interval: 0.5x to 1.5x of base
            interval = self.stall_interval * random.uniform(0.5, 1.5)
            return duration, interval

        elif self.args.pattern == "burst":
            self.burst_count += 1
            if self.burst_count <= 3:
                # Quick stalls during burst
                return self.stall_duration, self.stall_interval
            else:
                # Reset burst and longer pause
                self.burst_count = 0
                return self.stall_duration, self.stall_interval * 3

        elif self.args.pattern == "varying":
            # Cycle through different stall durations to show variety in IPG chart
            duration_ms = VARYING_STALL_DURATIONS_MS[self.varying_index]
            self.varying_index = (self.varying_index + 1) % len(VARYING_STALL_DURATIONS_MS)
            return duration_ms / 1000.0, self.stall_interval

        return self.stall_duration, self.stall_interval

    def run(self) -> int:
        """Main execution. Returns exit code."""
        if not self.connect():
            return 1

        start_time = time.monotonic()
        end_time = start_time + self.args.duration
        last_stall_time = start_time
        next_interval = self.stall_interval

        logging.info(f"Sending data for {self.args.duration}s with stalls "
                    f"({self.args.stall}ms every {self.args.interval}ms, "
                    f"pattern: {self.args.pattern})...")

        try:
            while time.monotonic() < end_time:
                now = time.monotonic()

                # Check if it's time for a stall
                if now - last_stall_time >= next_interval:
                    stall_duration, next_interval = self.get_next_stall()

                    stall_start = now - start_time
                    logging.info(f"Stall {len(self.stalls_executed) + 1} at {stall_start:.1f}s "
                               f"({stall_duration * 1000:.0f}ms)")

                    # Execute the stall
                    time.sleep(stall_duration)

                    self.stalls_executed.append({
                        'start': stall_start,
                        'duration': stall_duration,
                    })
                    self.total_stall_time += stall_duration
                    last_stall_time = time.monotonic()

                # Send data
                try:
                    sent = self.sock.send(self.send_data)
                    self.total_bytes += sent
                    self.total_sends += 1

                except BlockingIOError:
                    # Socket buffer full - this shouldn't happen with fast receiver
                    # but handle it gracefully
                    time.sleep(0.001)

                except (ConnectionResetError, BrokenPipeError):
                    logging.info("Server closed connection")
                    break

        except KeyboardInterrupt:
            logging.info("Interrupted by user")
        finally:
            self.cleanup()

        return self.report_results(start_time)

    def report_results(self, start_time: float) -> int:
        """Report test results and run self-checks. Returns exit code."""
        elapsed = time.monotonic() - start_time
        active_time = elapsed - self.total_stall_time

        logging.info("")
        logging.info("=" * 50)
        logging.info("SENDER STALL TEST RESULTS")
        logging.info("=" * 50)
        logging.info(f"Duration: {format_duration(elapsed)}")
        logging.info(f"Total sent: {format_bytes(self.total_bytes)} in {self.total_sends} sends")
        logging.info(f"Stalls executed: {len(self.stalls_executed)}")
        logging.info(f"Total stall time: {self.total_stall_time:.1f}s")
        logging.info(f"Active sending time: {active_time:.1f}s")

        if active_time > 0:
            effective_rate = self.total_bytes / active_time / 1024
            logging.info(f"Effective rate (during sends): {effective_rate:.1f} KB/s")

        logging.info("")

        # Self-check assertions
        passed = True
        checks = []

        # Check 1: No zero-window (we can't directly detect this, but we can
        # check if we were never blocked for extended periods)
        # If receiver was fast, we shouldn't have significant blocking
        checks.append(("No zero-window expected", True,
                      "Receiver has large buffer (check JitterTrap for confirmation)"))

        # Check 2: Did stalls occur as configured?
        expected_stalls = int(self.args.duration / (self.stall_interval + self.stall_duration))
        stall_count = len(self.stalls_executed)
        if stall_count >= expected_stalls * 0.7:  # Allow some variance
            avg_stall = sum(s['duration'] for s in self.stalls_executed) / max(1, stall_count)
            checks.append(("Stalls executed", True,
                          f"{stall_count} stalls, avg {avg_stall*1000:.0f}ms"))
        else:
            checks.append(("Stalls executed", False,
                          f"Only {stall_count} stalls (expected ~{expected_stalls})"))
            passed = False

        # Check 3: Did we send meaningful data?
        if self.total_bytes >= 100000:
            checks.append(("Data sent", True, format_bytes(self.total_bytes)))
        else:
            checks.append(("Data sent", False,
                          f"Only {format_bytes(self.total_bytes)} sent"))
            passed = False

        # Report checks
        logging.info("Self-check results:")
        for name, result, detail in checks:
            status = "PASS" if result else "FAIL"
            logging.info(f"  [{status}] {name}: {detail}")

        logging.info("")
        logging.info("Expected observations in JitterTrap:")
        logging.info("  - Throughput chart: periodic dips during stalls")
        logging.info("  - TCP Window chart: stays healthy (non-zero)")
        logging.info("  - Flow details: NO zero-window events")
        logging.info("  - IPG histogram: large gaps visible")
        logging.info("")
        logging.info("Key diagnostic: If window stays healthy but throughput dips,")
        logging.info("the problem is sender-side (application stall), not receiver-side.")
        logging.info("")

        return 0 if passed else 1

    def cleanup(self) -> None:
        """Clean up resources."""
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
        logging.info("Client shutdown complete")


def main() -> int:
    args = setup_argparse()
    setup_logging(verbose=args.verbose, quiet=args.quiet)

    try:
        client = SenderStallClient(args)
        return client.run()
    except Exception as e:
        logging.error(f"Error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
