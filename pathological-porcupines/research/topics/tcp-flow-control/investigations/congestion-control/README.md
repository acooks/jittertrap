# BBR vs CUBIC Congestion Control

**Last Updated:** 2025-12-29 (Verified Results)

## Question

**"Which congestion control algorithm should I use?"**

## Key Finding

Neither is universally better. The choice depends on network conditions.

| Condition | Recommendation | Verified Ratio |
|-----------|----------------|----------------|
| Low jitter (< 16% of RTT) | Either works | ~1.0x |
| Moderate jitter (16-24% of RTT) | Test both (chaos zone) | Variable |
| High jitter (> 24% of RTT) | BBR | 1.9-3.9x |
| Any packet loss | BBR | 2.7-17.6x |

## Verified Performance Comparison

### Under Jitter (50ms RTT, 0% loss)

| Jitter | CUBIC | BBR | BBR/CUBIC |
|--------|-------|-----|-----------|
| 0ms (0%) | 5,319 KB/s | 5,408 KB/s | 1.0x |
| 8ms (16%) | 3,655 KB/s | 1,799 KB/s | **0.5x** (CUBIC wins) |
| 12ms (24%) | 303 KB/s | 584 KB/s | **1.9x** (BBR wins) |
| 16ms (32%) | 118 KB/s | 416 KB/s | **3.5x** (BBR wins) |

### Under Loss (50ms RTT, 0% jitter)

| Loss | CUBIC | BBR | BBR/CUBIC |
|------|-------|-----|-----------|
| 0% | 5,332 KB/s | 5,405 KB/s | 1.0x |
| 0.1% | 1,811 KB/s | 4,832 KB/s | **2.7x** |
| 1% | 396 KB/s | 3,101 KB/s | **7.8x** |
| 5% | 119 KB/s | 2,100 KB/s | **17.6x** |

## Surprising Result: BBR Can Lose

In the transition zone (16% jitter/RTT ratio), BBR can be **worse** than CUBIC:

| Condition | BBR/CUBIC | Notes |
|-----------|-----------|-------|
| 50ms RTT, ±8ms jitter (16%) | 0.49x | BBR worse |
| 24ms RTT, ±4ms jitter (17%) | 0.69x | BBR worse |

This occurs because BBR's pacing interacts poorly with specific jitter patterns at the transition point. Once past the cliff, BBR recovers its advantage.

## Chaos Zone Stability

At the jitter cliff, CUBIC shows extreme variance while BBR remains stable:

| Metric | CUBIC | BBR |
|--------|-------|-----|
| CV at 20% jitter/RTT | **66%** | 18% |
| Bifurcation | Yes | No |
| Predictability | Poor | Good |

Even when BBR has lower mean throughput at certain jitter levels, its stability makes it more suitable for real-time applications.

## Practical Recommendation

```bash
# Enable BBR (requires kernel 4.9+)
sudo sysctl -w net.ipv4.tcp_congestion_control=bbr

# Check current algorithm
sysctl net.ipv4.tcp_congestion_control
```

**Rule of thumb:** Use BBR for:
- Satellite links (Starlink, etc.)
- Wireless networks (WiFi, LTE)
- Any link with packet loss
- Real-time applications (video streaming)

Use CUBIC for:
- Low-latency datacenter networks
- Stable wired connections with minimal jitter

## Plots

![BBR Advantage](plots/07_bbr_advantage.png)

## Data Source

- `results/verified-2025-12-29/jitter-cliff-verification.csv` - 420 runs
- `results/verified-2025-12-29/loss-tolerance-clean.csv` - 140 runs
- `results/verified-2025-12-29/chaos-zone-statistical.csv` - 400 runs
