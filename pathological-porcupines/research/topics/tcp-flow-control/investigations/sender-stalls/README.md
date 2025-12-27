# Sender Stall Signatures

## Question

**"How do I distinguish sender stalls from receiver stalls?"**

When video stutters, we can detect receiver problems (zero-window) and network problems (retransmits), but what about sender-side issues? A camera or encoder that stalls produces a different signature.

## Key Finding

Sender stalls produce **zero zero-window events** and **large inter-packet gaps**.

| Stall Type | Zero-Window | Window Health | IPG Gaps | Cause |
|------------|-------------|---------------|----------|-------|
| Receiver stall | Yes (>5) | Drops to 0 | N/A | Receiver buffer full |
| Sender stall | **0** | Healthy | **Large (>60ms)** | Sender application paused |

## Diagnostic Rule

```
IF zero_window = 0 AND IPG_max > max(60ms, RTT × 1.2):
    → Sender stall detected
```

**Accuracy:** 94.4% (51/54 experiments correctly classified)

## RTT Ceiling Limitation

Sender stalls are only detectable when **stall duration > RTT**.

| RTT | Minimum Detectable Stall |
|-----|--------------------------|
| 50ms | >60ms |
| 100ms | >100ms |
| 200ms | >200ms |

**Why:** TCP's ACK-clocked pacing naturally creates inter-packet gaps proportional to RTT. Short stalls hide within normal pacing.

## TCP Idle Restart Effect

Long stalls (>RTO) trigger TCP idle restart (RFC 9293), reducing cwnd:

| Stall Duration | Throughput vs Baseline |
|----------------|----------------------|
| 100ms | 98% (minimal impact) |
| 500ms | 89% |
| 1000ms | 82% |
| 2000ms | **74%** (26% reduction) |

## BBR vs CUBIC Recovery

BBR advantage only emerges on higher-RTT paths:

| Network Delay | BBR Advantage at 2s Stall |
|---------------|--------------------------|
| 0ms (LAN) | 0% (no difference) |
| 25ms | 6.6% |
| 50ms | 11.7% |

## Diagnostic Flowchart

1. Check zero-window events
   - If >5: **Receiver problem**
   - If 0: Continue

2. Check retransmits
   - If >10: **Network problem**
   - If <10: Continue

3. Check inter-packet gaps (from sender IP)
   - If max >60ms with healthy window: **Sender stall**
   - If gaps normal: Check application layer

## Interactive Demo

Location: `tests/tcp-timing/sender-stall/`

```bash
# Server (fast receiver)
sudo ip netns exec pp-dest python3 server.py --port 9999

# Client (stuttering sender) - varying gap pattern
sudo ip netns exec pp-source python3 client.py --host 10.0.1.2 --port 9999
```

## Plots

- `plots/21_sender_stall_discrimination.png` - IPG vs zero-window scatter
- `plots/22_sender_stall_recovery.png` - Throughput penalty by stall duration
- `plots/23_rtt_ceiling_limitation.png` - Why stalls ≤ RTT are invisible
- `plots/24_phase15_sender_stall_summary.png` - Key findings combined
- `plots/25-27_*.png` - JitterTrap screenshots showing the signature
