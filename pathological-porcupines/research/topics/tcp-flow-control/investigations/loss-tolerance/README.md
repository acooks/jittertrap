# Loss Tolerance: BBR Dominates

## Question

**"How does packet loss affect TCP throughput, and which algorithm handles it better?"**

## Key Finding

BBR provides **2-17x throughput advantage** over CUBIC under packet loss.

| Loss Rate | CUBIC (KB/s) | BBR (KB/s) | BBR Advantage |
|-----------|--------------|------------|---------------|
| 0% | 4,817 | 4,932 | 1.0x |
| 0.1% | 1,834 | 4,471 | **2.4x** |
| 0.25% | 1,001 | 4,093 | **4.1x** |
| 0.5% | 708 | 3,547 | **5.0x** |
| 1% | 384 | 3,133 | **8.2x** |
| 2% | 265 | 2,724 | **10.3x** |
| 5% | 129 | 2,173 | **16.8x** |

## Why CUBIC Collapses

CUBIC is loss-based: it interprets every packet loss as congestion and halves its window. On lossy links (wireless, satellite), this causes:
1. Repeated window cuts from non-congestion losses
2. Throughput spirals downward
3. Cannot recover even when link is uncongested

## Why BBR Survives

BBR is model-based: it estimates bandwidth and RTT independently of loss. It maintains throughput as long as the link has capacity, treating random losses as noise rather than congestion signals.

## Practical Implication

**Always use BBR on lossy networks.** Even 0.1% loss causes CUBIC throughput to drop 62%, while BBR maintains 90% efficiency.

This is especially relevant for:
- Wireless links (WiFi, LTE)
- Satellite (Starlink)
- Long-distance internet paths

## Plots

- `plots/03_loss_tolerance_bbr_dominates.png` - Loss vs throughput comparison
- `plots/16d_bbr_loss_tolerance.png` - Detailed BBR behavior under loss
