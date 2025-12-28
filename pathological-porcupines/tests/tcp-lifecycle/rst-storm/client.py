#!/usr/bin/env python3
"""
RST Storm Client - Connects rapidly to trigger RST responses

Creates many short-lived connections to a server that responds with RST,
demonstrating detection of abrupt connection termination.

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
from common.network import create_tcp_socket
from common.logging_utils import setup_logging


# Default configuration
DEFAULT_HOST = "10.0.1.2"
DEFAULT_PORT = 9999
DEFAULT_DURATION = 10         # seconds
DEFAULT_CONNECT_RATE = 10     # connections per second


def setup_argparse() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="RST Storm Client - triggers RST responses",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Default: 10 connections/second
    python client.py --host 10.0.1.2 --port 9999

    # Faster connection rate
    python client.py --rate 50

    # Slower for easier observation
    python client.py --rate 5 --duration 20

JitterTrap observation:
    - Many flows with RST flag
    - Connection reset errors visible
    - Short-lived connections
        """
    )
    parser.add_argument("--host", "-H", default=DEFAULT_HOST,
                        help=f"Server address (default: {DEFAULT_HOST})")
    parser.add_argument("--port", "-p", type=int, default=DEFAULT_PORT,
                        help=f"Server port (default: {DEFAULT_PORT})")
    parser.add_argument("--duration", "-d", type=float, default=DEFAULT_DURATION,
                        help=f"Test duration in seconds (default: {DEFAULT_DURATION})")
    parser.add_argument("--rate", "-r", type=float, default=DEFAULT_CONNECT_RATE,
                        help=f"Connections per second (default: {DEFAULT_CONNECT_RATE})")
    parser.add_argument("--send-data", action="store_true",
                        help="Send some data before expecting RST")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    parser.add_argument("--quiet", "-q", action="store_true",
                        help="Quiet mode (errors only)")
    return parser.parse_args()


class RSTStormClient:
    """
    TCP client that triggers RST responses by connecting rapidly.

    Tracks connection reset exceptions and RST-terminated connections.
    """

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.total_connections = 0
        self.successful_connects = 0
        self.reset_count = 0
        self.refused_count = 0
        self.timeout_count = 0
        self.other_errors = 0
        self.normal_closes = 0
        self.start_time = None

    def make_connection(self) -> str:
        """
        Make a single connection attempt.

        Returns:
            Result string: 'reset', 'refused', 'timeout', 'normal', 'error'
        """
        sock = None
        try:
            sock = create_tcp_socket(timeout=2.0)
            sock.connect((self.args.host, self.args.port))
            self.successful_connects += 1

            if self.args.send_data:
                # Send some data to trigger the RST after server processes
                try:
                    sock.send(b"Hello, please RST me\n")
                except (ConnectionResetError, BrokenPipeError):
                    return 'reset'

            # Try to receive - this is where we'll likely see the RST
            try:
                data = sock.recv(1024)
                if data:
                    # Server sent data before RST - unusual
                    logging.debug("Received data before close")
                # Empty recv means clean close (FIN) - shouldn't happen with our server
                return 'normal'

            except ConnectionResetError:
                return 'reset'

        except ConnectionResetError:
            return 'reset'

        except ConnectionRefusedError:
            return 'refused'

        except socket.timeout:
            return 'timeout'

        except OSError as e:
            logging.debug(f"Connection error: {e}")
            return 'error'

        finally:
            if sock:
                try:
                    sock.close()
                except Exception:
                    pass

    def run(self) -> int:
        """Main execution. Returns exit code."""
        self.start_time = time.monotonic()
        end_time = self.start_time + self.args.duration

        connect_interval = 1.0 / self.args.rate
        next_connect_time = self.start_time
        last_status_time = self.start_time

        logging.info(f"Connecting to {self.args.host}:{self.args.port}")
        logging.info(f"Rate: {self.args.rate} connections/second")
        logging.info(f"Duration: {self.args.duration}s")
        logging.info("Expecting RST responses from server...")
        logging.info("")

        try:
            while time.monotonic() < end_time:
                now = time.monotonic()

                # Rate limiting
                if now < next_connect_time:
                    time.sleep(next_connect_time - now)
                    now = time.monotonic()

                next_connect_time += connect_interval
                self.total_connections += 1

                # Make connection
                result = self.make_connection()

                if result == 'reset':
                    self.reset_count += 1
                    logging.debug(f"Connection {self.total_connections}: RST received")
                elif result == 'refused':
                    self.refused_count += 1
                    logging.debug(f"Connection {self.total_connections}: refused")
                elif result == 'timeout':
                    self.timeout_count += 1
                    logging.debug(f"Connection {self.total_connections}: timeout")
                elif result == 'normal':
                    self.normal_closes += 1
                    logging.debug(f"Connection {self.total_connections}: normal close (unexpected)")
                else:
                    self.other_errors += 1

                # Periodic status
                if now - last_status_time >= 2.0:
                    elapsed = now - self.start_time
                    rate = self.total_connections / elapsed
                    logging.info(f"Elapsed: {elapsed:.1f}s, Connections: {self.total_connections} ({rate:.1f}/s), "
                                f"RSTs: {self.reset_count}, Normal: {self.normal_closes}")
                    last_status_time = now

        except KeyboardInterrupt:
            logging.info("Interrupted by user")

        return self.report_results()

    def report_results(self) -> int:
        """Report test results. Returns exit code."""
        elapsed = time.monotonic() - self.start_time
        rate = self.total_connections / elapsed if elapsed > 0 else 0

        logging.info("")
        logging.info("=" * 50)
        logging.info("RST STORM CLIENT RESULTS")
        logging.info("=" * 50)
        logging.info(f"Duration: {elapsed:.1f}s")
        logging.info(f"Total connection attempts: {self.total_connections}")
        logging.info(f"Successful connects: {self.successful_connects}")
        logging.info(f"Connection rate: {rate:.1f}/s")
        logging.info("")
        logging.info("Connection outcomes:")
        logging.info(f"  RST received: {self.reset_count}")
        logging.info(f"  Normal close (FIN): {self.normal_closes}")
        logging.info(f"  Connection refused: {self.refused_count}")
        logging.info(f"  Timeout: {self.timeout_count}")
        logging.info(f"  Other errors: {self.other_errors}")
        logging.info("")

        if self.successful_connects > 0:
            rst_rate = self.reset_count / self.successful_connects * 100
            logging.info(f"RST rate: {rst_rate:.1f}% of successful connections")
        logging.info("")

        # Self-checks
        passed = True
        checks = []

        # Check 1: Did we make connections?
        if self.total_connections >= 10:
            checks.append(("Connections made", True,
                          f"{self.total_connections} attempts"))
        else:
            checks.append(("Connections made", False,
                          f"Only {self.total_connections} attempts"))
            passed = False

        # Check 2: Did we receive RSTs?
        if self.reset_count > 0:
            checks.append(("RST received", True, f"{self.reset_count} RSTs"))
        else:
            checks.append(("RST received", False, "No RSTs received"))
            passed = False

        # Check 3: Most connections ended with RST (not FIN)?
        if self.successful_connects > 0:
            if self.reset_count >= self.successful_connects * 0.8:
                checks.append(("RST dominant", True,
                              f"{self.reset_count}/{self.successful_connects} ended with RST"))
            else:
                checks.append(("RST dominant", False,
                              f"Only {self.reset_count}/{self.successful_connects} ended with RST"))
        else:
            checks.append(("RST dominant", False, "No successful connections"))
            passed = False

        # Report checks
        logging.info("Self-check results:")
        for name, result, detail in checks:
            status = "PASS" if result else "FAIL"
            logging.info(f"  [{status}] {name}: {detail}")

        logging.info("")
        logging.info("Observations in JitterTrap should show:")
        logging.info("  - Multiple flows with RST flag set")
        logging.info("  - No FIN flags (abrupt termination)")
        logging.info("  - Short connection durations")
        logging.info("")

        return 0 if passed else 1


def main() -> int:
    args = setup_argparse()
    setup_logging(verbose=args.verbose, quiet=args.quiet)

    try:
        client = RSTStormClient(args)
        return client.run()
    except Exception as e:
        logging.error(f"Error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
