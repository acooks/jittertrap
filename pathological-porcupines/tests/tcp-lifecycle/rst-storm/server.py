#!/usr/bin/env python3
"""
RST Storm Server - Demonstrates abrupt TCP connection termination

Accepts connections and immediately closes them with RST instead of FIN,
simulating abrupt connection termination.

Part of Pathological Porcupines: Network Application Failure Simulations
"""

import argparse
import logging
import socket
import struct
import sys
import time
from pathlib import Path

# Add common utilities path
sys.path.insert(0, str(Path(__file__).parent.parent.parent.parent))
from common.network import create_tcp_socket
from common.logging_utils import setup_logging


# Default configuration
DEFAULT_PORT = 9999
DEFAULT_DURATION = 12  # seconds (slightly longer than client)


def setup_argparse() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="RST Storm Server - closes connections with RST",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Basic server
    python server.py --port 9999

    # Longer duration
    python server.py --port 9999 --duration 20

JitterTrap observation:
    - RST flags visible in flow details
    - Flows terminate abruptly
    - Connection duration very short
        """
    )
    parser.add_argument("--port", "-p", type=int, default=DEFAULT_PORT,
                        help=f"Listen port (default: {DEFAULT_PORT})")
    parser.add_argument("--duration", "-d", type=float, default=DEFAULT_DURATION,
                        help=f"Server duration in seconds (default: {DEFAULT_DURATION})")
    parser.add_argument("--read-first", action="store_true",
                        help="Read some data before RST (creates more realistic scenario)")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    parser.add_argument("--quiet", "-q", action="store_true",
                        help="Quiet mode (errors only)")
    return parser.parse_args()


class RSTStormServer:
    """
    TCP server that terminates connections with RST.

    Uses SO_LINGER with timeout=0 to force RST instead of FIN.
    """

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.server_socket = None
        self.total_connections = 0
        self.rst_count = 0
        self.start_time = None

    def setup(self) -> None:
        """Initialize server socket."""
        self.server_socket = create_tcp_socket()
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind(('0.0.0.0', self.args.port))
        self.server_socket.listen(128)
        self.server_socket.settimeout(1.0)

        logging.info(f"RST Storm server listening on port {self.args.port}")
        logging.info("Will close connections with RST (SO_LINGER 0)")

    def handle_connection(self, client_socket: socket.socket, addr: tuple) -> None:
        """Handle a single connection - close with RST."""
        try:
            self.total_connections += 1

            if self.args.read_first:
                # Optionally read some data first
                try:
                    client_socket.settimeout(0.5)
                    data = client_socket.recv(1024)
                    if data:
                        logging.debug(f"Received {len(data)} bytes from {addr[0]}:{addr[1]}")
                except socket.timeout:
                    pass

            # Set SO_LINGER with timeout=0 to force RST on close
            client_socket.setsockopt(
                socket.SOL_SOCKET,
                socket.SO_LINGER,
                struct.pack('ii', 1, 0)  # linger on, timeout 0
            )

            # Close immediately - this sends RST instead of FIN
            client_socket.close()
            self.rst_count += 1

            logging.debug(f"Sent RST to {addr[0]}:{addr[1]}")

        except OSError as e:
            logging.debug(f"Error handling connection from {addr}: {e}")

    def run(self) -> int:
        """Main execution. Returns exit code."""
        self.setup()
        self.start_time = time.monotonic()
        end_time = self.start_time + self.args.duration
        last_status_time = self.start_time

        logging.info(f"Accepting connections for {self.args.duration}s...")

        try:
            while time.monotonic() < end_time:
                try:
                    client_socket, addr = self.server_socket.accept()
                    self.handle_connection(client_socket, addr)

                except socket.timeout:
                    continue

                # Periodic status
                now = time.monotonic()
                if now - last_status_time >= 2.0:
                    elapsed = now - self.start_time
                    rate = self.total_connections / elapsed
                    logging.info(f"Elapsed: {elapsed:.1f}s, Connections: {self.total_connections} "
                                f"({rate:.1f}/s), RSTs sent: {self.rst_count}")
                    last_status_time = now

        except KeyboardInterrupt:
            logging.info("Interrupted by user")
        finally:
            self.cleanup()

        return self.report_results()

    def report_results(self) -> int:
        """Report test results. Returns exit code."""
        elapsed = time.monotonic() - self.start_time
        rate = self.total_connections / elapsed if elapsed > 0 else 0

        logging.info("")
        logging.info("=" * 50)
        logging.info("RST STORM SERVER RESULTS")
        logging.info("=" * 50)
        logging.info(f"Duration: {elapsed:.1f}s")
        logging.info(f"Total connections: {self.total_connections}")
        logging.info(f"RST packets sent: {self.rst_count}")
        logging.info(f"Connection rate: {rate:.1f}/s")
        logging.info("")

        # Self-checks
        passed = True
        checks = []

        # Check 1: Did we handle connections?
        if self.total_connections >= 10:
            checks.append(("Connections handled", True,
                          f"{self.total_connections} connections"))
        else:
            checks.append(("Connections handled", False,
                          f"Only {self.total_connections} connections"))
            passed = False

        # Check 2: All connections ended with RST?
        if self.rst_count == self.total_connections:
            checks.append(("All RSTs sent", True,
                          f"{self.rst_count} RSTs = {self.total_connections} connections"))
        else:
            checks.append(("All RSTs sent", False,
                          f"{self.rst_count} RSTs < {self.total_connections} connections"))

        # Report checks
        logging.info("Self-check results:")
        for name, result, detail in checks:
            status = "PASS" if result else "FAIL"
            logging.info(f"  [{status}] {name}: {detail}")

        logging.info("")
        logging.info("Expected observations in JitterTrap:")
        logging.info("  - Flows show RST flag")
        logging.info("  - No FIN flags (abrupt termination)")
        logging.info("  - Very short connection durations")
        logging.info("")

        return 0 if passed else 1

    def cleanup(self) -> None:
        """Clean up resources."""
        if self.server_socket:
            self.server_socket.close()
        logging.info("Server shutdown complete")


def main() -> int:
    args = setup_argparse()
    setup_logging(verbose=args.verbose, quiet=args.quiet)

    try:
        server = RSTStormServer(args)
        return server.run()
    except Exception as e:
        logging.error(f"Error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
