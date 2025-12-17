#!/usr/bin/env python3
"""
Receiver Starvation - Server (Receiver)

Simulates a slow receiver that cannot keep up with incoming data,
causing TCP zero-window events.

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
from common.network import create_tcp_socket, get_socket_buffer_sizes
from common.logging_utils import setup_logging, format_bytes, format_rate


# Default configuration
DEFAULT_PORT = 9999
DEFAULT_RECV_BUF = 16384      # 16KB buffer - moderate size
DEFAULT_DELAY = 0.01          # 10ms delay between reads
DEFAULT_READ_SIZE = 8192      # 8KB per recv() → ~800 KB/s max receive rate
DEFAULT_DURATION = 15         # Test duration in seconds


def setup_argparse() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Receiver Starvation Server - simulates slow receiver",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Basic usage with default settings
    python server.py --port 9999

    # Aggressive starvation (small buffer, long delays)
    python server.py --port 9999 --recv-buf 4096 --delay 0.5

    # Mild starvation (larger buffer, short delays)
    python server.py --port 9999 --recv-buf 65536 --delay 0.05

JitterTrap observation:
    - Watch for zero-window events in flow details
    - RTT histogram will shift right as buffers fill
    - Throughput will drop during starvation periods
        """
    )
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"Listen port (default: {DEFAULT_PORT})")
    parser.add_argument("--recv-buf", type=int, default=DEFAULT_RECV_BUF,
                        help=f"SO_RCVBUF size in bytes (default: {DEFAULT_RECV_BUF})")
    parser.add_argument("--delay", type=float, default=DEFAULT_DELAY,
                        help=f"Delay between recv() calls in seconds (default: {DEFAULT_DELAY})")
    parser.add_argument("--read-size", type=int, default=DEFAULT_READ_SIZE,
                        help=f"Bytes to read per recv() (default: {DEFAULT_READ_SIZE})")
    parser.add_argument("--duration", type=float, default=DEFAULT_DURATION,
                        help=f"Test duration in seconds (default: {DEFAULT_DURATION})")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    parser.add_argument("--quiet", "-q", action="store_true",
                        help="Quiet mode (errors only)")
    return parser.parse_args()


class ReceiverStarvationServer:
    """
    TCP server that simulates slow receive processing.

    Simulates: Application blocked on slow I/O or computation
    Observable effect: Zero-window events, sender blocks
    """

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.server_socket = None
        self.client_socket = None
        self.running = True
        self.total_bytes = 0
        self.start_time = None

    def setup(self) -> None:
        """Initialize server socket."""
        self.server_socket = create_tcp_socket(recv_buf=self.args.recv_buf)
        self.server_socket.bind(('0.0.0.0', self.args.port))
        self.server_socket.listen(1)

        # Report actual buffer size (kernel may adjust)
        actual_recv, actual_send = get_socket_buffer_sizes(self.server_socket)
        logging.info(f"Server listening on port {self.args.port}")
        logging.info(f"Receive buffer: {format_bytes(actual_recv)} "
                     f"(requested {format_bytes(self.args.recv_buf)})")
        logging.info(f"Read delay: {self.args.delay * 1000:.0f}ms, "
                     f"read size: {format_bytes(self.args.read_size)}")

    def accept_connection(self) -> bool:
        """Wait for and accept a client connection."""
        logging.info("Waiting for client connection...")

        try:
            self.client_socket, addr = self.server_socket.accept()
            logging.info(f"Client connected from {addr[0]}:{addr[1]}")

            # Set receive buffer on connected socket too
            self.client_socket.setsockopt(
                socket.SOL_SOCKET, socket.SO_RCVBUF, self.args.recv_buf
            )

            actual_recv, _ = get_socket_buffer_sizes(self.client_socket)
            logging.debug(f"Connection receive buffer: {format_bytes(actual_recv)}")

            return True
        except OSError as e:
            logging.error(f"Accept failed: {e}")
            return False

    def simulate_slow_processing(self) -> None:
        """
        Simulate slow application processing.

        This is the pathology - we delay between recv() calls,
        causing the socket buffer to fill and TCP to advertise zero window.
        """
        time.sleep(self.args.delay)

    def run(self) -> int:
        """Main execution loop. Returns exit code."""
        self.setup()
        exit_code = 0

        try:
            if not self.accept_connection():
                return 1

            exit_code = self.handle_client()

        except KeyboardInterrupt:
            logging.info("Interrupted by user")
        finally:
            self.cleanup()

        return exit_code

    def handle_client(self) -> int:
        """Handle a single client connection. Returns exit code."""
        self.total_bytes = 0
        self.start_time = time.monotonic()
        end_time = self.start_time + self.args.duration
        last_report_time = self.start_time
        last_report_bytes = 0

        # Calculate theoretical receive capacity
        recv_capacity = self.args.read_size / self.args.delay
        logging.info(f"Starting slow receive loop for {self.args.duration}s")
        logging.info(f"Receive capacity: ~{format_rate(recv_capacity)} "
                     f"({format_bytes(self.args.read_size)} every {self.args.delay*1000:.0f}ms)")
        logging.info("Window will oscillate as buffer fills/drains")

        try:
            while self.running and time.monotonic() < end_time:
                # This delay is the pathology - it causes buffer to fill
                self.simulate_slow_processing()

                try:
                    data = self.client_socket.recv(self.args.read_size)
                except socket.timeout:
                    continue
                except ConnectionResetError:
                    logging.warning("Connection reset by client")
                    break

                if not data:
                    logging.info("Client disconnected (EOF)")
                    break

                self.total_bytes += len(data)

                # Periodic progress report
                now = time.monotonic()
                if now - last_report_time >= 5.0:
                    elapsed = now - last_report_time
                    bytes_interval = self.total_bytes - last_report_bytes
                    rate = bytes_interval / elapsed

                    total_elapsed = now - self.start_time
                    avg_rate = self.total_bytes / total_elapsed

                    logging.info(
                        f"Received: {format_bytes(self.total_bytes)} total, "
                        f"current: {format_rate(rate)}, "
                        f"avg: {format_rate(avg_rate)}"
                    )

                    last_report_time = now
                    last_report_bytes = self.total_bytes

        finally:
            if self.client_socket:
                self.client_socket.close()
                self.client_socket = None

        return self.report_final_stats()

    def report_final_stats(self) -> int:
        """Report final statistics and run self-checks. Returns exit code."""
        if self.start_time is None:
            return 1

        elapsed = time.monotonic() - self.start_time
        if elapsed <= 0:
            return 1

        avg_rate = self.total_bytes / elapsed
        recv_capacity = self.args.read_size / self.args.delay

        logging.info("")
        logging.info("=" * 50)
        logging.info("RECEIVER STARVATION TEST RESULTS")
        logging.info("=" * 50)
        logging.info(f"Duration: {elapsed:.1f}s")
        logging.info(f"Total received: {format_bytes(self.total_bytes)}")
        logging.info(f"Average rate: {format_rate(avg_rate)}")
        logging.info(f"Receive capacity: ~{format_rate(recv_capacity)}")
        logging.info("")

        # Self-check assertions
        passed = True
        checks = []

        # Check 1: Did we receive meaningful data?
        if self.total_bytes >= 100000:  # At least 100KB
            checks.append(("Data received", True, format_bytes(self.total_bytes)))
        else:
            checks.append(("Data received", False,
                          f"Only {format_bytes(self.total_bytes)} (expected >100KB)"))
            passed = False

        # Check 2: Was receive rate limited by our slow processing?
        # Average rate should be close to (but not exceed) our receive capacity
        if avg_rate <= recv_capacity * 1.2:  # Allow 20% margin
            checks.append(("Rate limited by receiver", True,
                          f"{format_rate(avg_rate)} <= {format_rate(recv_capacity)}"))
        else:
            checks.append(("Rate limited by receiver", False,
                          f"Rate {format_rate(avg_rate)} exceeds capacity"))

        # Check 3: Test ran for expected duration
        if elapsed >= self.args.duration * 0.8:
            checks.append(("Duration", True, f"{elapsed:.1f}s"))
        else:
            checks.append(("Duration", False,
                          f"Only {elapsed:.1f}s (expected {self.args.duration}s)"))
            passed = False

        logging.info("Self-check results:")
        for name, result, detail in checks:
            status = "PASS" if result else "FAIL"
            logging.info(f"  [{status}] {name}: {detail}")

        logging.info("")
        logging.info("Expected observations in JitterTrap:")
        logging.info("  - TCP Window: oscillating pattern (fills/drains)")
        logging.info("  - Throughput: limited by receiver capacity")
        logging.info("  - Brief zero-window events possible")
        logging.info("")

        return 0 if passed else 1

    def cleanup(self) -> None:
        """Clean up resources."""
        if self.client_socket:
            self.client_socket.close()
        if self.server_socket:
            self.server_socket.close()
        logging.info("Server shutdown complete")


def main() -> int:
    args = setup_argparse()
    setup_logging(verbose=args.verbose, quiet=args.quiet)

    try:
        server = ReceiverStarvationServer(args)
        return server.run()
    except Exception as e:
        logging.error(f"Error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
