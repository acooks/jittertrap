---
screenshot:
  views: [legend, throughput, toptalk, rtt, window, pgaps, flowdetails]
  data_accumulation_sec: 15
---

# TCP Sender Stall Demonstration

## Overview

Demonstrates application-level sender stalls where the sender periodically stops sending data. Unlike receiver starvation (which produces zero-window events), sender stalls create visible gaps in packet timing without any TCP flow control signals - the receiver always has buffer space available.

## Network Effect

- **Normal Phase**: Steady data transfer, packets evenly spaced
- **Stall Phase**: No packets sent, large inter-packet gaps visible
- **Recovery Phase**: Data resumes, TCP may invoke idle restart (cwnd reduction)

## JitterTrap Indicators

| Metric | Expected Value | Why |
|--------|---------------|-----|
| Window Size | Remains healthy (non-zero) | Receiver has space, sender just isn't sending |
| Zero-window events | 0 | No receiver-side pressure |
| Throughput | Periodic drops to zero | Sender stalls visible in throughput chart |
| Inter-packet gaps | Varying gaps (5, 10, 20, 50, 100ms) | Different gap sizes visible in IPG chart |

**Key diagnostic difference from receiver starvation:**
- Sender stall: Window stays healthy, no zero-window events
- Receiver starvation: Window drops to zero, zero-window events detected

## Root Cause

Application-level sender stalls can occur due to:

1. **Disk I/O delays**: Reading from slow storage
2. **Database queries**: Waiting for query results
3. **Computation**: CPU-bound processing between sends
4. **API calls**: Waiting for external service responses
5. **Resource contention**: Locks, thread pool exhaustion

### TCP Idle Restart (RFC 9293, Section 3.8.6.2.3)

When a sender stalls for longer than the RTO, TCP may reduce cwnd:

> "A TCP SHOULD set cwnd to no more than RW (restart window) before beginning transmission if the TCP has not sent data in an interval exceeding the retransmission timeout."

Default restart window: RW = min(IW, cwnd) where IW is typically 10 segments.

This means:
- Short stalls (<RTO): No impact, transmission resumes at full speed
- Long stalls (>RTO): TCP resets to slow-start, throughput temporarily drops

### BBR vs CUBIC Handling

**Hypothesis:** BBR maintains bandwidth estimates during idle periods, recovering faster than CUBIC which must rebuild cwnd.

## Simulation Method

**Server (receiver)**:
- Accepts connection with normal receive buffer
- Reads continuously and as fast as possible
- Reports bytes received (should match client pattern)

**Client (sender)**:
- Connects and sends data in bursts
- Periodically stalls (configurable duration and interval)
- Stall patterns: periodic, random, or burst

## Usage

### With test runner (Recommended)
```bash
sudo ./infra/run-test.sh tcp-timing/sender-stall
```

### Manual execution
```bash
# Terminal 1: Start server (in destination namespace)
sudo ip netns exec pp-dest python3 server.py --port 9999

# Terminal 2: Start client (in source namespace, default varying pattern)
sudo ip netns exec pp-source python3 client.py --host 10.0.1.2 --port 9999
```

## Configuration Options

### Server options

| Argument | Default | Description |
|----------|---------|-------------|
| `--port` | 9999 | Listen port |
| `--recv-buf` | 262144 | SO_RCVBUF size (large to avoid receiver-side effects) |
| `--verbose` | false | Verbose logging |

### Client options

| Argument | Default | Description |
|----------|---------|-------------|
| `--host` | 10.0.1.2 | Server address |
| `--port` | 9999 | Server port |
| `--send-buf` | 65536 | SO_SNDBUF size |
| `--block-size` | 8192 | Size of each send() |
| `--duration` | 15 | Total test duration |
| `--stall` | 500 | Stall duration in milliseconds (ignored for varying pattern) |
| `--interval` | 1000 | Time between stalls in milliseconds |
| `--pattern` | varying | Stall pattern: varying (5,10,20,50,100ms), periodic, random, or burst |
| `--verbose` | false | Verbose logging |

## Variations

### Short frequent stalls (disk I/O simulation)
```bash
python3 client.py --stall 50 --interval 500
```

### Long infrequent stalls (database query simulation)
```bash
python3 client.py --stall 2000 --interval 5000
```

### Random stalls (unpredictable application behavior)
```bash
python3 client.py --stall 500 --interval 2000 --pattern random
```

### Burst stalls (3 quick stalls, then normal)
```bash
python3 client.py --stall 200 --interval 300 --pattern burst
```

### Trigger TCP idle restart (stall > RTO)
```bash
# Stall for 1.5 seconds - should trigger cwnd reduction
python3 client.py --stall 1500 --interval 5000 --duration 20
```

## Self-Check Assertions

The test verifies:
1. **No zero-window detected**: Receiver was never the bottleneck
2. **Stalls occurred**: Inter-packet gaps match configured stall duration
3. **Data sent**: Meaningful data transfer between stalls

## tcpdump Commands

Observe sender stall behavior at packet level:

```bash
# In observer namespace - watch for gaps in packet timing
sudo ip netns exec pp-observer tcpdump -i br0 -n 'tcp port 9999' -tt

# Count packets per second (look for gaps)
sudo ip netns exec pp-observer tcpdump -i br0 -n 'tcp port 9999' -c 100 -tt 2>&1 | awk '{print $1}' | cut -d. -f1 | uniq -c

# Watch window advertisements (should stay healthy)
sudo ip netns exec pp-observer tcpdump -i br0 -n 'tcp port 9999' -v 2>&1 | grep 'win'
```

## Expected Output

### Server
```
Server listening on port 9999
Receive buffer: 512.0 KB (requested 256.0 KB)
Client connected from 10.0.1.1:45678
Receiving data...
Received 2.1 MB in 15.0s (average 140.0 KB/s)

SENDER STALL TEST COMPLETE
Total received: 2.1 MB
Zero-window events: 0
```

### Client (varying pattern)
```
Sending data for 15s with stalls (500ms every 1000ms, pattern: varying)...

Stall 1 at 1.0s (5ms)
Stall 2 at 2.0s (10ms)
Stall 3 at 3.0s (20ms)
Stall 4 at 4.0s (50ms)
Stall 5 at 5.1s (100ms)
Stall 6 at 6.2s (5ms)
...

SENDER STALL TEST RESULTS
Duration: 15.0s
Stalls executed: 14
Total stall time: 0.5s

Self-check results:
  [PASS] No zero-window expected
  [PASS] Stalls executed: 14 stalls, avg 32ms
  [PASS] Data sent: 18.3 GB
```

## Comparison with persist-timer

| Aspect | sender-stall | persist-timer |
|--------|--------------|---------------|
| Cause | Sender application pauses | Receiver buffer full |
| Zero-window | No | Yes |
| Window chart | Healthy throughout | Drops to zero |
| Persist probes | No | Yes (exponential backoff) |
| TCP state impact | Possible idle restart | Persist timer active |
| Observable | IPG gaps, throughput dips | Zero-window in flow details |

## Use Case: Diagnosing Application Issues

When you see:
- **Throughput dips but window stays healthy**: Sender stall (application problem)
- **Throughput dips with zero-window**: Receiver stall (receiver problem)

This distinction helps isolate whether the issue is:
- Sender-side (disk, CPU, external service)
- Receiver-side (slow processing, small buffer)
- Network (congestion, packet loss)

## References

- [RFC 9293](https://datatracker.ietf.org/doc/html/rfc9293) - TCP Section 3.8.6.2.3: Restarting Idle Connections
- [RFC 2581](https://datatracker.ietf.org/doc/html/rfc2581) Section 4.1 - Restarting Idle Connections
- Stevens, W. R. "TCP/IP Illustrated, Volume 1" - Chapter 20: Bulk Data Flow
- Linux kernel: `net/ipv4/tcp_output.c` - `tcp_cwnd_restart()`
