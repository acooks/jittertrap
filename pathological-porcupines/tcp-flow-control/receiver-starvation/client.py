#!/usr/bin/env python3
"""
Receiver Starvation - Client (Sender)

Sends data to a slow receiver to trigger TCP zero-window events.

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
from common.logging_utils import setup_logging, format_bytes, format_rate


# Default configuration
DEFAULT_HOST = 'localhost'
DEFAULT_PORT = 9999
DEFAULT_RATE = 10.0           # Target MB/s
DEFAULT_DURATION = 30         # Seconds
DEFAULT_CHUNK_SIZE = 65536    # 64KB chunks


def setup_argparse() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Receiver Starvation Client - sends data to trigger zero-window",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Basic usage
    python client.py --host localhost --port 9999

    # High throughput to stress receiver faster
    python client.py --port 9999 --rate 100

    # Long duration test
    python client.py --port 9999 --duration 120

JitterTrap observation:
    - Watch for zero-window events when receiver can't keep up
    - RTT will increase as sender's buffer fills
    - Throughput will show sawtooth pattern
        """
    )
    parser.add_argument("--host", default=DEFAULT_HOST,
                        help=f"Server host (default: {DEFAULT_HOST})")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"Server port (default: {DEFAULT_PORT})")
    parser.add_argument("--rate", type=float, default=DEFAULT_RATE,
                        help=f"Target throughput in MB/s (default: {DEFAULT_RATE})")
    parser.add_argument("--duration", type=float, default=DEFAULT_DURATION,
                        help=f"Test duration in seconds (default: {DEFAULT_DURATION})")
    parser.add_argument("--chunk-size", type=int, default=DEFAULT_CHUNK_SIZE,
                        help=f"Chunk size for sends (default: {DEFAULT_CHUNK_SIZE})")
    parser.add_argument("--nodelay", action="store_true",
                        help="Enable TCP_NODELAY (disable Nagle)")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    parser.add_argument("--quiet", "-q", action="store_true",
                        help="Quiet mode (errors only)")
    return parser.parse_args()


class ReceiverStarvationClient:
    """
    TCP client that sends data to stress a slow receiver.

    Sends continuous data at specified rate to trigger zero-window
    events on the server side.
    """

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.socket = None
        self.running = True
        self.total_bytes = 0
        self.blocked_time = 0.0
        self.block_count = 0

    def setup(self) -> None:
        """Initialize client socket and connect."""
        self.socket = create_tcp_socket(nodelay=self.args.nodelay)

        logging.info(f"Connecting to {self.args.host}:{self.args.port}...")
        self.socket.connect((self.args.host, self.args.port))

        recv_buf, send_buf = get_socket_buffer_sizes(self.socket)
        logging.info(f"Connected. Send buffer: {format_bytes(send_buf)}")
        logging.info(f"Target rate: {format_rate(self.args.rate * 1024 * 1024)}")

    def send_data(self) -> int:
        """
        Send a chunk of data.

        Returns:
            Bytes sent (may block if receiver is slow)
        """
        # Pre-generate data chunk
        data = b'X' * self.args.chunk_size

        start = time.monotonic()
        try:
            sent = self.socket.send(data)
        except (BrokenPipeError, ConnectionResetError) as e:
            logging.warning(f"Connection lost: {e}")
            self.running = False
            return 0
        elapsed = time.monotonic() - start

        # Track blocking (indicates receiver is slow)
        if elapsed > 0.1:  # More than 100ms is considered blocked
            self.blocked_time += elapsed
            self.block_count += 1
            logging.debug(f"Send blocked for {elapsed*1000:.0f}ms "
                          f"(receiver likely zero-window)")

        return sent

    def run(self) -> None:
        """Main execution loop."""
        self.setup()

        target_bytes_per_sec = self.args.rate * 1024 * 1024
        target_interval = self.args.chunk_size / target_bytes_per_sec

        start_time = time.monotonic()
        end_time = start_time + self.args.duration
        last_report_time = start_time
        last_report_bytes = 0
        next_send_time = start_time

        logging.info(f"Starting {self.args.duration}s test (Ctrl+C to stop)")
        logging.info(f"Sending {format_bytes(self.args.chunk_size)} chunks "
                     f"to achieve {format_rate(target_bytes_per_sec)}")

        try:
            while self.running and time.monotonic() < end_time:
                # Rate limiting
                now = time.monotonic()
                if now < next_send_time:
                    time.sleep(next_send_time - now)
                next_send_time = time.monotonic() + target_interval

                # Send data
                sent = self.send_data()
                if sent == 0:
                    break
                self.total_bytes += sent

                # Periodic progress report
                now = time.monotonic()
                if now - last_report_time >= 5.0:
                    elapsed = now - last_report_time
                    bytes_interval = self.total_bytes - last_report_bytes
                    rate = bytes_interval / elapsed

                    # Try to get TCP info (Linux only)
                    tcp_info = get_tcp_info(self.socket)
                    retrans = tcp_info.get('retransmits', 'N/A') if tcp_info else 'N/A'
                    unacked = tcp_info.get('unacked', 'N/A') if tcp_info else 'N/A'

                    logging.info(
                        f"Sent: {format_bytes(self.total_bytes)} total, "
                        f"rate: {format_rate(rate)}, "
                        f"blocks: {self.block_count}, "
                        f"retrans: {retrans}, unacked: {unacked}"
                    )

                    last_report_time = now
                    last_report_bytes = self.total_bytes

        except KeyboardInterrupt:
            logging.info("Interrupted by user")
        finally:
            self.report_final_stats(start_time)
            self.cleanup()

    def report_final_stats(self, start_time: float) -> None:
        """Report final statistics."""
        elapsed = time.monotonic() - start_time
        if elapsed > 0:
            avg_rate = self.total_bytes / elapsed
            logging.info(f"Test complete: {format_bytes(self.total_bytes)} "
                         f"in {elapsed:.1f}s ({format_rate(avg_rate)} avg)")
            logging.info(f"Blocked {self.block_count} times, "
                         f"total blocked time: {self.blocked_time:.2f}s "
                         f"({self.blocked_time/elapsed*100:.1f}% of test)")

    def cleanup(self) -> None:
        """Clean up resources."""
        if self.socket:
            self.socket.close()
        logging.info("Client shutdown complete")


def main() -> int:
    args = setup_argparse()
    setup_logging(verbose=args.verbose, quiet=args.quiet)

    try:
        client = ReceiverStarvationClient(args)
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
