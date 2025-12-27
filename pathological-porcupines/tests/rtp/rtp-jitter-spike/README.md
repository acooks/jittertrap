# RTP Jitter Spike Demonstration

## Overview

Demonstrates periodic jitter spikes in an RTP media stream. The sender transmits packets at a regular frame rate (30 fps) but periodically introduces a large delay, simulating network congestion, processing delays, or garbage collection pauses. The receiver calculates RFC 3550 jitter and detects spike events.

## Network Effect

- **Normal operation**: Packets arrive at ~33ms intervals (30 fps)
- **During spike**: One packet delayed by 200ms (configurable)
- **Recovery**: Following packets arrive at burst rate to catch up
- **Result**: Periodic jitter outliers visible in histogram

## JitterTrap Indicators

| Metric | Expected Value | Why |
|--------|---------------|-----|
| Jitter Histogram | Outliers at >100ms | Periodic large delays |
| IPG Histogram | Spikes every ~3s | Delay injection interval |
| RFC 3550 Jitter | Elevated during/after spike | Smoothed average responds to deviation |
| Packet Rate | Temporary burst post-spike | Catch-up after delay |
| Max Jitter | >200ms | Matches injected spike delay |

## Root Cause

Jitter spikes in RTP streams commonly result from:

1. **Network congestion**: Queue buildup at congested router
2. **CPU scheduling**: OS scheduler delays processing
3. **Garbage collection**: JVM/runtime GC pauses
4. **Buffer underrun recovery**: Playout buffer refill
5. **Path switching**: Routing changes causing delay variation
6. **Bufferbloat**: Excessive buffering on path

**Why it matters**:
- Media playback stutters or freezes during spikes
- VoIP quality degrades with audible gaps
- Video conferencing shows freeze/catch-up artifacts
- Jitter buffer sizing becomes critical
- QoE (Quality of Experience) directly impacted

## Simulation Method

**Sender**:
- Sends RTP packets at 30 fps (33.33ms interval)
- Every 3 seconds (configurable), sleeps for 200ms before sending
- RTP timestamps advance steadily (based on wall clock at capture time)
- Arrival times show jitter, but timestamps remain smooth

**Receiver**:
- Calculates RFC 3550 interarrival jitter
- Tracks instantaneous jitter (arrival_diff - timestamp_diff)
- Builds jitter histogram
- Detects spikes above threshold (default: 100ms)

## Usage

### Manual execution

```bash
# Terminal 1: Start receiver (in destination namespace)
sudo ip netns exec pp-dest python3 receiver.py --port 9999

# Terminal 2: Start sender (in source namespace)
sudo ip netns exec pp-source python3 sender.py --host 10.0.1.2 --port 9999
```

### With test runner

```bash
# Basic run
sudo ./infra/run-test.sh rtp/rtp-jitter-spike

# Auto-start mode
sudo ./infra/run-test.sh rtp/rtp-jitter-spike --auto
```

## Configuration Options

### Sender options

| Argument | Default | Description |
|----------|---------|-------------|
| `--host` | 10.0.1.2 | Destination address |
| `--port` | 9999 | Destination port |
| `--duration` | 15 | Test duration in seconds |
| `--frame-rate` | 30 | Frames per second |
| `--spike-interval` | 3.0 | Seconds between jitter spikes |
| `--spike-delay` | 200 | Additional delay in ms during spike |
| `--payload-size` | 1200 | RTP payload size in bytes |

### Receiver options

| Argument | Default | Description |
|----------|---------|-------------|
| `--port` | 9999 | Listen port |
| `--duration` | 20 | Receive duration in seconds |
| `--spike-threshold` | 100 | Jitter threshold for spike detection (ms) |

## Variations

### Frequent mild spikes
```bash
python3 sender.py --spike-interval 1.5 --spike-delay 100
python3 receiver.py --spike-threshold 50
```

### Severe infrequent spikes (simulating GC)
```bash
python3 sender.py --spike-interval 5.0 --spike-delay 500
python3 receiver.py --spike-threshold 200
```

### High frame rate (60 fps video)
```bash
python3 sender.py --frame-rate 60 --spike-delay 150
```

### Audio-like (50 pps, 20ms frames)
```bash
python3 sender.py --frame-rate 50 --spike-delay 100
```

## Self-Check Assertions

The receiver verifies:
1. **Packet count**: Received sufficient packets (>100)
2. **Jitter spikes detected**: At least 2 spikes above threshold
3. **Max jitter above threshold**: Maximum observed jitter exceeds threshold

## RFC 3550 Jitter Calculation

The receiver implements the RFC 3550 interarrival jitter algorithm:

```
J(i) = J(i-1) + (|D(i-1,i)| - J(i-1))/16

where:
  D(i-1,i) = (R(i) - R(i-1)) - (S(i) - S(i-1))
  R(i) = arrival time of packet i
  S(i) = RTP timestamp of packet i
```

This produces a smoothed estimate that responds to jitter changes over time.

## tcpdump Commands

Observe RTP jitter at packet level:

```bash
# In observer namespace - watch RTP packets
sudo ip netns exec pp-observer tcpdump -i br0 -n 'udp port 9999' -tt

# Decode RTP headers
sudo ip netns exec pp-observer tcpdump -i br0 -n 'udp port 9999' -X -c 20

# Watch timing between packets
sudo ip netns exec pp-observer tcpdump -i br0 -n 'udp port 9999' --time-stamp-precision=micro -c 50
```

## Expected Output

### Sender
```
Sending RTP to 10.0.1.2:9999
Frame rate: 30.0 fps (33.3ms interval)
Jitter spikes: 200.0ms delay every 3.0s
Starting RTP transmission for 15s...
INJECTING JITTER SPIKE: 200.0ms delay
Elapsed: 2.0s, Packets: 60 (30.0 fps), Spikes: 0
INJECTING JITTER SPIKE: 200.0ms delay
Elapsed: 4.0s, Packets: 120 (30.0 fps), Spikes: 1
...

RTP JITTER SPIKE SENDER RESULTS
Duration: 15.0s
Total packets: 450
Jitter spikes injected: 5

Self-check results:
  [PASS] Packet count: 450 (expected ~450)
  [PASS] Spike count: 5 spikes (expected ~5)
```

### Receiver
```
Receiver listening on port 9999
Spike threshold: 100.0ms
Waiting for RTP packets...
Tracking SSRC: 0xA1B2C3D4
JITTER SPIKE detected: 203.5ms at 3.1s
JITTER SPIKE detected: 198.7ms at 6.2s
...

RTP JITTER SPIKE RECEIVER RESULTS
Duration: 15.5s
Total packets: 450

Jitter Statistics (instantaneous):
  Min: 0.12ms
  Max: 205.23ms
  Median: 0.87ms
  Mean: 3.45ms
  RFC 3550 smoothed: 12.34ms

Jitter Histogram:
       <5ms:   430 ( 95.6%) ################################################
      <10ms:    10 (  2.2%) #
      <20ms:     5 (  1.1%)
     <100ms:     0 (  0.0%)
    >=500ms:     5 (  1.1%)

Spike Events (>100.0ms): 5
  Spike 1: 203.5ms at 3.1s
  Spike 2: 198.7ms at 6.2s
  ...
  Average spike interval: 3.0s

Self-check results:
  [PASS] Packet count: 450 packets
  [PASS] Jitter spikes detected: 5 spikes
  [PASS] Max jitter above threshold: 205.23ms >= 100.0ms
```

## Relationship to Real-World Issues

| Real-World Cause | Simulated By |
|------------------|--------------|
| Network congestion | Periodic delay injection |
| GC pause in encoder | Large delay every N seconds |
| CPU scheduling delay | Variable small delays |
| Buffer underrun | Delay then burst of packets |
| Path switching | Single large delay event |

## References

- RFC 3550 - RTP: A Transport Protocol for Real-Time Applications
- RFC 3551 - RTP Profile for Audio and Video Conferences
- ITU-T G.1010 - End-user multimedia QoS categories
- "Understanding Jitter in Packet Voice Networks" (Cisco)
- JitterTrap RTP analysis documentation
