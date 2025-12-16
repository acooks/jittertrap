# Bursty UDP Sender Demonstration

## Overview

Demonstrates bursty traffic patterns and their distinctive bimodal inter-packet gap (IPG) distribution. The sender transmits packets in bursts with tight spacing, followed by longer gaps between bursts. This creates a characteristic two-peak histogram visible in JitterTrap.

## Network Effect

- **Within burst**: Packets arrive at high rate (1ms spacing)
- **Between bursts**: No packets during gap period (~90ms)
- **Result**: Bimodal IPG distribution instead of uniform traffic

## JitterTrap Indicators

| Metric | Expected Value | Why |
|--------|---------------|-----|
| IPG Histogram | Two distinct peaks | Burst vs inter-burst spacing |
| Peak 1 | 1ms bucket | Within-burst packet interval (1ms) |
| Peak 2 | 50ms bucket | Gap between bursts (~90ms falls in 50-100ms bucket) |
| Mean IPG | ~10ms | Weighted average: (9×1ms + 1×90ms) / 10 |
| Throughput | Bursty pattern | Periodic spikes matching burst timing |

## Root Cause

Bursty traffic patterns are common in real applications:

1. **Video streaming**: Frames sent as bursts (B-frames, I-frames)
2. **Batch processing**: Application sends queued data in batches
3. **Network buffering**: Router/switch buffers create micro-bursts
4. **Request/response**: Bursts of responses to batch requests
5. **Timer-driven I/O**: Application sends on periodic timer

**Why it matters**:
- Burstiness can cause queue buildup and packet loss
- IPG histograms reveal application behavior patterns
- Network capacity planning needs peak rate, not average rate
- QoS policies may need to account for burst patterns

## Simulation Method

**Sender**:
- Uses `BurstTimer` from common/timing.py
- Sends bursts of N packets (default: 10)
- Within burst: tight spacing (default: 1ms)
- Between bursts: longer gap (default: 100ms interval)
- Each packet contains sequence number, burst ID, timestamp

**Receiver**:
- Receives packets and calculates inter-packet gaps
- Builds histogram of IPG values
- Classifies gaps as "small" (<10ms) or "large" (>20ms)
- Verifies bimodal distribution (>70% in two categories)

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
sudo ./infra/run-test.sh udp/bursty-sender

# Auto-start mode
sudo ./infra/run-test.sh udp/bursty-sender --auto
```

## Configuration Options

### Sender options

| Argument | Default | Description |
|----------|---------|-------------|
| `--host` | 10.0.1.2 | Destination address |
| `--port` | 9999 | Destination port |
| `--duration` | 10 | Test duration in seconds |
| `--burst-size` | 10 | Packets per burst |
| `--burst-interval` | 100 | Milliseconds between burst starts |
| `--packet-interval` | 1 | Milliseconds between packets in burst |
| `--payload-size` | 512 | UDP payload size in bytes |

### Receiver options

| Argument | Default | Description |
|----------|---------|-------------|
| `--port` | 9999 | Listen port |
| `--duration` | 15 | Receive duration in seconds |

## Variations

### Tight bursts (more pronounced bimodal)
```bash
# Faster within-burst, longer gap
python3 sender.py --packet-interval 0.5 --burst-interval 150
```

### Large bursts
```bash
# More packets per burst
python3 sender.py --burst-size 20 --burst-interval 200
```

### Micro-burst simulation
```bash
# Very tight burst, short interval
python3 sender.py --burst-size 50 --packet-interval 0.1 --burst-interval 50
```

### Video-like pattern (30fps with frame bursts)
```bash
# Simulate 30fps video with packet bursts per frame
python3 sender.py --burst-size 5 --burst-interval 33.33 --packet-interval 0.5
```

## Self-Check Assertions

The receiver verifies:
1. **Packet count**: Received sufficient packets (>50)
2. **Bimodal distribution**: >70% of IPG values in <10ms or >20ms categories
3. **Dual peaks**: Both small and large IPG categories have significant counts

## tcpdump Commands

Observe burst patterns at packet level:

```bash
# In observer namespace - watch packets with timing
sudo ip netns exec pp-observer tcpdump -i br0 -n 'udp port 9999' -tt

# Count packets per second
sudo ip netns exec pp-observer tcpdump -i br0 -n 'udp port 9999' -c 100 2>&1 | \
    awk '/^[0-9]/{print $1}' | cut -d. -f1 | uniq -c

# Watch packet sizes
sudo ip netns exec pp-observer tcpdump -i br0 -n 'udp port 9999' -v 2>&1 | grep length
```

## Expected Output

### Sender
```
Sending bursts to 10.0.1.2:9999
Burst pattern: 10 packets @ 1.0ms, every 100.0ms
Expected IPG histogram peaks:
  - Within burst: ~1.0ms
  - Between bursts: ~90ms
Starting burst transmission for 10s...
Elapsed: 2.0s, Bursts: 21, Packets: 210 (105/s)
...

BURSTY SENDER TEST RESULTS
Duration: 10.0s
Total packets: 1000
Bursts sent: 100
Average rate: 100.0 packets/s

Self-check results:
  [PASS] Packet count: 1000 packets (expected ~1000)
  [PASS] Burst count: 100 bursts
  [PASS] Packet rate: 100.0 pps
```

### Receiver
```
Receiver listening on port 9999
Waiting for packets...
Elapsed: 2.0s, Packets: 210 (105/s)
...

BURSTY RECEIVER TEST RESULTS
Duration: 10.5s
Total packets: 1000

IPG Statistics:
  Min: 0.85ms
  Max: 91.23ms
  Median: 1.02ms
  Mean: 9.52ms

IPG Histogram:
       <2ms:   900 ( 90.0%) ############################################
       <5ms:    10 (  1.0%)
      <10ms:     0 (  0.0%)
      <20ms:     0 (  0.0%)
      <50ms:     0 (  0.0%)
     <100ms:    90 (  9.0%) ####
     <200ms:     0 (  0.0%)

Self-check results:
  [PASS] Packet count: 1000 packets
  [PASS] Bimodal distribution: 99.0% in <10ms or >20ms buckets
  [PASS] Dual peaks: Small IPG: 910, Large IPG: 90

IPG Distribution Summary:
  Small IPG (<10ms): 910 (91.0%) - within burst
  Medium IPG (10-20ms): 0 - transition
  Large IPG (>20ms): 90 (9.0%) - between bursts
```

## Relationship to Network Problems

Bursty traffic can cause or exacerbate:

| Problem | How bursts contribute |
|---------|----------------------|
| Buffer overflow | Peak rate exceeds buffer capacity |
| Latency spikes | Queue buildup during burst |
| Packet loss | Tail-drop when queue full |
| Jitter | Variable queuing delay per packet |
| TCP performance | Induced loss triggers congestion control |

## References

- RFC 3714 - IAB Concerns Regarding Congestion Control for Voice Traffic
- "On the Self-Similar Nature of Ethernet Traffic" (Leland et al., 1994)
- "Measurement and Analysis of Link-Level Traffic" (Cáceres et al., 1991)
- JitterTrap IPG histogram documentation
