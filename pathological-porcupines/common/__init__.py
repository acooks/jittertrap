"""
Pathological Porcupines - Common Utilities

Shared utilities for network pathology simulations.
All modules use only Python 3.10+ standard library.
"""

from .network import create_tcp_socket, create_udp_socket, parse_address
from .timing import rate_limiter, sleep_until, monotonic_ns
from .protocol import RTPPacket, parse_rtp_header
from .logging_utils import setup_logging, get_logger

__all__ = [
    'create_tcp_socket',
    'create_udp_socket',
    'parse_address',
    'rate_limiter',
    'sleep_until',
    'monotonic_ns',
    'RTPPacket',
    'parse_rtp_header',
    'setup_logging',
    'get_logger',
]
