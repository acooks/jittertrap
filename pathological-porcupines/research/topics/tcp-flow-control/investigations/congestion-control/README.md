# BBR vs CUBIC Congestion Control

## Question

**"Which congestion control algorithm should I use?"**

## Key Finding

Neither is universally better. The choice depends on network conditions.

| Jitter Region | Recommendation |
|---------------|----------------|
| < 10% of RTT | CUBIC (default) |
| 10-25% of RTT | Test both (chaos zone) |
| > 30% of RTT | BBR (3-5x advantage) |

## Degradation Patterns

**CUBIC:** Sharp cliff, then flatlines at ~100-200 KB/s floor

**BBR:** Gradual degradation, maintains ~400-750 KB/s floor

BBR degrades gracefully; CUBIC collapses sharply.

## Surprising Result: BBR Can Lose

In the chaos zone, BBR can be **worse** than CUBIC:

| Condition | BBR/CUBIC Ratio |
|-----------|-----------------|
| 50ms RTT, ±8ms jitter | 0.33x (BBR worse) |
| 100ms RTT, ±16ms jitter | 0.72x (BBR worse) |
| 100ms RTT, ±20ms jitter | 0.52x (BBR worse) |

This appears related to BBR's pacing interacting poorly with specific jitter patterns.

## Practical Recommendation

```bash
# Enable BBR (requires kernel 4.9+)
sudo sysctl -w net.ipv4.tcp_congestion_control=bbr

# Check current algorithm
sysctl net.ipv4.tcp_congestion_control
```

CUBIC is Linux default since kernel 2.6.19. When in doubt, BBR rarely hurts on high-jitter or lossy links.

## Plots

- `plots/04_bbr_cubic_heatmap.png` - When each algorithm wins
- `plots/11_throughput_by_rtt.png` - Throughput comparison by RTT
