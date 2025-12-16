#!/usr/bin/env python3
"""
Nagle-Delayed ACK Interaction - Client

Sends small writes to demonstrate Nagle/delayed ACK latency interaction.

Part of Pathological Porcupines: Network Application Failure Simulations
"""

import argparse
import logging
import socket
import sys
import time
import statistics
from pathlib import Path

# Add common utilities path
sys.path.insert(0, str(Path(__file__).parent.parent.parent))
from common.network import create_tcp_socket
from common.logging_utils import setup_logging


# Default configuration
DEFAULT_HOST = 'localhost'
DEFAULT_PORT = 9999
DEFAULT_WRITE_SIZE = 100      # Small write to trigger Nagle
DEFAULT_WRITES = 1000         # Number of writes
DEFAULT_INTERVAL = 0.01       # 10ms between write pairs


def setup_argparse() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Nagle-Delayed ACK Client - demonstrates latency from interaction",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Default: shows Nagle/delayed ACK interaction (~40ms delays)
    python client.py --port 9999

    # With TCP_NODELAY: no Nagle, no delayed ACK interaction
    python client.py --port 9999 --nodelay

    # Larger writes (bypass Nagle due to MSS)
    python client.py --port 9999 --write-size 1500

JitterTrap observation:
    - Without --nodelay: RTT histogram shows ~40ms peak (delayed ACK)
    - With --nodelay: RTT histogram shows low-latency only
        """
    )
    parser.add_argument("--host", default=DEFAULT_HOST,
                        help=f"Server host (default: {DEFAULT_HOST})")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"Server port (default: {DEFAULT_PORT})")
    parser.add_argument("--nodelay", action="store_true",
                        help="Set TCP_NODELAY (disables Nagle algorithm)")
    parser.add_argument("--write-size", type=int, default=DEFAULT_WRITE_SIZE,
                        help=f"Bytes per write (default: {DEFAULT_WRITE_SIZE})")
    parser.add_argument("--writes", type=int, default=DEFAULT_WRITES,
                        help=f"Number of writes (default: {DEFAULT_WRITES})")
    parser.add_argument("--interval", type=float, default=DEFAULT_INTERVAL,
                        help=f"Seconds between write pairs (default: {DEFAULT_INTERVAL})")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    parser.add_argument("--quiet", "-q", action="store_true",
                        help="Quiet mode (errors only)")
    return parser.parse_args()


class NagleDelayedAckClient:
    """
    TCP client that demonstrates Nagle/delayed ACK interaction.

    Sends small writes which, without TCP_NODELAY, trigger Nagle's
    algorithm. Combined with server's delayed ACKs, this causes
    significant latency spikes.
    """

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.socket = None
        self.write_latencies = []

    def setup(self) -> None:
        """Initialize client socket and connect."""
        self.socket = create_tcp_socket(nodelay=self.args.nodelay)

        logging.info(f"Connecting to {self.args.host}:{self.args.port}...")
        self.socket.connect((self.args.host, self.args.port))

        nagle_status = "DISABLED (TCP_NODELAY)" if self.args.nodelay else "ENABLED"
        logging.info(f"Connected. Nagle algorithm: {nagle_status}")
        logging.info(f"Write size: {self.args.write_size} bytes")

        if not self.args.nodelay and self.args.write_size < 1460:
            logging.warning("Small writes with Nagle enabled will trigger delayed ACK interaction!")
            logging.warning("Expect ~40ms latency spikes on Linux (up to 200ms on other platforms)")

    def measure_write_latency(self, data: bytes) -> float:
        """
        Send data and measure time until send() returns.

        Note: This doesn't directly measure ACK time, but send() will
        block when the send buffer fills due to Nagle buffering.

        Returns:
            Latency in milliseconds
        """
        start = time.perf_counter()
        self.socket.send(data)
        elapsed = time.perf_counter() - start
        return elapsed * 1000  # Convert to ms

    def run(self) -> None:
        """Main execution loop."""
        self.setup()

        data = b'X' * self.args.write_size
        self.write_latencies = []

        logging.info(f"Sending {self.args.writes} writes of {self.args.write_size} bytes...")
        logging.info("(Measuring send() latency to detect Nagle buffering)")

        try:
            for i in range(self.args.writes):
                # Send first write of pair
                latency1 = self.measure_write_latency(data)
                self.write_latencies.append(latency1)

                # Send second write (this one triggers the Nagle/delayed ACK issue)
                latency2 = self.measure_write_latency(data)
                self.write_latencies.append(latency2)

                # If latency is high, it's likely Nagle waiting for ACK
                if latency2 > 10:  # > 10ms is suspicious
                    logging.debug(f"Write {i*2+1}: {latency2:.1f}ms (delayed ACK?)")

                # Small delay between write pairs
                if self.args.interval > 0:
                    time.sleep(self.args.interval)

                # Progress indicator
                if (i + 1) % 100 == 0:
                    logging.info(f"Progress: {i+1}/{self.args.writes} write pairs")

        except (BrokenPipeError, ConnectionResetError) as e:
            logging.warning(f"Connection lost: {e}")
        finally:
            self.report_stats()
            self.cleanup()

    def report_stats(self) -> None:
        """Report latency statistics."""
        if not self.write_latencies:
            return

        latencies = self.write_latencies

        # Basic stats
        avg = statistics.mean(latencies)
        median = statistics.median(latencies)
        stdev = statistics.stdev(latencies) if len(latencies) > 1 else 0

        # Percentiles
        sorted_latencies = sorted(latencies)
        p50 = sorted_latencies[len(sorted_latencies) * 50 // 100]
        p95 = sorted_latencies[len(sorted_latencies) * 95 // 100]
        p99 = sorted_latencies[len(sorted_latencies) * 99 // 100]
        max_lat = max(latencies)
        min_lat = min(latencies)

        logging.info("=" * 60)
        logging.info("Write Latency Statistics:")
        logging.info(f"  Count:  {len(latencies)}")
        logging.info(f"  Mean:   {avg:.2f}ms")
        logging.info(f"  Median: {median:.2f}ms")
        logging.info(f"  Stdev:  {stdev:.2f}ms")
        logging.info(f"  Min:    {min_lat:.2f}ms")
        logging.info(f"  Max:    {max_lat:.2f}ms")
        logging.info(f"  p50:    {p50:.2f}ms")
        logging.info(f"  p95:    {p95:.2f}ms")
        logging.info(f"  p99:    {p99:.2f}ms")

        # Count high-latency writes (> 30ms, likely delayed ACK)
        high_latency = [l for l in latencies if l > 30]
        high_pct = len(high_latency) / len(latencies) * 100

        if high_latency:
            logging.info(f"  High latency (>30ms): {len(high_latency)} ({high_pct:.1f}%)")
            logging.info("=" * 60)

            if not self.args.nodelay:
                logging.info("DIAGNOSIS: High latency writes detected.")
                logging.info("This is the Nagle/Delayed ACK interaction.")
                logging.info("Run with --nodelay to fix.")
        else:
            logging.info("=" * 60)
            if self.args.nodelay:
                logging.info("No high-latency writes detected (TCP_NODELAY working)")
            else:
                logging.info("No high-latency writes detected (unusual without TCP_NODELAY)")

    def cleanup(self) -> None:
        """Clean up resources."""
        if self.socket:
            self.socket.close()
        logging.info("Client shutdown complete")


def main() -> int:
    args = setup_argparse()
    setup_logging(verbose=args.verbose, quiet=args.quiet)

    try:
        client = NagleDelayedAckClient(args)
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
