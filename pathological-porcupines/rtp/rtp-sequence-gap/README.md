# RTP Sequence Gap Demonstration

## Overview

Demonstrates RTP packet loss detection through sequence number discontinuities. The sender intentionally skips sequence numbers to simulate packet loss, and the receiver detects these gaps and counts the missing packets. This is how media applications detect packet loss in real-time streams.

## Network Effect

- **Normal flow**: Sequential packets (seq 100, 101, 102, ...)
- **Gap event**: Skip in sequence (seq 100, 101, 105, ...) - 3 packets "lost"
- **Result**: Receiver detects seq_loss events at predictable intervals

## JitterTrap Indicators

| Metric | Expected Value | Why |
|--------|---------------|-----|
| seq_loss | Increments on each gap | Counts sequence discontinuities |
| Loss rate | Matches injected rate | (lost_packets / total_expected) |
| Gap events | ~7 events in 15s | One every 2 seconds |
| Flow details | Loss events logged | Shows sequence discontinuity |

## Root Cause

Packet loss in RTP streams occurs due to:

1. **Network congestion**: Router drops packets when queue full
2. **Buffer overflow**: Receiver can't keep up with arrival rate
3. **Link errors**: CRC failures on physical layer
4. **QoS policies**: Traffic shaping drops excess packets
5. **Firewall/NAT**: Stateful inspection times out flow

**How RTP detects loss**:
- Each packet has 16-bit sequence number (wraps at 65535)
- Receiver tracks expected sequence
- Gaps indicate missing packets
- Can distinguish loss from reordering

**Why it matters**:
- Video shows artifacts (macroblocking, freezes)
- Audio has dropouts or clicks
- FEC/retransmission can compensate (if available)
- Loss concealment algorithms hide minor loss

## Simulation Method

**Sender**:
- Sends RTP packets at 30 fps
- Every 2 seconds (configurable), skips 1-3 sequence numbers
- Uses `RTPPacket.skip_sequence()` to advance without sending
- Timestamps continue normally

**Receiver**:
- Tracks expected sequence number
- Detects when received seq != expected
- Distinguishes gaps from reordering (using sequence history)
- Counts total lost packets and gap events

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
sudo ./infra/run-test.sh rtp/rtp-sequence-gap

# Auto-start mode
sudo ./infra/run-test.sh rtp/rtp-sequence-gap --auto
```

## Configuration Options

### Sender options

| Argument | Default | Description |
|----------|---------|-------------|
| `--host` | 10.0.1.2 | Destination address |
| `--port` | 9999 | Destination port |
| `--duration` | 15 | Test duration in seconds |
| `--frame-rate` | 30 | Frames per second |
| `--gap-interval` | 2.0 | Seconds between gap events |
| `--gap-min` | 1 | Minimum packets to skip |
| `--gap-max` | 3 | Maximum packets to skip |
| `--payload-size` | 1200 | RTP payload size in bytes |

### Receiver options

| Argument | Default | Description |
|----------|---------|-------------|
| `--port` | 9999 | Listen port |
| `--duration` | 20 | Receive duration in seconds |

## Variations

### Frequent small gaps
```bash
python3 sender.py --gap-interval 1.0 --gap-min 1 --gap-max 1
```

### Burst loss (simulating congestion)
```bash
python3 sender.py --gap-interval 3.0 --gap-min 5 --gap-max 10
```

### High loss rate
```bash
python3 sender.py --gap-interval 0.5 --gap-min 1 --gap-max 2
```

### Sporadic loss (rare but severe)
```bash
python3 sender.py --gap-interval 5.0 --gap-min 10 --gap-max 20
```

## Self-Check Assertions

The receiver verifies:
1. **Packet count**: Received sufficient packets (>100)
2. **Gap events detected**: At least 3 gap events observed
3. **Lost packet count**: Total lost matches gap event sizes

## Sequence Number Handling

The receiver handles these cases:

| Case | Description | Action |
|------|-------------|--------|
| seq == expected | Normal in-order | Continue |
| seq > expected (small gap) | Packet loss | Count gap, log event |
| seq < expected (small diff) | Late/reordered | Count as out-of-order |
| seq in history | Duplicate | Count as duplicate |
| seq > expected (large gap) | Major event | Log warning, reset |

## tcpdump Commands

Observe sequence gaps at packet level:

```bash
# In observer namespace - decode RTP
sudo ip netns exec pp-observer tcpdump -i br0 -n 'udp port 9999' -X -c 30 2>&1 | \
    grep -A2 "0x0000"

# Watch sequence progression
sudo ip netns exec pp-observer tcpdump -i br0 -n 'udp port 9999' --immediate-mode 2>&1 | \
    head -50

# Count packets per second (detect gaps as rate dips)
watch -n1 'sudo ip netns exec pp-observer tcpdump -i br0 -n "udp port 9999" -c 30 2>&1 | wc -l'
```

## Expected Output

### Sender
```
Sending RTP to 10.0.1.2:9999
Frame rate: 30.0 fps
Sequence gaps: 1-3 packets every 2.0s
Starting RTP transmission for 15s...
CREATING SEQUENCE GAP: skipping 2 sequence number(s)
Elapsed: 2.0s, Packets: 60 (30.0 fps), Gap events: 1, Total skipped: 2
CREATING SEQUENCE GAP: skipping 1 sequence number(s)
Elapsed: 4.0s, Packets: 120 (30.0 fps), Gap events: 2, Total skipped: 3
...

RTP SEQUENCE GAP SENDER RESULTS
Duration: 15.0s
Packets sent: 450
Gap events created: 7
Total packets skipped: 14
Gap details:
  Gap 1: 2 packet(s) at 2.0s
  Gap 2: 1 packet(s) at 4.0s
  ...
Simulated loss rate: 3.01%

Self-check results:
  [PASS] Gap events: 7 events (expected ~7)
  [PASS] Packets skipped: 14 packets skipped
```

### Receiver
```
Receiver listening on port 9999
Waiting for RTP packets...
Tracking SSRC: 0xA1B2C3D4
SEQUENCE GAP detected: expected 60, got 62 (2 packet(s) lost) at 2.0s
SEQUENCE GAP detected: expected 122, got 123 (1 packet(s) lost) at 4.1s
...

RTP SEQUENCE GAP RECEIVER RESULTS
Duration: 15.5s
Packets received: 450

Packet Loss Statistics:
  Gap events detected: 7
  Total packets lost: 14
  Out of order packets: 0
  Duplicate packets: 0
  Loss rate: 3.01%

Gap Event Details:
  Gap 1: at 2.0s, seq 60->62, 2 packet(s) lost
  Gap 2: at 4.1s, seq 122->123, 1 packet(s) lost
  ...
  Average gap interval: 2.0s

Self-check results:
  [PASS] Packet count: 450 packets
  [PASS] Gap events detected: 7 events
  [PASS] Lost packet count: 14 packets lost
```

## Loss vs Reordering vs Duplicates

The receiver distinguishes between:

| Event | Detection | Typical Cause |
|-------|-----------|---------------|
| Loss | seq > expected, not in history | Network drop |
| Reorder | seq < expected, not in history | Path change |
| Duplicate | seq in recent history | Retransmission bug |
| Late | seq < expected, small diff | Minor reorder |

## References

- RFC 3550 - RTP: A Transport Protocol for Real-Time Applications
- RFC 3611 - RTP Control Protocol Extended Reports (RTCP XR)
- RFC 7002 - RTP Control Protocol (RTCP) Extended Report for Post-Repair Loss
- "Packet Loss Concealment for Audio Streams" (ITU-T G.729 Annex A)
- JitterTrap packet loss detection documentation
