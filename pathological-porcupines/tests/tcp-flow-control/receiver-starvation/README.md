# Receiver Starvation

## Overview

Simulates a TCP receiver application that cannot quite keep up with incoming data, causing TCP window oscillation as the receive buffer repeatedly fills and partially drains.

## Network Effect

**What you'll see:**
- TCP window oscillates (fills up, drains a bit, fills again)
- Brief or no sustained zero-window events
- Throughput limited by receiver's processing capacity
- RTT may increase slightly as buffers fill

**Key distinction from persist-timer:**
- persist-timer: Complete stop → sustained zero-window → persist probes
- receiver-starvation: Slow continuous → oscillating window → flow control

## JitterTrap Indicators

| Metric | Expected Value | Why |
|--------|---------------|-----|
| TCP Window | Oscillating pattern | Buffer fills/drains repeatedly |
| Throughput | Limited (~800 KB/s) | Receiver can't keep up |
| Zero Window | Brief or none | Not sustained like persist-timer |
| RTT | Slightly elevated | Some queuing in buffers |

## Root Cause (Real-World)

This occurs when:
- Receiver application is doing slow processing (disk I/O, computation)
- CPU contention (competing processes, GC pauses)
- Rate mismatch between sender and receiver capabilities
- Deliberate backpressure (rate limiting at application layer)

**Key difference from persist-timer:** The receiver is always reading, just not fast enough. This creates a "struggling" pattern rather than a complete stall.

## Simulation Method

**Server (receiver):**
- Reads data with a small delay between recv() calls
- Default: 8KB reads every 10ms → ~800 KB/s max capacity
- Buffer fills when sender exceeds this rate
- Window advertised reflects available buffer space

**Client (sender):**
- Sends at target rate (~1 MB/s by default)
- Rate is ~25% higher than receiver capacity
- Gets blocked briefly when window shrinks
- Resumes when receiver drains some buffer

## Usage

### With test runner (Recommended)
```bash
sudo ./infra/run-test.sh tcp-flow-control/receiver-starvation
```

### Manual execution
```bash
# Terminal 1: Start server (in destination namespace)
sudo ip netns exec pp-dest python3 server.py --port 9999

# Terminal 2: Start client (in source namespace)
sudo ip netns exec pp-source python3 client.py --host 10.0.1.2 --port 9999
```

## Configuration Options

### Server Options
| Option | Default | Description |
|--------|---------|-------------|
| `--port` | 9999 | Listen port |
| `--recv-buf` | 16384 | SO_RCVBUF size (16KB) |
| `--delay` | 0.01 | Seconds between recv() calls (10ms) |
| `--read-size` | 8192 | Bytes per recv() (8KB) |
| `--duration` | 15 | Test duration in seconds |

### Client Options
| Option | Default | Description |
|--------|---------|-------------|
| `--host` | 10.0.1.2 | Server host |
| `--port` | 9999 | Server port |
| `--rate` | 1.0 | Target throughput in MB/s |
| `--duration` | 15 | Test duration in seconds |
| `--chunk-size` | 16384 | Chunk size for sends (16KB) |

## Variations

### Mild starvation (barely keeping up)
```bash
# Receiver at 90% of sender rate - window fluctuates but rarely hits zero
python3 server.py --delay 0.008 --read-size 8192   # ~1 MB/s capacity
python3 client.py --rate 1.1                        # 1.1 MB/s send
```

### Severe starvation (frequent zero-window)
```bash
# Receiver at 50% of sender rate - frequent brief zero-window events
python3 server.py --delay 0.02 --read-size 4096   # ~200 KB/s capacity
python3 client.py --rate 0.5                       # 500 KB/s send
```

### Extreme starvation (like persist-timer)
```bash
# Receiver overwhelmed - behaves like persist-timer
python3 server.py --delay 0.1 --read-size 1024    # ~10 KB/s capacity
python3 client.py --rate 10                        # 10 MB/s send
```

## Self-Check Assertions

The test verifies:
1. **Data transferred**: Meaningful data exchange occurred (>100KB)
2. **Rate limited**: Actual throughput limited by receiver capacity
3. **Blocking detected**: Sender experienced flow control blocking

## Expected Output

### Server
```
Server listening on port 9999
Receive buffer: 32.0 KB (requested 16.0 KB)
Starting slow receive loop for 15s
Receive capacity: ~800.0 KB/s (8.0 KB every 10ms)
Window will oscillate as buffer fills/drains
Client connected from 10.0.1.1:45678
Received: 4.0 MB total, current: 780.5 KB/s, avg: 795.2 KB/s
...

RECEIVER STARVATION TEST RESULTS
Duration: 15.0s
Total received: 11.9 MB
Average rate: 795.2 KB/s
Receive capacity: ~800.0 KB/s

Self-check results:
  [PASS] Data received: 11.9 MB
  [PASS] Rate limited by receiver: 795.2 KB/s <= 800.0 KB/s
  [PASS] Duration: 15.0s
```

### Client
```
Connecting to 10.0.1.2:9999...
Connected. Send buffer: 128.0 KB
Target rate: 1.0 MB/s
Starting 15s test
Sending 16.0 KB chunks to achieve 1.0 MB/s
Sent: 4.2 MB total, rate: 820.1 KB/s, blocks: 42
...

RECEIVER STARVATION CLIENT RESULTS
Duration: 15.0s
Total sent: 11.9 MB
Target rate: 1.0 MB/s
Actual rate: 795.2 KB/s
Blocked 156 times, total blocked time: 3.21s (21.4% of test)

Self-check results:
  [PASS] Data sent: 11.9 MB
  [PASS] Rate limited by receiver: 795.2 KB/s < 1.0 MB/s
  [PASS] Sender blocked: 156 blocks, 3.21s total
```

## Comparison with persist-timer

| Aspect | receiver-starvation | persist-timer |
|--------|---------------------|---------------|
| Receiver behavior | Slow continuous reading | Complete stop |
| Window pattern | Oscillating (sawtooth) | Binary (healthy → zero → healthy) |
| Zero-window | Brief/intermittent | Sustained (seconds) |
| Persist probes | Rarely triggered | Yes, at exponential backoff |
| Observable in JitterTrap | Window chart oscillation | Window drops to zero flat line |

## tcpdump Commands

```bash
# Watch window advertisements
sudo ip netns exec pp-observer tcpdump -i br0 -n 'tcp port 9999' -v 2>&1 | grep -E 'win [0-9]+'

# Count packets with small window (<1000 bytes)
sudo ip netns exec pp-observer tcpdump -i br0 -n 'tcp port 9999 and tcp[14:2] < 1000' -c 100
```

## References

- RFC 793: TCP (window management)
- RFC 1122: Host Requirements (flow control)
- RFC 7323: TCP Extensions (window scaling)
