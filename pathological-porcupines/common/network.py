#!/usr/bin/env python3
"""
Network utilities for Pathological Porcupines.

Socket creation and address parsing utilities.
Standard library only - no external dependencies.
"""

import socket
from typing import Tuple, Optional


def create_tcp_socket(
    reuse_addr: bool = True,
    recv_buf: Optional[int] = None,
    send_buf: Optional[int] = None,
    nodelay: bool = False,
    timeout: Optional[float] = None,
    keepalive: bool = False,
    keepalive_idle: Optional[int] = None,
    keepalive_interval: Optional[int] = None,
    keepalive_count: Optional[int] = None,
    linger: Optional[Tuple[bool, int]] = None,
) -> socket.socket:
    """
    Create a TCP socket with common options.

    Args:
        reuse_addr: Set SO_REUSEADDR (default True)
        recv_buf: SO_RCVBUF size in bytes (None = system default)
        send_buf: SO_SNDBUF size in bytes (None = system default)
        nodelay: Set TCP_NODELAY to disable Nagle algorithm
        timeout: Socket timeout in seconds (None = blocking)
        keepalive: Enable SO_KEEPALIVE
        keepalive_idle: Seconds before first keepalive probe (Linux TCP_KEEPIDLE)
        keepalive_interval: Seconds between keepalive probes (Linux TCP_KEEPINTVL)
        keepalive_count: Failed probes before connection is dead (Linux TCP_KEEPCNT)
        linger: Tuple of (on/off, timeout_seconds) for SO_LINGER

    Returns:
        Configured TCP socket

    Example:
        # Simple server socket
        sock = create_tcp_socket()
        sock.bind(('0.0.0.0', 8080))
        sock.listen(5)

        # Socket for zero-window simulation (small receive buffer)
        sock = create_tcp_socket(recv_buf=4096)

        # Low-latency socket (Nagle disabled)
        sock = create_tcp_socket(nodelay=True)

        # Socket that sends RST on close
        sock = create_tcp_socket(linger=(True, 0))
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    if reuse_addr:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    if recv_buf is not None:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, recv_buf)

    if send_buf is not None:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, send_buf)

    if nodelay:
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    if timeout is not None:
        sock.settimeout(timeout)

    if keepalive:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)

        # Linux-specific keepalive parameters
        if keepalive_idle is not None:
            try:
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPIDLE, keepalive_idle)
            except (AttributeError, OSError):
                pass  # Not available on this platform

        if keepalive_interval is not None:
            try:
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPINTVL, keepalive_interval)
            except (AttributeError, OSError):
                pass

        if keepalive_count is not None:
            try:
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPCNT, keepalive_count)
            except (AttributeError, OSError):
                pass

    if linger is not None:
        import struct
        on, timeout_sec = linger
        sock.setsockopt(
            socket.SOL_SOCKET,
            socket.SO_LINGER,
            struct.pack('ii', 1 if on else 0, timeout_sec)
        )

    return sock


def create_udp_socket(
    reuse_addr: bool = True,
    recv_buf: Optional[int] = None,
    send_buf: Optional[int] = None,
    timeout: Optional[float] = None,
    broadcast: bool = False,
    dont_fragment: bool = False,
) -> socket.socket:
    """
    Create a UDP socket with common options.

    Args:
        reuse_addr: Set SO_REUSEADDR (default True)
        recv_buf: SO_RCVBUF size in bytes
        send_buf: SO_SNDBUF size in bytes
        timeout: Socket timeout in seconds
        broadcast: Enable SO_BROADCAST
        dont_fragment: Set IP_MTU_DISCOVER to IP_PMTUDISC_DO (Linux)

    Returns:
        Configured UDP socket

    Example:
        # Simple UDP socket
        sock = create_udp_socket()
        sock.bind(('0.0.0.0', 9999))

        # Socket for PMTU testing
        sock = create_udp_socket(dont_fragment=True)
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    if reuse_addr:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    if recv_buf is not None:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, recv_buf)

    if send_buf is not None:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, send_buf)

    if timeout is not None:
        sock.settimeout(timeout)

    if broadcast:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    if dont_fragment:
        # Linux-specific: IP_MTU_DISCOVER = 10, IP_PMTUDISC_DO = 2
        try:
            IP_MTU_DISCOVER = 10
            IP_PMTUDISC_DO = 2
            sock.setsockopt(socket.IPPROTO_IP, IP_MTU_DISCOVER, IP_PMTUDISC_DO)
        except OSError:
            pass  # Not available on this platform

    return sock


def parse_address(addr_str: str, default_port: int = 5000) -> Tuple[str, int]:
    """
    Parse 'host:port' or just 'host' string into (host, port) tuple.

    Args:
        addr_str: Address string like "localhost:8080" or "192.168.1.1"
        default_port: Port to use if not specified

    Returns:
        Tuple of (host, port)

    Example:
        >>> parse_address("localhost:8080")
        ('localhost', 8080)
        >>> parse_address("192.168.1.1", default_port=9999)
        ('192.168.1.1', 9999)
    """
    if ':' in addr_str:
        host, port_str = addr_str.rsplit(':', 1)
        return host, int(port_str)
    return addr_str, default_port


def get_socket_buffer_sizes(sock: socket.socket) -> Tuple[int, int]:
    """
    Get actual socket buffer sizes (may differ from requested).

    Args:
        sock: Socket to query

    Returns:
        Tuple of (recv_buf_size, send_buf_size)
    """
    recv_buf = sock.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
    send_buf = sock.getsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF)
    return recv_buf, send_buf


def get_tcp_info(sock: socket.socket) -> Optional[dict]:
    """
    Get TCP connection info (Linux-specific).

    Returns dict with RTT, cwnd, etc. or None if not available.
    """
    try:
        import struct
        TCP_INFO = 11  # Linux-specific
        info = sock.getsockopt(socket.IPPROTO_TCP, TCP_INFO, 104)

        # Parse first few fields of tcp_info struct
        # See: /usr/include/linux/tcp.h
        fields = struct.unpack('BBBBIIIIIIIII', info[:52])
        return {
            'state': fields[0],
            'ca_state': fields[1],
            'retransmits': fields[2],
            'probes': fields[3],
            'backoff': fields[4],
            'options': fields[5],
            'snd_wscale': (fields[6] >> 4) & 0xf,
            'rcv_wscale': fields[6] & 0xf,
            'rto': fields[7],  # microseconds
            'ato': fields[8],
            'snd_mss': fields[9],
            'rcv_mss': fields[10],
            'unacked': fields[11],
            'sacked': fields[12],
        }
    except (OSError, struct.error):
        return None
