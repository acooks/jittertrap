#!/usr/bin/env python3
"""
Silly Window Syndrome - Server (Receiver)

Simulates receiver-side Silly Window Syndrome by reading tiny amounts
and having a very small receive buffer.

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


# Default configuration - designed to trigger SWS
DEFAULT_PORT = 9999
DEFAULT_RECV_BUF = 256        # Very small buffer
DEFAULT_READ_SIZE = 1         # Read 1 byte at a time (classic SWS)
DEFAULT_DELAY = 0.001         # 1ms between reads


def setup_argparse() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Silly Window Syndrome Server - tiny buffer, tiny reads",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Classic SWS (extreme)
    python server.py --port 9999 --recv-buf 256 --read-size 1

    # Moderate SWS
    python server.py --port 9999 --recv-buf 1024 --read-size 10

    # SWS avoidance demonstration (read all available)
    python server.py --port 9999 --recv-buf 256 --read-size 0

JitterTrap observation:
    - Packet size histogram shows very small packets
    - High packet rate with low throughput
    - Watch the packet size distribution shift left
        """
    )
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"Listen port (default: {DEFAULT_PORT})")
    parser.add_argument("--recv-buf", type=int, default=DEFAULT_RECV_BUF,
                        help=f"SO_RCVBUF size (default: {DEFAULT_RECV_BUF})")
    parser.add_argument("--read-size", type=int, default=DEFAULT_READ_SIZE,
                        help=f"Bytes per recv() (0=all available, default: {DEFAULT_READ_SIZE})")
    parser.add_argument("--delay", type=float, default=DEFAULT_DELAY,
                        help=f"Delay between reads in seconds (default: {DEFAULT_DELAY})")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    parser.add_argument("--quiet", "-q", action="store_true",
                        help="Quiet mode (errors only)")
    return parser.parse_args()


class SillyWindowServer:
    """
    TCP server that triggers Silly Window Syndrome.

    By using a tiny receive buffer and reading 1 byte at a time,
    we cause the advertised window to fluctuate in tiny increments,
    leading to inefficient tiny segment transmissions.
    """

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.server_socket = None
        self.client_socket = None
        self.running = True
        self.total_bytes = 0
        self.read_count = 0
        self.start_time = None

    def setup(self) -> None:
        """Initialize server socket."""
        self.server_socket = create_tcp_socket(recv_buf=self.args.recv_buf)
        self.server_socket.bind(('0.0.0.0', self.args.port))
        self.server_socket.listen(1)

        actual_recv, _ = get_socket_buffer_sizes(self.server_socket)
        logging.info(f"Server listening on port {self.args.port}")
        logging.info(f"Receive buffer: {format_bytes(actual_recv)} "
                     f"(requested {format_bytes(self.args.recv_buf)})")

        read_desc = "all available" if self.args.read_size == 0 else f"{self.args.read_size} byte(s)"
        logging.info(f"Reading {read_desc} per recv(), delay: {self.args.delay*1000:.1f}ms")

        if self.args.read_size == 1:
            logging.warning("Classic SWS mode: reading 1 byte at a time")
            logging.warning("This will cause many tiny TCP segments!")

    def accept_connection(self) -> bool:
        """Wait for and accept a client connection."""
        logging.info("Waiting for client connection...")

        try:
            self.client_socket, addr = self.server_socket.accept()
            logging.info(f"Client connected from {addr[0]}:{addr[1]}")

            # Apply small receive buffer to connected socket
            self.client_socket.setsockopt(
                socket.SOL_SOCKET, socket.SO_RCVBUF, self.args.recv_buf
            )

            return True
        except OSError as e:
            logging.error(f"Accept failed: {e}")
            return False

    def read_data(self) -> bytes:
        """
        Read data in SWS-inducing manner.

        Returns:
            Data read (empty bytes on EOF/error)
        """
        if self.args.read_size == 0:
            # SWS avoidance: read all available
            return self.client_socket.recv(65536)
        else:
            # SWS trigger: read tiny amount
            return self.client_socket.recv(self.args.read_size)

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
        self.read_count = 0
        self.start_time = time.monotonic()
        last_report_time = self.start_time

        logging.info("Starting SWS receive loop (Ctrl+C to stop)")

        try:
            while self.running:
                # Small delay to simulate slow processing
                if self.args.delay > 0:
                    time.sleep(self.args.delay)

                try:
                    data = self.read_data()
                except (socket.timeout, ConnectionResetError):
                    break

                if not data:
                    logging.info("Client disconnected (EOF)")
                    break

                self.total_bytes += len(data)
                self.read_count += 1

                # Periodic progress report
                now = time.monotonic()
                if now - last_report_time >= 5.0:
                    elapsed = now - self.start_time
                    avg_read_size = self.total_bytes / self.read_count if self.read_count > 0 else 0
                    reads_per_sec = self.read_count / elapsed

                    logging.info(
                        f"Received: {format_bytes(self.total_bytes)}, "
                        f"reads: {self.read_count}, "
                        f"avg read size: {avg_read_size:.1f} bytes, "
                        f"reads/s: {reads_per_sec:.0f}"
                    )

                    last_report_time = now

        finally:
            self.report_final_stats()
            if self.client_socket:
                self.client_socket.close()
                self.client_socket = None

    def report_final_stats(self) -> None:
        """Report final statistics."""
        if self.start_time is None or self.read_count == 0:
            return

        elapsed = time.monotonic() - self.start_time
        avg_read_size = self.total_bytes / self.read_count
        reads_per_sec = self.read_count / elapsed
        throughput = self.total_bytes / elapsed

        logging.info(f"Session complete:")
        logging.info(f"  Total: {format_bytes(self.total_bytes)} in {elapsed:.1f}s")
        logging.info(f"  Reads: {self.read_count} ({reads_per_sec:.0f}/s)")
        logging.info(f"  Avg read size: {avg_read_size:.1f} bytes")
        logging.info(f"  Throughput: {throughput/1024:.1f} KB/s")

        if avg_read_size < 10:
            logging.warning(f"SWS confirmed: average read size was only {avg_read_size:.1f} bytes!")

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
        server = SillyWindowServer(args)
        server.run()
        return 0
    except Exception as e:
        logging.error(f"Error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
