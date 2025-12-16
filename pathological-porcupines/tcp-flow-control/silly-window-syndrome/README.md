# Silly Window Syndrome

## Overview

Demonstrates the classic TCP "Silly Window Syndrome" (SWS) where both sender
and receiver operate inefficiently by exchanging many tiny segments instead
of fewer large ones.

## Network Effect

**What you'll see:**
- Packet size histogram heavily weighted toward small packets
- High packet count relative to bytes transferred
- Poor throughput despite no packet loss
- High overhead ratio (headers >> payload)

## JitterTrap Indicators

| Metric | Expected Value | Why |
|--------|---------------|-----|
| Packet Size | Clustered near minimum | Tiny segments being sent |
| Packets/Second | Very high | Many small packets |
| Throughput | Low | Header overhead dominates |
| RTT | Normal | No congestion, just inefficiency |

## Root Cause (Real-World)

**Receiver-side SWS:**
- Application reads only a few bytes at a time
- Window opens by small amounts
- Sender transmits tiny segments to fill small windows

**Sender-side SWS:**
- Application writes small chunks (without Nagle or with TCP_NODELAY)
- Each small write becomes a separate segment

This simulation demonstrates receiver-side SWS.

## Simulation Method

Server (receiver) uses a very small receive buffer and reads only 1 byte
at a time. This causes the advertised window to fluctuate in tiny increments,
leading to tiny segment transmissions.

## Usage

### Terminal 1: Start JitterTrap (optional)
```bash
sudo jt-server -i lo
# Open http://localhost:8080 and select loopback interface
```

### Terminal 2: Start Server (receiver)
```bash
cd pathological-porcupines/tcp-flow-control/silly-window-syndrome
python server.py --port 9999 --read-size 1
```

### Terminal 3: Start Client (sender)
```bash
python client.py --host localhost --port 9999
```

## Configuration Options

### Server Options
| Option | Default | Description |
|--------|---------|-------------|
| `--port` | 9999 | Listen port |
| `--recv-buf` | 256 | SO_RCVBUF size (very small for SWS) |
| `--read-size` | 1 | Bytes to read per recv() call |
| `--delay` | 0.001 | Delay between reads (1ms) |

### Client Options
| Option | Default | Description |
|--------|---------|-------------|
| `--host` | localhost | Server host |
| `--port` | 9999 | Server port |
| `--duration` | 30 | Test duration in seconds |

## Variations

- **Classic SWS**: `--read-size 1 --recv-buf 256` (extreme)
- **Moderate SWS**: `--read-size 10 --recv-buf 1024` (visible but not severe)
- **SWS Avoidance**: `--read-size 0` (reads all available, demonstrates fix)

## Educational Notes

**Silly Window Syndrome was identified in RFC 813 (1982):**

The syndrome occurs when:
1. Receiver advertises small windows (< MSS)
2. Sender transmits small segments to fill those windows
3. Result: TCP overhead (40 bytes header) dominates

**Avoidance mechanisms:**

*Receiver-side (RFC 1122):*
- Don't advertise window < MSS unless buffer is completely empty
- Wait until window is at least min(MSS, buffer/2) before advertising

*Sender-side (Nagle's Algorithm, RFC 896):*
- Buffer small writes until previous data is acknowledged
- Send immediately only if segment is MSS-sized or no unacked data

## tcpdump Commands

```bash
# Watch packet sizes
sudo tcpdump -i lo port 9999 -nn -q

# Detailed view showing window sizes
sudo tcpdump -i lo port 9999 -nn -v 2>&1 | grep -E 'length [0-9]+|win [0-9]+'

# Count packets by size (requires tshark)
sudo tshark -i lo -f "port 9999" -T fields -e tcp.len | sort | uniq -c
```

## References

- RFC 813: Window and Acknowledgement Strategy in TCP (1982)
- RFC 896: Nagle's Algorithm - Congestion Control in IP/TCP
- RFC 1122: Host Requirements - TCP (SWS avoidance)
- Stevens, TCP/IP Illustrated Vol. 1, Chapter 22
