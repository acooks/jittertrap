# TCP Persist Timer Demonstration

## Overview

Demonstrates TCP's zero-window condition and persist timer mechanism. When a receiver stops reading data, its receive buffer fills up and it advertises a zero window, causing the sender to block. The sender then uses a persist timer with exponential backoff to probe whether the window has reopened.

## Network Effect

- **Initial Phase**: Normal data transfer at full speed
- **Stall Phase**: Sender blocked, no data packets visible
- **Probe Phase**: Small packets (window probes) at exponential intervals
- **Recovery Phase**: Burst of data when receiver resumes reading

## JitterTrap Indicators

| Metric | Expected Value | Why |
|--------|---------------|-----|
| Window Size | Drops to 0 | Receiver buffer full, advertising zero window |
| Throughput | Drops to near-zero | No data can be sent during zero-window |
| Zero-window events | 1+ events | JitterTrap flow details show zero-window |

**Primary observable**: The TCP Window chart shows the transition from healthy (large window) to zero-window (flat at 0) and back to healthy when the receiver resumes reading.

## Root Cause

The TCP persist timer prevents deadlock when a receiver's window goes to zero. This is specified in:
- **RFC 9293** (TCP) Section 3.8.6.1: "The sending TCP peer must regularly transmit at least one octet of new data... even if the send window is zero, in order to 'probe' the window."
- **RFC 6298** (RTO): Defines the retransmission timeout calculation used for probe intervals.

### Mechanism (per RFC 9293)

1. Receiver's application stops reading (slow consumer, blocked I/O, etc.)
2. Receiver buffer fills completely
3. Receiver advertises window size = 0 in ACKs
4. Sender cannot send data and starts persist timer
5. First zero-window probe sent after **1 RTO** (Retransmission Timeout)
6. Subsequent probes use **exponential backoff** (RTO × 2 each time)
7. Each probe sends 1 byte of data; ACK reveals current window size
8. If window still zero, timer doubles and sender waits again

### Timing (per RFC 6298)

| Parameter | Value | Notes |
|-----------|-------|-------|
| Initial RTO | 1 second | Before first RTT measurement |
| Minimum RTO | 1 second | Floor for calculated RTO |
| Maximum RTO | ≥60 seconds | Ceiling for backoff |
| Backoff | RTO × 2 | On each timeout |

**Probe sequence**: With minimum RTO, probes occur at approximately 1s, 2s, 4s, 8s, 16s, 32s, 60s (capped), 60s...

**Note**: Linux uses `tcp_retries2` (default 15) to limit total probes, resulting in ~15 minutes before connection abort.

**Key insight**: Without the persist timer, a lost window update could cause permanent deadlock - sender waiting for window, receiver waiting for data.

## Simulation Method

**Server (receiver)**:
- Accepts connection with small receive buffer (4KB)
- Reads normally for 5 seconds (visible normal traffic in JitterTrap)
- Stops reading for 5 seconds (zero-window occurs)
- Receive buffer fills, triggering zero-window advertisement
- Resumes reading to drain buffer and allow recovery

**Client (sender)**:
- Connects with large send buffer for aggressive sending
- Sends continuously until blocked by zero-window
- Reports blocking events when window goes to zero

## Usage

### With test runner (Recommended)
```bash
sudo ./infra/run-test.sh tcp-timing/persist-timer
```

### Manual execution
```bash
# Terminal 1: Start server (in destination namespace)
sudo ip netns exec pp-dest python3 server.py --port 9999

# Terminal 2: Start client (in source namespace)
sudo ip netns exec pp-source python3 client.py --host 10.0.1.2 --port 9999
```

## Configuration Options

### Server options

| Argument | Default | Description |
|----------|---------|-------------|
| `--port` | 9999 | Listen port |
| `--recv-buf` | 4096 | SO_RCVBUF size (smaller = faster zero-window) |
| `--normal` | 5 | Seconds of normal reading before stall |
| `--stall` | 5 | Stall duration in seconds |
| `--verbose` | false | Verbose logging |

### Client options

| Argument | Default | Description |
|----------|---------|-------------|
| `--host` | 10.0.1.2 | Server address |
| `--port` | 9999 | Server port |
| `--send-buf` | 65536 | SO_SNDBUF size |
| `--block-size` | 8192 | Size of each send() |
| `--duration` | 12 | Total test duration |
| `--verbose` | false | Verbose logging |

## Variations

### Quick demo (shorter stall)
```bash
python3 server.py --stall 3
python3 client.py --duration 10
```

### Extended observation (multiple probes visible)
```bash
# Stall for 15s to see probes at ~1s, 2s, 4s, 8s intervals
python3 server.py --stall 15
python3 client.py --duration 25
```

### Aggressive zero-window (tiny buffer)
```bash
python3 server.py --recv-buf 2048 --normal 2 --stall 10
python3 client.py --block-size 4096
```

## Self-Check Assertions

The test verifies:
1. **Zero-window detected**: Client experienced blocking (couldn't send)
2. **Persist timer triggered**: Block duration >= 4s (at least one RTO cycle)
3. **Data sent**: Meaningful data transfer before blocking

## tcpdump Commands

Observe persist timer behavior at packet level:

```bash
# In observer namespace - watch window advertisements
sudo ip netns exec pp-observer tcpdump -i br0 -n 'tcp port 9999' -v 2>&1 | grep -E 'win|seq|ack'

# Watch for zero-window (window field = 0)
sudo ip netns exec pp-observer tcpdump -i br0 -n 'tcp port 9999 and tcp[14:2] = 0'

# Watch for window probes (small packets during stall)
sudo ip netns exec pp-observer tcpdump -i br0 -n 'tcp port 9999' -c 50
```

## Expected Output

### Server
```
Server listening on port 9999
Receive buffer: 8.2 KB (requested 4.0 KB)
Will read for 5s, then stall for 5s
Client connected from 10.0.1.1:45678
Normal phase: received 2.5 MB in 5.0s
STOPPING READS - zero-window will occur
Stalling for 5 seconds...
Stall complete after 5.0s
Resuming reads to drain buffer...
Drained 56.0 KB in 0.1s

PERSIST TIMER TEST COMPLETE
Total received: 2.6 MB
Stall duration: 5s
```

### Client
```
Connecting to 10.0.1.2:9999...
Connected to 10.0.1.2:9999
Sending data for 12s...
Window opened after 5.1s block

PERSIST TIMER TEST RESULTS
Duration: 12.0s
Total sent: 2.6 MB

Blocking events: 1 total, 1 significant (>= 1s)
  Block 1: started at 5.0s, duration 5.1s

Self-check results:
  [PASS] Zero-window detected: 1 block event(s)
  [PASS] Persist timer triggered: 1 block(s) >= 4s
  [PASS] Data sent: 2.6 MB
```

## Comparison with receiver-starvation

| Aspect | persist-timer | receiver-starvation |
|--------|---------------|---------------------|
| Receiver behavior | Complete stop (sleep) | Slow continuous reading |
| Window pattern | Binary (healthy → zero → healthy) | Oscillating (sawtooth) |
| Zero-window duration | Sustained (seconds) | Brief/intermittent |
| Persist probes | Yes, at exponential backoff | Rarely triggered |
| Observable | Window drops to zero flat line | Window chart oscillation |

## References

- [RFC 9293](https://datatracker.ietf.org/doc/html/rfc9293) - Transmission Control Protocol (TCP) - Section 3.8.6.1 Zero-Window Probing
- [RFC 6298](https://datatracker.ietf.org/doc/html/rfc6298) - Computing TCP's Retransmission Timer
- [RFC 1122](https://datatracker.ietf.org/doc/html/rfc1122) Section 4.2.2.17 - Probing Zero Windows (original specification)
- Stevens, W. R. "TCP/IP Illustrated, Volume 1" - Chapter 22: TCP Persist Timer
- Linux kernel: `net/ipv4/tcp_timer.c` - `tcp_probe_timer()`
