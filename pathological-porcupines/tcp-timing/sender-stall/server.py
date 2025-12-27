#!/usr/bin/env python3
"""
Sender Stall - Server (Fast Receiver)

Receives data as fast as possible to ensure sender stalls are
not caused by receiver-side pressure.

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
from common.logging_utils import setup_logging, format_bytes


# Default configuration
DEFAULT_PORT = 9999
DEFAULT_RECV_BUF = 262144     # Large buffer to ensure no receiver-side pressure


def setup_argparse() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Sender Stall Server - fast receiver to isolate sender behavior",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Basic usage with default settings
    python server.py --port 9999

    # With verbose logging
    python server.py --port 9999 --verbose

JitterTrap observation:
    - Watch for throughput dips (sender stalls)
    - Window should stay healthy (no zero-window)
    - IPG histogram shows large gaps
        """
    )
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"Listen port (default: {DEFAULT_PORT})")
    parser.add_argument("--recv-buf", type=int, default=DEFAULT_RECV_BUF,
                        help=f"SO_RCVBUF size in bytes (default: {DEFAULT_RECV_BUF})")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    parser.add_argument("--quiet", "-q", action="store_true",
                        help="Quiet mode (errors only)")
    return parser.parse_args()


class SenderStallServer:
    """
    TCP server that receives data as fast as possible.

    Ensures sender stalls are visible without receiver-side interference.
    Observable: Large inter-packet gaps in JitterTrap, but no zero-window.
    """

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.server_socket = None
        self.client_socket = None
        self.total_bytes = 0
        self.start_time = None
        self.recv_count = 0

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

    def accept_connection(self) -> bool:
        """Wait for and accept a client connection."""
        logging.info("Waiting for client connection...")

        try:
            self.client_socket, addr = self.server_socket.accept()
            logging.info(f"Client connected from {addr[0]}:{addr[1]}")

            # Set large receive buffer on connected socket
            self.client_socket.setsockopt(
                socket.SOL_SOCKET, socket.SO_RCVBUF, self.args.recv_buf
            )

            actual_recv, _ = get_socket_buffer_sizes(self.client_socket)
            logging.debug(f"Connection receive buffer: {format_bytes(actual_recv)}")

            return True
        except OSError as e:
            logging.error(f"Accept failed: {e}")
            return False

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
        self.recv_count = 0
        self.start_time = time.monotonic()
        last_status_time = self.start_time

        logging.info("Receiving data (will read as fast as possible)...")

        try:
            # Set a reasonable timeout for detecting client disconnect
            self.client_socket.settimeout(5.0)

            while True:
                try:
                    data = self.client_socket.recv(65536)  # Large reads for speed
                except socket.timeout:
                    # Check if client might still be sending
                    continue
                except ConnectionResetError:
                    logging.info("Connection reset by client")
                    break

                if not data:
                    logging.info("Client disconnected (EOF)")
                    break

                self.total_bytes += len(data)
                self.recv_count += 1

                # Periodic status
                now = time.monotonic()
                if now - last_status_time >= 5.0:
                    elapsed = now - self.start_time
                    rate = self.total_bytes / elapsed / 1024
                    logging.debug(f"Received: {format_bytes(self.total_bytes)} "
                                 f"in {elapsed:.1f}s ({rate:.1f} KB/s)")
                    last_status_time = now

        finally:
            if self.client_socket:
                self.client_socket.close()
                self.client_socket = None

        # Report results
        elapsed = time.monotonic() - self.start_time
        avg_rate = self.total_bytes / elapsed / 1024 if elapsed > 0 else 0

        logging.info("")
        logging.info("=" * 50)
        logging.info("SENDER STALL TEST COMPLETE")
        logging.info("=" * 50)
        logging.info(f"Total received: {format_bytes(self.total_bytes)}")
        logging.info(f"Duration: {elapsed:.1f}s")
        logging.info(f"Average rate: {avg_rate:.1f} KB/s")
        logging.info(f"Receive calls: {self.recv_count}")
        logging.info("")
        logging.info("Expected observations in JitterTrap:")
        logging.info("  - TCP Window chart: stays healthy (non-zero)")
        logging.info("  - Throughput chart: periodic dips during sender stalls")
        logging.info("  - Flow details: NO zero-window events")
        logging.info("  - IPG histogram: large gaps visible")
        logging.info("")

        return 0

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
        server = SenderStallServer(args)
        return server.run()
    except Exception as e:
        logging.error(f"Error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
