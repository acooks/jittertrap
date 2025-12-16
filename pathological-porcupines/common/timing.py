#!/usr/bin/env python3
"""
Timing utilities for Pathological Porcupines.

Rate limiting, precise timing, and scheduling utilities.
Standard library only - no external dependencies.
"""

import time
from typing import Generator, Optional


def monotonic_ns() -> int:
    """
    Get monotonic time in nanoseconds.

    Returns:
        Current monotonic time in nanoseconds
    """
    return time.monotonic_ns()


def monotonic_us() -> int:
    """
    Get monotonic time in microseconds.

    Returns:
        Current monotonic time in microseconds
    """
    return time.monotonic_ns() // 1000


def monotonic_ms() -> int:
    """
    Get monotonic time in milliseconds.

    Returns:
        Current monotonic time in milliseconds
    """
    return time.monotonic_ns() // 1_000_000


def sleep_until(target_ns: int) -> None:
    """
    Sleep until a specific monotonic time.

    Args:
        target_ns: Target time in nanoseconds (from monotonic_ns())

    Note:
        If target_ns is in the past, returns immediately.
    """
    now = time.monotonic_ns()
    if target_ns > now:
        time.sleep((target_ns - now) / 1_000_000_000)


def sleep_us(microseconds: int) -> None:
    """
    Sleep for a specified number of microseconds.

    Args:
        microseconds: Duration to sleep
    """
    if microseconds > 0:
        time.sleep(microseconds / 1_000_000)


def sleep_ms(milliseconds: int) -> None:
    """
    Sleep for a specified number of milliseconds.

    Args:
        milliseconds: Duration to sleep
    """
    if milliseconds > 0:
        time.sleep(milliseconds / 1000)


class RateLimiter:
    """
    Token bucket rate limiter for controlling packet/byte send rates.

    Example:
        # Limit to 1000 packets per second
        limiter = RateLimiter(rate=1000, burst=10)
        for packet in packets:
            limiter.wait()
            send(packet)

        # Limit to 1 Mbps
        limiter = RateLimiter(rate=1_000_000/8, burst=1500)  # bytes per second
        for packet in packets:
            limiter.wait(len(packet))
            send(packet)
    """

    def __init__(self, rate: float, burst: Optional[float] = None):
        """
        Initialize rate limiter.

        Args:
            rate: Rate limit (units per second)
            burst: Maximum burst size (default: rate / 10)
        """
        self.rate = rate
        self.burst = burst if burst is not None else rate / 10
        self.tokens = self.burst
        self.last_update = time.monotonic()

    def _update_tokens(self) -> None:
        """Update token count based on elapsed time."""
        now = time.monotonic()
        elapsed = now - self.last_update
        self.tokens = min(self.burst, self.tokens + elapsed * self.rate)
        self.last_update = now

    def wait(self, tokens: float = 1.0) -> None:
        """
        Wait until tokens are available, then consume them.

        Args:
            tokens: Number of tokens to consume (default 1)
        """
        self._update_tokens()

        if self.tokens >= tokens:
            self.tokens -= tokens
            return

        # Need to wait for more tokens
        needed = tokens - self.tokens
        wait_time = needed / self.rate
        time.sleep(wait_time)
        self.tokens = 0

    def try_acquire(self, tokens: float = 1.0) -> bool:
        """
        Try to acquire tokens without waiting.

        Args:
            tokens: Number of tokens to acquire

        Returns:
            True if tokens were acquired, False otherwise
        """
        self._update_tokens()

        if self.tokens >= tokens:
            self.tokens -= tokens
            return True
        return False


def rate_limiter(rate: float, burst: Optional[float] = None) -> Generator[None, None, None]:
    """
    Generator-based rate limiter.

    Args:
        rate: Events per second
        burst: Maximum burst (default: rate / 10)

    Yields:
        None (just controls timing)

    Example:
        # Send 100 packets per second
        for _ in rate_limiter(100):
            send_packet()
            if done:
                break
    """
    limiter = RateLimiter(rate, burst)
    while True:
        limiter.wait()
        yield


class IntervalTimer:
    """
    Timer that fires at regular intervals, catching up if behind.

    Useful for maintaining consistent timing even when processing
    takes variable time.

    Example:
        timer = IntervalTimer(interval_ms=33.33)  # ~30 fps
        while running:
            process_frame()
            timer.wait()  # Maintains 30 fps regardless of processing time
    """

    def __init__(self, interval_ms: float):
        """
        Initialize interval timer.

        Args:
            interval_ms: Interval between ticks in milliseconds
        """
        self.interval_ns = int(interval_ms * 1_000_000)
        self.next_tick = time.monotonic_ns()

    def wait(self) -> int:
        """
        Wait until next interval tick.

        Returns:
            Number of missed ticks (0 if on time)
        """
        self.next_tick += self.interval_ns
        now = time.monotonic_ns()

        if now >= self.next_tick:
            # We're behind - count missed ticks
            missed = (now - self.next_tick) // self.interval_ns
            self.next_tick = now + self.interval_ns
            return missed + 1

        # Wait for next tick
        sleep_until(self.next_tick)
        return 0

    def reset(self) -> None:
        """Reset timer to start fresh from now."""
        self.next_tick = time.monotonic_ns()


class BurstTimer:
    """
    Timer for generating bursty traffic patterns.

    Sends bursts of packets, then pauses.

    Example:
        # Send 10 packets every 100ms
        timer = BurstTimer(burst_size=10, burst_interval_ms=100, packet_interval_us=100)
        for packet_num in timer:
            send_packet()
            if packet_num > 1000:
                break
    """

    def __init__(
        self,
        burst_size: int,
        burst_interval_ms: float,
        packet_interval_us: float = 0,
    ):
        """
        Initialize burst timer.

        Args:
            burst_size: Number of packets per burst
            burst_interval_ms: Time between burst starts
            packet_interval_us: Time between packets within burst (0 = line rate)
        """
        self.burst_size = burst_size
        self.burst_interval_ns = int(burst_interval_ms * 1_000_000)
        self.packet_interval_ns = int(packet_interval_us * 1000)
        self.next_burst = time.monotonic_ns()
        self.packet_count = 0

    def __iter__(self):
        return self

    def __next__(self) -> int:
        """
        Get next packet timing.

        Returns:
            Packet number (0-indexed)
        """
        packet_in_burst = self.packet_count % self.burst_size

        if packet_in_burst == 0:
            # Start of new burst - wait for burst interval
            sleep_until(self.next_burst)
            self.next_burst = time.monotonic_ns() + self.burst_interval_ns
        elif self.packet_interval_ns > 0:
            # Within burst - wait for packet interval
            sleep_us(self.packet_interval_ns // 1000)

        result = self.packet_count
        self.packet_count += 1
        return result
