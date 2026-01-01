# Starlink Network Profiles

**Last Updated:** 2025-12-29 (Verified Results)

## Question

**"What video quality can I expect over Starlink, and how should I configure for it?"**

## Real Starlink Characteristics

Based on published measurements (APNIC, WirelessMoves, Starlink):

| Metric | Cited Value | Source |
|--------|-------------|--------|
| RTT | 27ms median | Starlink official US latency |
| Jitter | 7ms average, 40ms at handover | APNIC |
| Packet Loss | 0.13% baseline, ~1% at handover | WirelessMoves, APNIC |

**Key insight:** Starlink loss is caused by satellite handovers and radio impairments, not congestion.

## Verified Profiles (Canonical Test Parameters)

### Baseline (27ms RTT, 7ms jitter)

| Loss | CUBIC | BBR | BBR Advantage |
|------|-------|-----|---------------|
| 0.10% | 317 KB/s | 910 KB/s | **2.9x** |
| 0.125% | 292 KB/s | 918 KB/s | **3.1x** |
| 0.15% | 291 KB/s | 907 KB/s | **3.1x** |
| 0.175% | 308 KB/s | 909 KB/s | **2.9x** |
| 0.20% | 283 KB/s | 894 KB/s | **3.2x** |

*Verified with 10 iterations per condition*

### Handover (60ms RTT, 40ms jitter)

Extreme conditions during satellite handover events:

| Loss | CUBIC | BBR | BBR Advantage |
|------|-------|-----|---------------|
| 0.50% | 64 KB/s | 276 KB/s | **4.3x** |
| 0.75% | 65 KB/s | 269 KB/s | **4.1x** |
| 1.00% | 65 KB/s | 270 KB/s | **4.2x** |
| 1.25% | 63 KB/s | 267 KB/s | **4.3x** |
| 1.50% | 61 KB/s | 262 KB/s | **4.3x** |

**Key finding:** CUBIC is essentially unusable during handovers (~65 KB/s).

### Degraded (80ms RTT, 15ms jitter)

Extended degradation periods:

| Loss | CUBIC | BBR | BBR Advantage |
|------|-------|-----|---------------|
| 1.00% | 204 KB/s | 445 KB/s | **2.2x** |
| 1.25% | 195 KB/s | 451 KB/s | **2.3x** |
| 1.50% | 160 KB/s | 457 KB/s | **2.9x** |
| 1.75% | 154 KB/s | 449 KB/s | **2.9x** |
| 2.00% | 133 KB/s | 421 KB/s | **3.2x** |

## Summary: BBR Advantage by Profile

| Profile | Conditions | CUBIC | BBR | Ratio |
|---------|------------|-------|-----|-------|
| **Baseline** | 27ms, 7ms, 0.13% | 292 KB/s | 918 KB/s | **3.1x** |
| **Handover** | 60ms, 40ms, 1.0% | 65 KB/s | 270 KB/s | **4.2x** |
| **Degraded** | 80ms, 15ms, 1.5% | 160 KB/s | 457 KB/s | **2.9x** |

## Video Quality Expectations

| Condition | With CUBIC | With BBR | Min Bitrate |
|-----------|------------|----------|-------------|
| Baseline | 360p choppy | 720p smooth | ~300-900 KB/s |
| Handover | Unusable | 360p barely | ~65-270 KB/s |
| Degraded | 360p choppy | 480p usable | ~160-460 KB/s |

## Practical Recommendations

1. **Use BBR** - provides 2.9-4.2x advantage across all Starlink conditions
2. **Size buffers for 40ms jitter spikes** - handovers cause large jitter bursts
3. **Expect brief degradation every 15s** - satellite handovers cause regular spikes
4. **Plan for 270 KB/s minimum during handovers** - use adaptive bitrate streaming

## Sources

- [APNIC: A Transport Protocol's View of Starlink](https://blog.apnic.net/2024/05/17/a-transport-protocols-view-of-starlink/)
- [WirelessMoves: Analyzing Packet Loss in Starlink](https://blog.wirelessmoves.com/2024/07/analyzing-packet-loss-in-starlink.html)
- [Starlink Latency Paper](https://starlink.com/public-files/StarlinkLatency.pdf)

## Plots

![Starlink Profiles](plots/06_starlink_profiles.png)

## Data Source

- `results/verified-2025-12-29/starlink-canonical-baseline.csv` - 200 runs
- `results/verified-2025-12-29/starlink-canonical-handover.csv` - 200 runs
- `results/verified-2025-12-29/starlink-canonical-degraded.csv` - 200 runs
