#!/usr/bin/env python3
"""
Silly Window Syndrome - Client (Sender)

Sends data to a SWS-susceptible server to demonstrate tiny packet transmission.

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
DEFAULT_HOST = 'localhost'
DEFAULT_PORT = 9999
DEFAULT_DURATION = 30


def setup_argparse() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Silly Window Syndrome Client - sends data to SWS server",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Basic usage
    python client.py --host localhost --port 9999

    # Longer test
    python client.py --port 9999 --duration 60

JitterTrap observation:
    - Watch packet size histogram show very small packets
    - Sender is forced to send tiny segments due to small receiver window
        """
    )
    parser.add_argument("--host", default=DEFAULT_HOST,
                        help=f"Server host (default: {DEFAULT_HOST})")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"Server port (default: {DEFAULT_PORT})")
    parser.add_argument("--duration", type=float, default=DEFAULT_DURATION,
                        help=f"Test duration in seconds (default: {DEFAULT_DURATION})")
    parser.add_argument("--send-buf", type=int, default=None,
                        help="SO_SNDBUF size (default: system default)")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    parser.add_argument("--quiet", "-q", action="store_true",
                        help="Quiet mode (errors only)")
    return parser.parse_args()


class SillyWindowClient:
    """
    TCP client that sends data to demonstrate SWS.

    The sender wants to send large chunks but is limited by the
    receiver's tiny advertised window.
    """

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.socket = None
        self.running = True
        self.total_bytes = 0
        self.send_count = 0

    def setup(self) -> None:
        """Initialize client socket and connect."""
        self.socket = create_tcp_socket(send_buf=self.args.send_buf)

        logging.info(f"Connecting to {self.args.host}:{self.args.port}...")
        self.socket.connect((self.args.host, self.args.port))

        _, send_buf = get_socket_buffer_sizes(self.socket)
        logging.info(f"Connected. Send buffer: {format_bytes(send_buf)}")

    def run(self) -> None:
        """Main execution loop."""
        self.setup()

        # We want to send large chunks, but receiver will limit us
        chunk = b'X' * 65536  # 64KB chunks

        start_time = time.monotonic()
        end_time = start_time + self.args.duration
        last_report_time = start_time
        last_report_bytes = 0
        last_report_sends = 0

        logging.info(f"Starting {self.args.duration}s test (Ctrl+C to stop)")
        logging.info(f"Sending 64KB chunks, but server's tiny window will fragment them")

        try:
            while self.running and time.monotonic() < end_time:
                try:
                    # send() may only send partial data if window is small
                    sent = self.socket.send(chunk)
                    self.total_bytes += sent
                    self.send_count += 1

                except (BrokenPipeError, ConnectionResetError) as e:
                    logging.warning(f"Connection lost: {e}")
                    break

                # Periodic progress report
                now = time.monotonic()
                if now - last_report_time >= 5.0:
                    elapsed = now - last_report_time
                    bytes_interval = self.total_bytes - last_report_bytes
                    sends_interval = self.send_count - last_report_sends

                    avg_send = bytes_interval / sends_interval if sends_interval > 0 else 0
                    throughput = bytes_interval / elapsed

                    logging.info(
                        f"Sent: {format_bytes(self.total_bytes)}, "
                        f"sends: {self.send_count}, "
                        f"avg send: {avg_send:.0f} bytes, "
                        f"throughput: {throughput/1024:.1f} KB/s"
                    )

                    last_report_time = now
                    last_report_bytes = self.total_bytes
                    last_report_sends = self.send_count

        except KeyboardInterrupt:
            logging.info("Interrupted by user")
        finally:
            self.report_final_stats(start_time)
            self.cleanup()

    def report_final_stats(self, start_time: float) -> None:
        """Report final statistics."""
        elapsed = time.monotonic() - start_time
        if elapsed > 0 and self.send_count > 0:
            avg_send = self.total_bytes / self.send_count
            throughput = self.total_bytes / elapsed

            logging.info(f"Test complete:")
            logging.info(f"  Total: {format_bytes(self.total_bytes)} in {elapsed:.1f}s")
            logging.info(f"  Sends: {self.send_count}")
            logging.info(f"  Avg send size: {avg_send:.0f} bytes")
            logging.info(f"  Throughput: {throughput/1024:.1f} KB/s")

            # The interesting metric: how much smaller were sends than requested?
            requested = 65536
            reduction = (1 - avg_send / requested) * 100
            if avg_send < requested:
                logging.info(f"  Window limited sends by {reduction:.0f}% "
                             f"(wanted {requested}, got {avg_send:.0f})")

    def cleanup(self) -> None:
        """Clean up resources."""
        if self.socket:
            self.socket.close()
        logging.info("Client shutdown complete")


def main() -> int:
    args = setup_argparse()
    setup_logging(verbose=args.verbose, quiet=args.quiet)

    try:
        client = SillyWindowClient(args)
        client.run()
        return 0
    except ConnectionRefusedError:
        logging.error(f"Connection refused to {args.host}:{args.port}")
        logging.error("Make sure the server is running first")
        return 1
    except Exception as e:
        logging.error(f"Error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
