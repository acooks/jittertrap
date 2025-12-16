#!/usr/bin/env python3
"""
Persist Timer - Client (Sender that fills receiver buffer)

Sends data continuously until blocked by zero-window, demonstrating
TCP persist timer mechanism.

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
from common.network import create_tcp_socket, get_socket_buffer_sizes, get_tcp_info
from common.logging_utils import setup_logging, format_bytes, format_duration


# Default configuration
DEFAULT_HOST = "10.0.1.2"
DEFAULT_PORT = 9999
DEFAULT_SEND_BUF = 65536      # Large buffer to push data quickly
DEFAULT_DURATION = 12         # Total test duration (should exceed server normal + stall)
DEFAULT_BLOCK_SIZE = 8192     # Size of each send


def setup_argparse() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Persist Timer Client - fills receiver buffer to trigger zero-window",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Connect to default server
    python client.py --host 10.0.1.2 --port 9999

    # Aggressive sending (larger blocks)
    python client.py --host 10.0.1.2 --port 9999 --block-size 16384

    # Longer test duration
    python client.py --host 10.0.1.2 --duration 30

JitterTrap observation:
    - TCP Window chart: drops to zero during stall
    - Throughput chart: drops to near-zero during stall
    - Flow details: zero-window events
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
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    parser.add_argument("--quiet", "-q", action="store_true",
                        help="Quiet mode (errors only)")
    return parser.parse_args()


class PersistTimerClient:
    """
    TCP client that demonstrates persist timer by filling receiver buffer.

    Tracks blocking behavior to detect when window opens from persist probes.
    """

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.sock = None
        self.total_bytes = 0
        self.total_sends = 0
        self.block_events = []  # Timestamps of blocking periods
        self.send_data = b'X' * args.block_size

    def connect(self) -> bool:
        """Connect to server."""
        self.sock = create_tcp_socket(send_buf=self.args.send_buf, nodelay=True)

        # Use non-blocking for better control, with select for waiting
        self.sock.setblocking(False)

        logging.info(f"Connecting to {self.args.host}:{self.args.port}...")

        try:
            self.sock.connect((self.args.host, self.args.port))
        except BlockingIOError:
            # Non-blocking connect in progress
            pass

        # Wait for connection (up to 10 seconds)
        import select
        _, writable, _ = select.select([], [self.sock], [], 10.0)
        if not writable:
            logging.error("Connection timeout")
            return False

        # Check for connection error
        err = self.sock.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
        if err != 0:
            logging.error(f"Connection failed: {err}")
            return False

        actual_recv, actual_send = get_socket_buffer_sizes(self.sock)
        logging.info(f"Connected to {self.args.host}:{self.args.port}")
        logging.info(f"Send buffer: {format_bytes(actual_send)} "
                     f"(requested {format_bytes(self.args.send_buf)})")

        return True

    def run(self) -> int:
        """Main execution. Returns exit code."""
        if not self.connect():
            return 1

        import select

        start_time = time.monotonic()
        end_time = start_time + self.args.duration
        last_status_time = start_time
        in_blocked_state = False
        block_start = None

        logging.info(f"Sending data for {self.args.duration}s...")
        logging.info("Waiting for server to stop reading (zero-window)...")

        try:
            while time.monotonic() < end_time:
                now = time.monotonic()

                # Try to send data
                try:
                    sent = self.sock.send(self.send_data)
                    self.total_bytes += sent
                    self.total_sends += 1

                    if in_blocked_state:
                        # We were blocked, now we can send again
                        block_duration = now - block_start
                        self.block_events.append({
                            'start': block_start - start_time,
                            'duration': block_duration,
                            'timestamp': now - start_time,
                        })
                        # Only log significant blocks (>= 1s)
                        if block_duration >= 1.0:
                            logging.info(f"Window opened after {block_duration:.1f}s block")
                        in_blocked_state = False

                except BlockingIOError:
                    # Socket buffer full - window likely zero
                    if not in_blocked_state:
                        in_blocked_state = True
                        block_start = now
                        logging.debug("BLOCKED - zero-window condition detected")

                    # Wait for socket to become writable or timeout
                    remaining = end_time - now
                    if remaining <= 0:
                        break

                    # Short wait with select to detect when we can write again
                    timeout = min(0.5, remaining)
                    _, writable, _ = select.select([], [self.sock], [], timeout)

                except ConnectionResetError:
                    logging.warning("Connection reset by server")
                    break

                except BrokenPipeError:
                    logging.info("Server closed connection")
                    break

                # Periodic status
                if now - last_status_time >= 5.0:
                    elapsed = now - start_time
                    rate = self.total_bytes / elapsed
                    tcp_info = get_tcp_info(self.sock)
                    if tcp_info:
                        logging.debug(f"Elapsed: {elapsed:.1f}s, "
                                     f"Sent: {format_bytes(self.total_bytes)}, "
                                     f"Probes: {tcp_info.get('probes', 0)}")
                    last_status_time = now

        except KeyboardInterrupt:
            logging.info("Interrupted by user")
        finally:
            if in_blocked_state:
                # Record final block if still blocked at end
                block_duration = time.monotonic() - block_start
                self.block_events.append({
                    'start': block_start - start_time,
                    'duration': block_duration,
                    'timestamp': time.monotonic() - start_time,
                    'ongoing': True,
                })

            self.cleanup()

        return self.report_results(start_time)

    def report_results(self, start_time: float) -> int:
        """Report test results and run self-checks. Returns exit code."""
        elapsed = time.monotonic() - start_time

        logging.info("")
        logging.info("=" * 50)
        logging.info("PERSIST TIMER TEST RESULTS")
        logging.info("=" * 50)
        logging.info(f"Duration: {format_duration(elapsed)}")
        logging.info(f"Total sent: {format_bytes(self.total_bytes)} in {self.total_sends} sends")
        logging.info("")

        # Analyze block events - only show significant ones (>= 1s)
        if self.block_events:
            significant = [e for e in self.block_events if e['duration'] >= 1.0]
            logging.info(f"Blocking events: {len(self.block_events)} total, {len(significant)} significant (>= 1s)")
            for i, event in enumerate(significant[:10]):  # Show up to 10
                ongoing = " (ongoing at test end)" if event.get('ongoing') else ""
                logging.info(f"  Block {i+1}: started at {event['start']:.1f}s, "
                            f"duration {event['duration']:.1f}s{ongoing}")
            if len(significant) > 10:
                logging.info(f"  ... and {len(significant) - 10} more significant blocks")

            # Check for persist timer intervals (should be ~5s, ~10s, ~20s...)
            if len(significant) >= 2:
                logging.info("")
                logging.info(f"Significant block intervals (>= 1s, showing up to 10):")
                for i in range(1, min(len(significant), 11)):
                    interval = significant[i]['start'] - significant[i-1]['start']
                    logging.info(f"  Interval {i}: {interval:.1f}s")

        logging.info("")

        # Self-check assertions
        passed = True
        checks = []

        # Check 1: Did we detect at least one blocking event?
        if len(self.block_events) >= 1:
            checks.append(("Zero-window detected", True,
                          f"{len(self.block_events)} block event(s)"))
        else:
            checks.append(("Zero-window detected", False,
                          "No blocking events detected"))
            passed = False

        # Check 2: Did the block last long enough to trigger persist timer?
        # Initial persist timer is typically 5 seconds
        long_blocks = [e for e in self.block_events if e['duration'] >= 4.0]
        if long_blocks:
            checks.append(("Persist timer triggered", True,
                          f"{len(long_blocks)} block(s) >= 4s"))
        else:
            checks.append(("Persist timer triggered", False,
                          "No blocks >= 4s (persist timer interval)"))
            # Not a hard failure - may depend on server timing

        # Check 3: Did we send meaningful data before blocking?
        if self.total_bytes >= 10000:
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
        logging.info("  - TCP Window chart: drops to zero during block")
        logging.info("  - Throughput chart: drops to near-zero during block")
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
        client = PersistTimerClient(args)
        return client.run()
    except Exception as e:
        logging.error(f"Error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
