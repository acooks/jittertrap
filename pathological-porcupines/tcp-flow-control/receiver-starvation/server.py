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
DEFAULT_RECV_BUF = 8192       # Small buffer to trigger zero-window quickly
DEFAULT_DELAY = 0.1           # 100ms delay between reads
DEFAULT_READ_SIZE = 1024      # Bytes per recv()


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

    def run(self) -> None:
        """Main execution loop."""
        self.setup()

        try:
            while self.running:
                if not self.accept_connection():
                    continue

                self.handle_client()

        except KeyboardInterrupt:
            logging.info("Interrupted by user")
        finally:
            self.cleanup()

    def handle_client(self) -> None:
        """Handle a single client connection."""
        self.total_bytes = 0
        self.start_time = time.monotonic()
        last_report_time = self.start_time
        last_report_bytes = 0

        logging.info("Starting slow receive loop (Ctrl+C to stop)")
        logging.info(f"Will cause zero-window events due to {self.args.delay*1000:.0f}ms "
                     f"delay between {format_bytes(self.args.read_size)} reads")

        try:
            while self.running:
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
            self.report_final_stats()
            if self.client_socket:
                self.client_socket.close()
                self.client_socket = None

    def report_final_stats(self) -> None:
        """Report final statistics."""
        if self.start_time is None:
            return

        elapsed = time.monotonic() - self.start_time
        if elapsed > 0:
            avg_rate = self.total_bytes / elapsed
            logging.info(f"Session complete: {format_bytes(self.total_bytes)} "
                         f"in {elapsed:.1f}s ({format_rate(avg_rate)} avg)")

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
        server.run()
        return 0
    except Exception as e:
        logging.error(f"Error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
