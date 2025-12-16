# Receiver Starvation

## Overview

Simulates a TCP receiver application that cannot keep up with incoming data,
causing the receive buffer to fill and eventually triggering TCP zero-window
advertisements.

## Network Effect

**What you'll see:**
- Zero-window events as receiver buffer fills
- RTT increases as data queues in sender's buffer
- Throughput drops to near-zero during starvation periods
- Persist probes from sender (1s, 2s, 4s... intervals)

## JitterTrap Indicators

| Metric | Expected Value | Why |
|--------|---------------|-----|
| Zero Window Count | Increases | Receiver advertises window=0 |
| RTT | Increases, then stabilizes | Sender queues data |
| Throughput | Drops to near-zero | No window to send into |
| Packet Size | Tiny probe packets | Zero-window probes |

## Root Cause (Real-World)

This occurs when:
- Receiver application is blocked on slow I/O (disk, database)
- CPU starvation (competing processes, GC pauses)
- Application bug causing delayed reads
- Deliberate backpressure (rate limiting)

## Simulation Method

Server (receiver) sleeps between recv() calls, simulating slow processing.
This causes the socket receive buffer to fill, triggering TCP flow control.

## Usage

### Terminal 1: Start JitterTrap (optional)
```bash
sudo jt-server -i lo
# Open http://localhost:8080 and select loopback interface
```

### Terminal 2: Start Server (receiver)
```bash
cd pathological-porcupines/tcp-flow-control/receiver-starvation
python server.py --port 9999 --delay 0.1
```

### Terminal 3: Start Client (sender)
```bash
python client.py --host localhost --port 9999 --rate 10
```

## Configuration Options

### Server Options
| Option | Default | Description |
|--------|---------|-------------|
| `--port` | 9999 | Listen port |
| `--recv-buf` | 8192 | SO_RCVBUF size (smaller = faster starvation) |
| `--delay` | 0.1 | Seconds to sleep between recv() calls |
| `--read-size` | 1024 | Bytes to read per recv() call |

### Client Options
| Option | Default | Description |
|--------|---------|-------------|
| `--host` | localhost | Server host |
| `--port` | 9999 | Server port |
| `--rate` | 10 | Target throughput in MB/s |
| `--duration` | 30 | Test duration in seconds |

## Variations

- **Mild**: `--delay 0.05 --recv-buf 65536` (occasional zero-window)
- **Severe**: `--delay 0.5 --recv-buf 4096` (sustained starvation)
- **Intermittent**: Use `--pattern burst` to alternate fast/slow reading

## Educational Notes

TCP flow control prevents sender from overwhelming receiver:

1. Receiver advertises available buffer space in "window" field
2. Sender limits unacknowledged data to advertised window
3. When window reaches 0, sender stops and waits
4. Sender periodically probes with tiny segments to detect window opening
5. Window opening allows transmission to resume

This is distinct from congestion control (which limits for network capacity).

## tcpdump Commands

```bash
# Watch for zero-window advertisements
sudo tcpdump -i lo port 9999 -nn -v 2>&1 | grep -E 'win [0-9]+'

# Count zero-window events
sudo tcpdump -i lo port 9999 -nn 2>&1 | grep 'win 0'
```

## References

- RFC 793: TCP (window management)
- RFC 1122: Host Requirements (persist timer)
- RFC 7323: TCP Extensions (window scaling)
