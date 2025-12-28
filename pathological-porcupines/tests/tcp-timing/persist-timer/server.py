#!/usr/bin/env python3
"""
Persist Timer - Server (Receiver that stops reading)

Simulates a receiver that stops reading data, causing TCP zero-window
events and triggering the persist timer mechanism.

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
from common.network import create_tcp_socket, get_socket_buffer_sizes
from common.logging_utils import setup_logging, format_bytes


# Default configuration
DEFAULT_PORT = 9999
DEFAULT_RECV_BUF = 4096       # Small buffer to trigger zero-window quickly
DEFAULT_NORMAL_DURATION = 5   # Seconds of normal reading before stalling
DEFAULT_STALL_DURATION = 5    # How long to stall (seconds)


def setup_argparse() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Persist Timer Server - stops reading to trigger zero-window",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Basic usage with default settings
    python server.py --port 9999

    # Aggressive stall (smaller buffer, longer stall)
    python server.py --port 9999 --recv-buf 2048 --stall 20

    # Short demo
    python server.py --port 9999 --stall 10

JitterTrap observation:
    - Watch for zero-window events in flow details
    - IPG histogram will show gaps at persist timer intervals
    - Window size will drop to zero, then slowly recover
        """
    )
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"Listen port (default: {DEFAULT_PORT})")
    parser.add_argument("--recv-buf", type=int, default=DEFAULT_RECV_BUF,
                        help=f"SO_RCVBUF size in bytes (default: {DEFAULT_RECV_BUF})")
    parser.add_argument("--normal", type=float, default=DEFAULT_NORMAL_DURATION,
                        help=f"Normal reading duration in seconds (default: {DEFAULT_NORMAL_DURATION})")
    parser.add_argument("--stall", type=float, default=DEFAULT_STALL_DURATION,
                        help=f"Stall duration in seconds (default: {DEFAULT_STALL_DURATION})")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    parser.add_argument("--quiet", "-q", action="store_true",
                        help="Quiet mode (errors only)")
    return parser.parse_args()


class PersistTimerServer:
    """
    TCP server that triggers persist timer by stopping reads.

    Simulates: Application that blocks on slow I/O, database, or computation
    Observable effect: Zero-window events, persist timer probes
    """

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.server_socket = None
        self.client_socket = None
        self.running = True
        self.total_bytes = 0
        self.stall_start = None

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
        logging.info(f"Will read for {self.args.normal}s, then stall for {self.args.stall}s")

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

        logging.info(f"Reading normally for {self.args.normal}s (observe normal traffic in JitterTrap)...")

        try:
            # Phase 1: Read normally for specified duration
            normal_start = time.monotonic()
            normal_end = normal_start + self.args.normal

            while time.monotonic() < normal_end:
                try:
                    data = self.client_socket.recv(8192)
                except socket.timeout:
                    continue
                except ConnectionResetError:
                    logging.warning("Connection reset by client")
                    return 1

                if not data:
                    logging.info("Client disconnected (EOF)")
                    return 1

                self.total_bytes += len(data)

            normal_elapsed = time.monotonic() - normal_start
            logging.info(f"Normal phase: received {format_bytes(self.total_bytes)} in {normal_elapsed:.1f}s")

            # Phase 2: Stop reading - this will cause zero-window
            logging.info(f"STOPPING READS - zero-window will occur")
            logging.info(f"Stalling for {self.args.stall} seconds...")
            logging.info("Watch for zero-window events and persist timer probes in JitterTrap")

            self.stall_start = time.monotonic()
            time.sleep(self.args.stall)
            stall_elapsed = time.monotonic() - self.stall_start

            logging.info(f"Stall complete after {stall_elapsed:.1f}s")

            # Phase 3: Resume reading to drain buffer
            logging.info("Resuming reads to drain buffer...")
            drain_start = time.monotonic()
            drain_bytes = 0

            # Set a timeout so we don't block forever
            self.client_socket.settimeout(2.0)

            while True:
                try:
                    data = self.client_socket.recv(4096)
                    if not data:
                        break
                    drain_bytes += len(data)
                    self.total_bytes += len(data)
                except socket.timeout:
                    break
                except ConnectionResetError:
                    break

            drain_elapsed = time.monotonic() - drain_start
            logging.info(f"Drained {format_bytes(drain_bytes)} in {drain_elapsed:.1f}s")

        finally:
            if self.client_socket:
                self.client_socket.close()
                self.client_socket = None

        # Report results
        logging.info("")
        logging.info("=" * 50)
        logging.info("PERSIST TIMER TEST COMPLETE")
        logging.info("=" * 50)
        logging.info(f"Total received: {format_bytes(self.total_bytes)}")
        logging.info(f"Stall duration: {self.args.stall}s")
        logging.info("")
        logging.info("Expected observations in JitterTrap:")
        logging.info("  - TCP Window chart: drops to zero during stall")
        logging.info("  - Throughput chart: drops to near-zero during stall")
        logging.info("  - Flow details: zero-window events")
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
        server = PersistTimerServer(args)
        return server.run()
    except Exception as e:
        logging.error(f"Error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
