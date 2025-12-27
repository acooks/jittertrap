# Jitter Cliff and Chaos Zone

## Question

**"At what jitter level does TCP throughput collapse?"**

## Key Finding

The jitter cliff is **RTT-relative**, not a fixed millisecond value.

**Throughput collapses when jitter exceeds ~20% of RTT.**

| RTT | Cliff Location | As % of RTT |
|-----|----------------|-------------|
| 24ms | ±4-8ms | 17-33% |
| 50ms | ±8-12ms | 16-24% |
| 100ms | ±16-24ms | 16-24% |

## Counter-Intuitive Implication

**Higher RTT = more jitter tolerance**

At ±12ms jitter:
- 50ms RTT: 368 KB/s (collapsed)
- 100ms RTT: 2,061 KB/s (functional)

Satellite links with high RTT are paradoxically more tolerant of absolute jitter than low-latency links.

## The Chaos Zone

Near the cliff transition (jitter 10-25% of RTT), results are highly variable:

| Region | Coefficient of Variation | Behavior |
|--------|--------------------------|----------|
| Below cliff | 1-5% | Stable, predictable |
| At cliff | 30-115% | Chaotic, bimodal |
| Above cliff | 5-20% | Stable but degraded |

**Implication:** Don't trust single measurements near the cliff. Run multiple tests.

## Chaos Zone Root Cause: RTO Mode Bifurcation

The chaos zone variability is caused by TCP entering one of two stable states:

| State | Retransmits | Throughput | Collapse Rate |
|-------|-------------|------------|---------------|
| RTO Mode | <50 | Low | 58% |
| Fast Retransmit Mode | >500 | High | 19% |

**Mechanism:** When cwnd is small at the moment of loss, not enough duplicate ACKs arrive to trigger fast retransmit. TCP falls back to RTO timeout (200ms-1s), effectively stalling.

**Why BBR is more stable:** BBR's pacing keeps packets in flight consistently, ensuring losses always trigger fast retransmit rather than RTO.

## Plots

- `plots/05_jitter_cliff_rtt_relative.png` - Cliff location vs RTT
- `plots/06_chaos_zone_phase11.png` - Initial chaos zone analysis
- `plots/08_cv_chaos_zones.png` - Coefficient of variation heatmap
- `plots/15_rto_bifurcation.png` - RTO vs fast retransmit bifurcation
