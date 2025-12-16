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

The TCP persist timer (RFC 1122 Section 4.2.2.17) prevents deadlock when a receiver's window goes to zero:

1. Receiver's application stops reading (slow consumer, blocked I/O, etc.)
2. Receiver buffer fills completely
3. Receiver advertises window size = 0 in ACKs
4. Sender cannot send data and sets persist timer
5. Timer fires at exponential intervals (5s, 10s, 20s, 40s, 60s, 120s max)
6. Each timer expiry sends a "window probe" (1 byte of data)
7. Probe ACK reveals current window size
8. If window still zero, timer doubles and sender waits again

**Key insight**: Without the persist timer, a lost window update could cause permanent deadlock - sender waiting for window, receiver waiting for data.

## Simulation Method

**Server (receiver)**:
- Accepts connection with small receive buffer (4KB)
- Reads normally for 5 seconds (visible normal traffic)
- Stops reading for 5 seconds (zero-window occurs)
- Receive buffer fills, triggering zero-window advertisement
- Resumes reading to drain buffer and allow recovery

**Client (sender)**:
- Connects with large send buffer for aggressive sending
- Sends continuously until blocked by zero-window
- Reports blocking events when window goes to zero

## Usage

### Manual execution

```bash
# Terminal 1: Start server (in destination namespace)
sudo ip netns exec pp-dest python3 server.py --port 9999 --stall 15

# Terminal 2: Start client (in source namespace)
sudo ip netns exec pp-source python3 client.py --host 10.0.1.2 --port 9999
```

### With test runner

```bash
# Basic run
sudo ./infra/run-test.sh tcp-timing/persist-timer

# Auto-start mode (no user prompt)
sudo ./infra/run-test.sh tcp-timing/persist-timer --auto
```

## Configuration Options

### Server options

| Argument | Default | Description |
|----------|---------|-------------|
| `--port` | 9999 | Listen port |
| `--recv-buf` | 4096 | SO_RCVBUF size (smaller = faster zero-window) |
| `--initial-reads` | 5 | Reads before stopping |
| `--stall` | 15 | Stall duration in seconds |
| `--verbose` | false | Verbose logging |

### Client options

| Argument | Default | Description |
|----------|---------|-------------|
| `--host` | 10.0.1.2 | Server address |
| `--port` | 9999 | Server port |
| `--send-buf` | 65536 | SO_SNDBUF size |
| `--block-size` | 8192 | Size of each send() |
| `--duration` | 20 | Total test duration |
| `--verbose` | false | Verbose logging |

## Variations

### Quick demo (shorter stall)
```bash
python3 server.py --stall 10
python3 client.py --duration 15
```

### Aggressive zero-window (tiny buffer)
```bash
python3 server.py --recv-buf 2048 --initial-reads 2 --stall 20
python3 client.py --block-size 4096
```

### Extended observation (longer for multiple probes)
```bash
python3 server.py --stall 45
python3 client.py --duration 60
```

## Self-Check Assertions

The test verifies:
1. **Zero-window detected**: Client experienced blocking (couldn't send)
2. **Persist timer triggered**: Block duration >= 4s (persist timer interval)
3. **Data sent**: Meaningful data transfer before blocking

## tcpdump Commands

Observe persist timer behavior at packet level:

```bash
# In observer namespace - watch window advertisements
sudo ip netns exec pp-observer tcpdump -i br0 -n 'tcp port 9999' -v 2>&1 | grep -E 'win|seq|ack'

# Watch for zero-window
sudo ip netns exec pp-observer tcpdump -i br0 -n 'tcp port 9999 and tcp[14:2] = 0'

# Watch for window probes (small packets during stall)
sudo ip netns exec pp-observer tcpdump -i br0 -n 'tcp port 9999' -c 50
```

## Expected Output

### Server
```
Server listening on port 9999
Receive buffer: 8.2 KB (requested 4.0 KB)
Will read 5 times, then stall for 15s
Client connected from 10.0.1.1:45678
Read 5.0 KB in 5 reads
STOPPING READS - zero-window will occur
Stalling for 15 seconds...
Stall complete after 15.0s
Resuming reads to drain buffer...
Drained 56.0 KB in 0.1s

PERSIST TIMER TEST COMPLETE
Total received: 61.0 KB
```

### Client
```
Connecting to 10.0.1.2:9999...
Connected to 10.0.1.2:9999
Sending data for 20s...
BLOCKED - zero-window condition detected
Window opened after 5.1s block
BLOCKED - zero-window condition detected
Window opened after 10.2s block
...

PERSIST TIMER TEST RESULTS
Duration: 20.1s
Total sent: 98.5 KB

Blocking events detected: 2
  Block 1: started at 0.5s, duration 5.1s
  Block 2: started at 5.8s, duration 10.2s

Self-check results:
  [PASS] Zero-window detected: 2 block event(s)
  [PASS] Persist timer triggered: 2 block(s) >= 4s
  [PASS] Data sent: 98.5 KB
```

## References

- RFC 1122 Section 4.2.2.17 - Probing Zero Windows
- RFC 793 - Transmission Control Protocol (original TCP spec)
- Stevens, W. R. "TCP/IP Illustrated, Volume 1" - Chapter 22: TCP Persist Timer
- Linux kernel source: `net/ipv4/tcp_timer.c` - `tcp_probe_timer()`
