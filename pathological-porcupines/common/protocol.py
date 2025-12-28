#!/usr/bin/env python3
"""
Protocol utilities for Pathological Porcupines.

RTP packet construction and parsing.
Standard library only - no external dependencies.
"""

import struct
import time
import random
from typing import Tuple, Optional


class RTPPacket:
    """
    RTP packet builder per RFC 3550.

    Header format (12 bytes minimum):
     0                   1                   2                   3
     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |V=2|P|X|  CC   |M|     PT      |       sequence number         |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                           timestamp                           |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |           synchronization source (SSRC) identifier            |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    Example:
        # Create H.264 video RTP stream
        rtp = RTPPacket(payload_type=96, clock_rate=90000)
        packet = rtp.build(payload=h264_nal_unit, marker=is_last_fragment)
        socket.sendto(packet, dest)

        # Simulate jitter by manipulating timestamps
        rtp.set_timestamp(rtp.timestamp + random.randint(-1000, 1000))
        packet = rtp.build(payload)
    """

    VERSION = 2

    # Common payload types (static)
    PT_PCMU = 0       # G.711 u-law audio (8kHz)
    PT_PCMA = 8       # G.711 A-law audio (8kHz)
    PT_G722 = 9       # G.722 audio (8kHz)
    PT_L16_STEREO = 10  # Linear PCM stereo (44.1kHz)
    PT_L16_MONO = 11    # Linear PCM mono (44.1kHz)
    PT_MPA = 14       # MPEG audio
    PT_G728 = 15      # G.728 audio
    PT_G729 = 18      # G.729 audio
    PT_JPEG = 26      # JPEG video
    PT_H261 = 31      # H.261 video
    PT_MPV = 32       # MPEG-1/2 video
    PT_MP2T = 33      # MPEG-2 TS

    # Dynamic payload types (96-127, negotiated via SDP)
    PT_H264 = 96      # H.264/AVC video (typical)
    PT_H265 = 97      # H.265/HEVC video (typical)
    PT_VP8 = 98       # VP8 video (typical)
    PT_VP9 = 99       # VP9 video (typical)
    PT_OPUS = 111     # Opus audio (typical)

    # Clock rates
    CLOCK_RATE_AUDIO_8K = 8000    # G.711, G.729, etc.
    CLOCK_RATE_AUDIO_48K = 48000  # Opus
    CLOCK_RATE_VIDEO = 90000      # Most video codecs

    def __init__(
        self,
        payload_type: int = PT_H264,
        ssrc: Optional[int] = None,
        clock_rate: int = CLOCK_RATE_VIDEO,
        initial_sequence: Optional[int] = None,
        initial_timestamp: Optional[int] = None,
    ):
        """
        Initialize RTP packet builder.

        Args:
            payload_type: RTP payload type (0-127)
            ssrc: Synchronization source ID (random if None)
            clock_rate: Clock rate in Hz (e.g., 90000 for video)
            initial_sequence: Starting sequence number (random if None)
            initial_timestamp: Starting timestamp (random if None)
        """
        self.payload_type = payload_type & 0x7F
        self.ssrc = ssrc if ssrc is not None else random.randint(0, 0xFFFFFFFF)
        self.clock_rate = clock_rate
        self.sequence = initial_sequence if initial_sequence is not None else random.randint(0, 0xFFFF)
        self.timestamp = initial_timestamp if initial_timestamp is not None else random.randint(0, 0xFFFFFFFF)
        self.start_time = time.monotonic()

    def build(
        self,
        payload: bytes,
        marker: bool = False,
        timestamp: Optional[int] = None,
        sequence: Optional[int] = None,
        padding: int = 0,
        extension: Optional[bytes] = None,
        csrc_list: Optional[list] = None,
    ) -> bytes:
        """
        Build an RTP packet with the given payload.

        Args:
            payload: Payload data
            marker: Marker bit (e.g., end of frame)
            timestamp: Override timestamp (None = auto from clock)
            sequence: Override sequence number (None = auto increment)
            padding: Padding bytes to add (0-255)
            extension: Header extension data (None = no extension)
            csrc_list: List of CSRC values (None = no CSRCs)

        Returns:
            Complete RTP packet as bytes
        """
        # Use provided or auto-calculate values
        seq = sequence if sequence is not None else self.sequence
        self.sequence = (self.sequence + 1) & 0xFFFF

        if timestamp is not None:
            ts = timestamp
        else:
            # Calculate timestamp from wall clock
            elapsed = time.monotonic() - self.start_time
            ts = (self.timestamp + int(elapsed * self.clock_rate)) & 0xFFFFFFFF

        # Build header
        # Byte 0: V=2, P, X, CC
        csrc_count = len(csrc_list) if csrc_list else 0
        byte0 = (self.VERSION << 6)
        if padding:
            byte0 |= 0x20  # P bit
        if extension:
            byte0 |= 0x10  # X bit
        byte0 |= (csrc_count & 0x0F)

        # Byte 1: M, PT
        byte1 = (self.payload_type & 0x7F)
        if marker:
            byte1 |= 0x80

        # Fixed header
        header = struct.pack(
            '!BBHII',
            byte0,
            byte1,
            seq & 0xFFFF,
            ts & 0xFFFFFFFF,
            self.ssrc & 0xFFFFFFFF
        )

        # CSRC list
        if csrc_list:
            for csrc in csrc_list:
                header += struct.pack('!I', csrc & 0xFFFFFFFF)

        # Extension header (simplified - 2 bytes profile, 2 bytes length)
        if extension:
            ext_words = (len(extension) + 3) // 4
            header += struct.pack('!HH', 0xBEDE, ext_words)
            header += extension.ljust(ext_words * 4, b'\x00')

        # Payload and padding
        result = header + payload
        if padding:
            result += b'\x00' * (padding - 1) + bytes([padding])

        return result

    def set_sequence(self, seq: int) -> None:
        """
        Manually set sequence number (for gap simulation).

        Args:
            seq: New sequence number (0-65535)
        """
        self.sequence = seq & 0xFFFF

    def set_timestamp(self, ts: int) -> None:
        """
        Manually set timestamp (for jitter simulation).

        Args:
            ts: New timestamp value
        """
        self.timestamp = ts & 0xFFFFFFFF

    def skip_sequence(self, count: int = 1) -> None:
        """
        Skip sequence numbers (simulate packet loss).

        Args:
            count: Number of sequence numbers to skip
        """
        self.sequence = (self.sequence + count) & 0xFFFF

    def add_timestamp_jitter(self, max_jitter_samples: int) -> None:
        """
        Add random jitter to timestamp.

        Args:
            max_jitter_samples: Maximum jitter in timestamp units
        """
        jitter = random.randint(-max_jitter_samples, max_jitter_samples)
        self.timestamp = (self.timestamp + jitter) & 0xFFFFFFFF


def parse_rtp_header(data: bytes) -> Tuple[int, bool, int, int, int, int, bytes]:
    """
    Parse RTP header from packet data.

    Args:
        data: Raw packet data (at least 12 bytes)

    Returns:
        Tuple of (version, marker, payload_type, sequence, timestamp, ssrc, payload)

    Raises:
        ValueError: If packet is too short or invalid

    Example:
        version, marker, pt, seq, ts, ssrc, payload = parse_rtp_header(packet)
        print(f"RTP seq={seq}, ts={ts}, marker={marker}")
    """
    if len(data) < 12:
        raise ValueError("Packet too short for RTP header (need 12+ bytes)")

    byte0, byte1, seq, timestamp, ssrc = struct.unpack('!BBHII', data[:12])

    version = (byte0 >> 6) & 0x03
    padding = bool(byte0 & 0x20)
    extension = bool(byte0 & 0x10)
    csrc_count = byte0 & 0x0F

    marker = bool(byte1 & 0x80)
    payload_type = byte1 & 0x7F

    if version != 2:
        raise ValueError(f"Invalid RTP version: {version} (expected 2)")

    # Calculate header length
    header_len = 12 + (csrc_count * 4)

    # Skip extension if present
    if extension:
        if len(data) < header_len + 4:
            raise ValueError("Packet too short for extension header")
        ext_profile, ext_length = struct.unpack('!HH', data[header_len:header_len+4])
        header_len += 4 + (ext_length * 4)

    if len(data) < header_len:
        raise ValueError("Packet too short for declared header length")

    # Extract payload
    payload = data[header_len:]

    # Remove padding if present
    if padding and payload:
        pad_len = payload[-1]
        if pad_len > len(payload):
            raise ValueError(f"Invalid padding length: {pad_len}")
        payload = payload[:-pad_len]

    return version, marker, payload_type, seq, timestamp, ssrc, payload


def rtp_timestamp_to_ms(timestamp: int, clock_rate: int, base_timestamp: int = 0) -> float:
    """
    Convert RTP timestamp to milliseconds.

    Args:
        timestamp: RTP timestamp
        clock_rate: Clock rate in Hz
        base_timestamp: Reference timestamp (for relative calculation)

    Returns:
        Time in milliseconds
    """
    delta = (timestamp - base_timestamp) & 0xFFFFFFFF
    # Handle wraparound
    if delta > 0x80000000:
        delta -= 0x100000000
    return delta * 1000.0 / clock_rate


def calculate_rtp_jitter(
    arrival_time_ms: float,
    rtp_timestamp: int,
    clock_rate: int,
    prev_arrival_time_ms: float,
    prev_rtp_timestamp: int,
    prev_jitter: float,
) -> float:
    """
    Calculate RFC 3550 interarrival jitter.

    Args:
        arrival_time_ms: Current packet arrival time
        rtp_timestamp: Current RTP timestamp
        clock_rate: Clock rate in Hz
        prev_arrival_time_ms: Previous packet arrival time
        prev_rtp_timestamp: Previous RTP timestamp
        prev_jitter: Previous jitter estimate

    Returns:
        Updated jitter estimate in milliseconds

    Note:
        This implements the RFC 3550 jitter calculation:
        J(i) = J(i-1) + (|D(i-1,i)| - J(i-1))/16
        where D(i-1,i) = (R(i) - R(i-1)) - (S(i) - S(i-1))
    """
    # Transit time difference
    arrival_diff = arrival_time_ms - prev_arrival_time_ms
    ts_diff = rtp_timestamp_to_ms(rtp_timestamp, clock_rate, prev_rtp_timestamp)
    d = arrival_diff - ts_diff

    # Exponential moving average with gain 1/16
    return prev_jitter + (abs(d) - prev_jitter) / 16.0
