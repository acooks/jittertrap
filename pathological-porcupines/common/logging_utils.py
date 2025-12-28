#!/usr/bin/env python3
"""
Logging utilities for Pathological Porcupines.

Consistent logging setup across all pathology simulations.
Standard library only - no external dependencies.
"""

import logging
import sys
from typing import Optional


# Default format includes timestamp, level, and message
DEFAULT_FORMAT = '%(asctime)s.%(msecs)03d %(levelname)-8s %(message)s'
DEFAULT_DATE_FORMAT = '%H:%M:%S'

# Verbose format includes module/function for debugging
VERBOSE_FORMAT = '%(asctime)s.%(msecs)03d %(levelname)-8s [%(module)s:%(funcName)s:%(lineno)d] %(message)s'


def setup_logging(
    verbose: bool = False,
    debug: bool = False,
    quiet: bool = False,
    log_file: Optional[str] = None,
) -> None:
    """
    Configure logging for pathology simulations.

    Args:
        verbose: Use verbose format with module/function info
        debug: Set log level to DEBUG
        quiet: Set log level to WARNING (suppress INFO)
        log_file: Optional file to write logs to

    Example:
        # Basic setup
        setup_logging()
        logging.info("Starting simulation")

        # Debug mode
        setup_logging(debug=True)

        # Quiet mode (warnings and errors only)
        setup_logging(quiet=True)

        # Log to file
        setup_logging(log_file="simulation.log")
    """
    # Determine log level
    if debug:
        level = logging.DEBUG
    elif quiet:
        level = logging.WARNING
    else:
        level = logging.INFO

    # Determine format
    fmt = VERBOSE_FORMAT if verbose else DEFAULT_FORMAT

    # Configure root logger
    handlers = []

    # Console handler
    console_handler = logging.StreamHandler(sys.stderr)
    console_handler.setFormatter(logging.Formatter(fmt, DEFAULT_DATE_FORMAT))
    handlers.append(console_handler)

    # File handler (if specified)
    if log_file:
        file_handler = logging.FileHandler(log_file)
        file_handler.setFormatter(logging.Formatter(fmt, DEFAULT_DATE_FORMAT))
        handlers.append(file_handler)

    # Apply configuration
    logging.basicConfig(
        level=level,
        handlers=handlers,
        force=True,  # Override any existing configuration
    )


def get_logger(name: str) -> logging.Logger:
    """
    Get a logger instance with the given name.

    Args:
        name: Logger name (typically __name__)

    Returns:
        Logger instance

    Example:
        logger = get_logger(__name__)
        logger.info("Processing packet %d", seq)
    """
    return logging.getLogger(name)


class StatsLogger:
    """
    Helper for logging periodic statistics.

    Accumulates stats and logs summary at intervals.

    Example:
        stats = StatsLogger(interval_seconds=5)
        while running:
            send_packet()
            stats.count('packets_sent')
            stats.count('bytes_sent', len(packet))
            stats.log_if_due()
    """

    def __init__(
        self,
        interval_seconds: float = 10.0,
        logger: Optional[logging.Logger] = None,
    ):
        """
        Initialize stats logger.

        Args:
            interval_seconds: Interval between log outputs
            logger: Logger to use (default: root logger)
        """
        self.interval = interval_seconds
        self.logger = logger or logging.getLogger()
        self.counters: dict = {}
        self.gauges: dict = {}
        self.last_log_time: Optional[float] = None

    def count(self, name: str, value: int = 1) -> None:
        """
        Increment a counter.

        Args:
            name: Counter name
            value: Amount to add (default 1)
        """
        self.counters[name] = self.counters.get(name, 0) + value

    def gauge(self, name: str, value: float) -> None:
        """
        Set a gauge value (last value wins).

        Args:
            name: Gauge name
            value: Current value
        """
        self.gauges[name] = value

    def log_if_due(self) -> bool:
        """
        Log stats if interval has elapsed.

        Returns:
            True if stats were logged
        """
        import time

        now = time.monotonic()

        if self.last_log_time is None:
            self.last_log_time = now
            return False

        elapsed = now - self.last_log_time
        if elapsed < self.interval:
            return False

        # Format and log stats
        parts = []

        # Counters as rates
        for name, value in sorted(self.counters.items()):
            rate = value / elapsed
            if rate >= 1000000:
                parts.append(f"{name}={rate/1000000:.2f}M/s")
            elif rate >= 1000:
                parts.append(f"{name}={rate/1000:.2f}K/s")
            else:
                parts.append(f"{name}={rate:.1f}/s")

        # Gauges as current values
        for name, value in sorted(self.gauges.items()):
            parts.append(f"{name}={value:.2f}")

        if parts:
            self.logger.info("Stats: %s", ", ".join(parts))

        # Reset for next interval
        self.counters.clear()
        self.gauges.clear()
        self.last_log_time = now

        return True

    def reset(self) -> None:
        """Reset all counters and gauges."""
        self.counters.clear()
        self.gauges.clear()
        self.last_log_time = None


def format_bytes(num_bytes: int) -> str:
    """
    Format byte count in human-readable form.

    Args:
        num_bytes: Number of bytes

    Returns:
        Formatted string (e.g., "1.5 MB")
    """
    for unit in ['B', 'KB', 'MB', 'GB', 'TB']:
        if abs(num_bytes) < 1024.0:
            return f"{num_bytes:.1f} {unit}"
        num_bytes /= 1024.0
    return f"{num_bytes:.1f} PB"


def format_rate(bytes_per_sec: float) -> str:
    """
    Format byte rate in human-readable form.

    Args:
        bytes_per_sec: Bytes per second

    Returns:
        Formatted string (e.g., "10.5 Mbps")
    """
    bits_per_sec = bytes_per_sec * 8

    for unit in ['bps', 'Kbps', 'Mbps', 'Gbps']:
        if abs(bits_per_sec) < 1000.0:
            return f"{bits_per_sec:.1f} {unit}"
        bits_per_sec /= 1000.0
    return f"{bits_per_sec:.1f} Tbps"


def format_duration(seconds: float) -> str:
    """
    Format duration in human-readable form.

    Args:
        seconds: Duration in seconds

    Returns:
        Formatted string (e.g., "2m 30s")
    """
    if seconds < 1:
        return f"{seconds*1000:.0f}ms"
    elif seconds < 60:
        return f"{seconds:.1f}s"
    elif seconds < 3600:
        minutes = int(seconds // 60)
        secs = seconds % 60
        return f"{minutes}m {secs:.0f}s"
    else:
        hours = int(seconds // 3600)
        minutes = int((seconds % 3600) // 60)
        return f"{hours}h {minutes}m"
