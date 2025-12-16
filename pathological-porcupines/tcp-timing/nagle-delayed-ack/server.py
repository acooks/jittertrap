#!/usr/bin/env python3
"""
Nagle-Delayed ACK Interaction - Server

Simple echo server that demonstrates delayed ACK behavior.

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
from common.network import create_tcp_socket
from common.logging_utils import setup_logging, format_bytes


# Default configuration
DEFAULT_PORT = 9999


def setup_argparse() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Nagle-Delayed ACK Server - demonstrates delayed ACK timing",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Basic server (delayed ACKs enabled - default)
    python server.py --port 9999

    # Server with immediate ACKs (no delayed ACK)
    python server.py --port 9999 --quickack

    # Echo server (sends response, affects ACK timing)
    python server.py --port 9999 --echo

JitterTrap observation:
    - Watch RTT histogram for bimodal distribution
    - ~40ms peak is delayed ACK timer (Linux)
    - Enabling --quickack or --echo will change the pattern
        """
    )
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"Listen port (default: {DEFAULT_PORT})")
    parser.add_argument("--echo", action="store_true",
                        help="Echo data back to client")
    parser.add_argument("--quickack", action="store_true",
                        help="Enable TCP_QUICKACK to disable delayed ACKs (Linux)")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    parser.add_argument("--quiet", "-q", action="store_true",
                        help="Quiet mode (errors only)")
    return parser.parse_args()


class NagleDelayedAckServer:
    """
    TCP server for demonstrating Nagle/delayed ACK interaction.

    Default behavior uses delayed ACKs, which interact badly with
    clients using Nagle's algorithm for small writes.
    """

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.server_socket = None
        self.running = True

    def setup(self) -> None:
        """Initialize server socket."""
        self.server_socket = create_tcp_socket()
        self.server_socket.bind(('0.0.0.0', self.args.port))
        self.server_socket.listen(5)

        logging.info(f"Server listening on port {self.args.port}")
        logging.info(f"Echo mode: {self.args.echo}")
        logging.info(f"Quick ACK: {self.args.quickack}")

        if not self.args.quickack and not self.args.echo:
            logging.info("Delayed ACKs will cause ~40ms latency with Nagle clients")

    def enable_quickack(self, sock: socket.socket) -> bool:
        """
        Enable TCP_QUICKACK on socket (Linux-specific).

        Returns True if successful.
        """
        try:
            # TCP_QUICKACK = 12 on Linux
            TCP_QUICKACK = 12
            sock.setsockopt(socket.IPPROTO_TCP, TCP_QUICKACK, 1)
            return True
        except (AttributeError, OSError):
            logging.warning("TCP_QUICKACK not available on this platform")
            return False

    def run(self) -> None:
        """Main execution loop."""
        self.setup()

        try:
            while self.running:
                logging.info("Waiting for client connection...")
                try:
                    client_socket, addr = self.server_socket.accept()
                except OSError:
                    break

                logging.info(f"Client connected from {addr[0]}:{addr[1]}")

                if self.args.quickack:
                    if self.enable_quickack(client_socket):
                        logging.info("TCP_QUICKACK enabled - ACKs sent immediately")

                self.handle_client(client_socket)

        except KeyboardInterrupt:
            logging.info("Interrupted by user")
        finally:
            self.cleanup()

    def handle_client(self, client_socket: socket.socket) -> None:
        """Handle a single client connection."""
        total_bytes = 0
        recv_count = 0
        start_time = time.monotonic()

        try:
            while self.running:
                # Re-enable quickack each recv (it's a one-shot on Linux)
                if self.args.quickack:
                    self.enable_quickack(client_socket)

                try:
                    data = client_socket.recv(65536)
                except (socket.timeout, ConnectionResetError):
                    break

                if not data:
                    break

                total_bytes += len(data)
                recv_count += 1

                # Echo data if requested
                if self.args.echo:
                    try:
                        client_socket.sendall(data)
                    except (BrokenPipeError, ConnectionResetError):
                        break

        finally:
            elapsed = time.monotonic() - start_time
            if recv_count > 0:
                avg_size = total_bytes / recv_count
                logging.info(f"Session: {format_bytes(total_bytes)}, "
                             f"{recv_count} recv()s, "
                             f"avg size: {avg_size:.0f} bytes, "
                             f"duration: {elapsed:.1f}s")
            client_socket.close()

    def cleanup(self) -> None:
        """Clean up resources."""
        if self.server_socket:
            self.server_socket.close()
        logging.info("Server shutdown complete")


def main() -> int:
    args = setup_argparse()
    setup_logging(verbose=args.verbose, quiet=args.quiet)

    try:
        server = NagleDelayedAckServer(args)
        server.run()
        return 0
    except Exception as e:
        logging.error(f"Error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
