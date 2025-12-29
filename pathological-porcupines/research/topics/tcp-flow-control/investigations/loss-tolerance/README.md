# Loss Tolerance: BBR Dominates

**Last Updated:** 2025-12-29 (Verified Results)

## Question

**"How does packet loss affect TCP throughput, and which algorithm handles it better?"**

## Key Finding

BBR provides **2.7-17.6x throughput advantage** over CUBIC under packet loss.

| Loss Rate | CUBIC (KB/s) | BBR (KB/s) | BBR Advantage |
|-----------|--------------|------------|---------------|
| 0% | 5,332 | 5,405 | 1.0x |
| 0.1% | 1,811 | 4,832 | **2.7x** |
| 0.25% | 1,016 | 4,224 | **4.2x** |
| 0.5% | 595 | 3,605 | **6.1x** |
| 1% | 396 | 3,101 | **7.8x** |
| 2% | 247 | 2,610 | **10.6x** |
| 5% | 119 | 2,100 | **17.6x** |

*Verified at 50ms RTT, 0% jitter, 5 iterations per condition*

## Why CUBIC Collapses

CUBIC is loss-based: it interprets every packet loss as congestion and halves its window. On lossy links (wireless, satellite), this causes:
1. Repeated window cuts from non-congestion losses
2. Throughput spirals downward
3. Cannot recover even when link is uncongested

At just 0.1% loss, CUBIC throughput drops to **34%** of baseline.

## Why BBR Survives

BBR is model-based: it estimates bandwidth and RTT independently of loss. It maintains throughput as long as the link has capacity, treating random losses as noise rather than congestion signals.

At 0.1% loss, BBR maintains **89%** of baseline throughput.

## Practical Implication

**Always use BBR on lossy networks.** Even 0.1% loss causes CUBIC throughput to drop 66%, while BBR maintains 89% efficiency.

This is especially relevant for:
- Wireless links (WiFi, LTE)
- Satellite (Starlink)
- Long-distance internet paths

## Plots

![Loss Tolerance](plots/02_loss_tolerance.png)
![BBR Advantage](plots/07_bbr_advantage.png)

## Data Source

- `results/verified-2025-12-29/loss-tolerance-clean.csv` - 140 runs
- `results/verified-2025-12-29/baseline-verification.csv` - 80 runs
