# Nagle-Delayed ACK Interaction

## Overview

Demonstrates the infamous interaction between Nagle's algorithm and TCP
delayed acknowledgments, which can cause 40-200ms latency spikes for
small write patterns.

## Network Effect

**What you'll see:**
- Bimodal RTT distribution (fast: ~1ms, slow: ~40-200ms)
- Regular latency spikes on small writes
- Throughput much lower than expected for request-response patterns
- ACKs arriving in batches rather than per-segment

## JitterTrap Indicators

| Metric | Expected Value | Why |
|--------|---------------|-----|
| RTT Histogram | Bimodal (two peaks) | Normal ACKs vs delayed ACKs |
| RTT p99 | 40-200ms | Delayed ACK timeout |
| RTT p50 | 1-10ms | Fast ACKs (piggybacked or immediate) |
| IPG | Gaps of ~40ms | Delayed ACK timer interval |

## Root Cause (Real-World)

**The interaction:**

1. **Nagle's Algorithm** (sender): Buffer small writes until either:
   - Previous data is acknowledged, OR
   - Buffer reaches MSS

2. **Delayed ACK** (receiver): Wait up to 200ms before sending ACK:
   - Hoping to piggyback ACK on response data
   - Or until 2 full segments received
   - Or timer expires (40ms on Linux, 200ms default)

**The deadlock:**
1. Sender writes small chunk (< MSS)
2. Nagle buffers it, waiting for ACK of previous data
3. Receiver has data but delays ACK, waiting for more data or response
4. Both wait... until delayed ACK timer expires (40-200ms)

## Simulation Method

Client sends small writes without TCP_NODELAY, triggering Nagle buffering.
Server uses standard TCP stack with delayed ACKs, creating the interaction.

## Usage

### Terminal 1: Start JitterTrap (optional)
```bash
sudo jt-server -i lo
# Open http://localhost:8080, watch RTT histogram become bimodal
```

### Terminal 2: Start Server
```bash
cd pathological-porcupines/tcp-timing/nagle-delayed-ack
python server.py --port 9999
```

### Terminal 3: Start Client (with Nagle - shows problem)
```bash
python client.py --host localhost --port 9999
```

### Terminal 4: Start Client (without Nagle - shows fix)
```bash
python client.py --host localhost --port 9999 --nodelay
```

## Configuration Options

### Server Options
| Option | Default | Description |
|--------|---------|-------------|
| `--port` | 9999 | Listen port |
| `--echo` | False | Echo data back (affects delayed ACK) |
| `--quickack` | False | Disable delayed ACKs (TCP_QUICKACK) |

### Client Options
| Option | Default | Description |
|--------|---------|-------------|
| `--host` | localhost | Server host |
| `--port` | 9999 | Server port |
| `--nodelay` | False | Set TCP_NODELAY (disables Nagle) |
| `--write-size` | 100 | Bytes per write (must be < MSS) |
| `--writes` | 1000 | Number of writes to perform |
| `--interval` | 0.01 | Seconds between write pairs |

## Variations

- **Classic deadlock**: Default settings (Nagle enabled, ~100 byte writes)
- **Fixed with NODELAY**: `--nodelay` flag (immediate sends)
- **Fixed with QUICKACK**: Server with `--quickack` (immediate ACKs)
- **No deadlock**: `--write-size 1500` (MSS-sized writes bypass Nagle)

## Educational Notes

**Nagle's Algorithm (RFC 896, 1984):**
```
if data_to_send >= MSS:
    send immediately
elif no unacknowledged data:
    send immediately
else:
    buffer until ACK received or MSS accumulated
```

**Delayed ACK (RFC 1122, RFC 2581):**
```
Wait up to delayed_ack_timeout (40-200ms) before sending ACK
Send immediately if:
  - 2 full segments received
  - Out-of-order segment received
  - PSH flag set (implementation-dependent)
```

**Common fixes:**
1. **TCP_NODELAY**: Disable Nagle (appropriate for interactive/gaming)
2. **TCP_QUICKACK**: Disable delayed ACK (Linux-specific)
3. **Coalesce writes**: Combine small writes into larger ones
4. **Use writev()**: Gather I/O to send multiple buffers atomically

## Platform Differences

| Platform | Default Delayed ACK | Notes |
|----------|---------------------|-------|
| Linux | 40ms | Can use TCP_QUICKACK |
| macOS | 100ms | No TCP_QUICKACK |
| Windows | 200ms | Can be tuned via registry |

## tcpdump Commands

```bash
# Watch for delayed ACKs (large gaps before ACK)
sudo tcpdump -i lo port 9999 -nn -ttt

# Measure time between data and ACK
sudo tcpdump -i lo port 9999 -nn -ttt | grep -E 'length [0-9]+|ack'
```

## References

- RFC 896: Congestion Control in IP/TCP Internetworks (Nagle)
- RFC 1122: Requirements for Internet Hosts (Delayed ACK)
- RFC 2581: TCP Congestion Control (ACK recommendations)
- "Nagle's Algorithm is Not Friendly" - John Nagle's retrospective
