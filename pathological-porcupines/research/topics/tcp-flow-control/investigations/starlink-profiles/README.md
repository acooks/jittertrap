# Starlink Network Profiles

## Question

**"What video quality can I expect over Starlink, and how should I configure for it?"**

## Real Starlink Characteristics

Based on published measurements (APNIC, WirelessMoves, Starlink):

| Metric | Typical Range | Notes |
|--------|---------------|-------|
| RTT | 25-50ms median | Starlink reports 25.7ms median US latency |
| Jitter | <10ms typical, 30-50ms at handover | Avg 6.7ms, spikes every 15s |
| Packet Loss | 0.1-0.2% baseline, ~1% at handover | Not congestion-related |

**Key insight:** Starlink loss is caused by satellite handovers and radio impairments, not congestion.

## Tested Profiles

### Realistic Conditions (from published data)

| Profile | RTT | Jitter | Loss | CUBIC | BBR | BBR Advantage |
|---------|-----|--------|------|-------|-----|---------------|
| Baseline | 24-30ms | ±3-7ms | 0.1-0.2% | 1,137 KB/s | 3,114 KB/s | **2.7x** |
| Handover | 50-80ms | ±15-25ms | 0.5-1.0% | 143 KB/s | 387 KB/s | **2.7x** |
| Degraded | 60-90ms | ±10-20ms | 1-2% | 163 KB/s | 567 KB/s | **3.5x** |

### Synthetic Stress-Test (for extreme testing)

| Profile | RTT | Jitter | Loss | Notes |
|---------|-----|--------|------|-------|
| Excellent | 50ms | ±5ms | 0.05% | Optimistic |
| Normal | 100ms | ±20ms | 0.3% | Moderate stress |
| Degraded | 200ms | ±40ms | 1.5% | High stress |
| Severe | 500ms | ±75ms | 3.0% | **Unrealistic** (GEO-like) |

Note: The "Severe" profile exceeds real Starlink - closer to geostationary satellite.

## Video Quality Expectations

| Condition | With CUBIC | With BBR | Recommendation |
|-----------|------------|----------|----------------|
| Baseline | 720p | 1080p-4K | BBR strongly recommended |
| Handover | 360p | 480p | BBR + 15s buffer |
| Degraded | 360p | 480p | BBR essential |

## Practical Recommendations

1. **Use BBR** - provides 2.7-3.5x advantage across all conditions
2. **Size buffers for jitter** - at least 2× the jitter spike duration
3. **Expect brief degradation every 15s** - satellite handovers cause spikes
4. **Target 2-5 Mbps for reliable streaming** - don't count on 100+ Mbps during handovers

## Sources

- [APNIC: A Transport Protocol's View of Starlink](https://blog.apnic.net/2024/05/17/a-transport-protocols-view-of-starlink/)
- [WirelessMoves: Analyzing Packet Loss in Starlink](https://blog.wirelessmoves.com/2024/07/analyzing-packet-loss-in-starlink.html)
- [Starlink Latency Paper](https://starlink.com/public-files/StarlinkLatency.pdf)

## Plots

- `plots/09_starlink_profiles.png` - Synthetic stress-test profiles
- `plots/20_starlink_realistic_comparison.png` - Realistic Starlink comparison
