# Jitter Cliff and Chaos Zone

**Last Updated:** 2025-12-29 (Verified Results)

## Question

**"At what jitter level does TCP throughput collapse?"**

## Key Finding

The jitter cliff is **RTT-relative**, not a fixed millisecond value.

**Throughput collapses when jitter exceeds ~16-24% of RTT.**

| RTT | CUBIC Cliff | BBR Cliff | Notes |
|-----|-------------|-----------|-------|
| 24ms | ±8ms (33%) | ±4ms (17%) | BBR more sensitive at low RTT |
| 50ms | ±12ms (24%) | ±8ms (16%) | Verified baseline: 5,302/5,406 KB/s |
| 100ms | ±16ms (16%) | ±16ms (16%) | Both algorithms similar |

## Verified Throughput Values (50ms RTT)

| Jitter | CUBIC | BBR | BBR/CUBIC |
|--------|-------|-----|-----------|
| 0ms | 5,319 KB/s | 5,408 KB/s | 1.0x |
| 4ms (8%) | 4,863 KB/s | 4,934 KB/s | 1.0x |
| 8ms (16%) | 3,655 KB/s | 1,799 KB/s | 0.5x |
| 12ms (24%) | 303 KB/s | 584 KB/s | 1.9x |
| 16ms (32%) | 118 KB/s | 416 KB/s | 3.5x |
| 24ms (48%) | 89 KB/s | 351 KB/s | 3.9x |

## Counter-Intuitive Implication

**Higher RTT = more absolute jitter tolerance**

At ±12ms jitter:
- 24ms RTT: CUBIC 171 KB/s (collapsed)
- 50ms RTT: CUBIC 303 KB/s (collapsed)
- 100ms RTT: CUBIC 2,023 KB/s (functional)

Satellite links with high RTT are paradoxically more tolerant of absolute jitter than low-latency links.

## The Chaos Zone

Near the cliff transition (jitter 16-32% of RTT), results are highly variable:

| Jitter (50ms RTT) | CUBIC CV | BBR CV | Behavior |
|-------------------|----------|--------|----------|
| ±8ms (16%) | 21% | 31% | Entering chaos |
| ±10ms (20%) | **66%** | 18% | CUBIC bifurcation |
| ±12ms (24%) | 47% | 14% | Transition |
| ±14ms (28%) | 19% | 5% | Stable degraded |
| ±16ms (32%) | 15% | 5% | Stable degraded |

**Key Finding:** CUBIC shows bifurcation (CV = 66%) at 20% jitter/RTT ratio, while BBR remains stable (CV = 18%).

## Chaos Zone Root Cause: RTO Mode Bifurcation

The chaos zone variability is caused by TCP entering one of two stable states:

| State | Retransmits | Throughput | Mechanism |
|-------|-------------|------------|-----------|
| RTO Mode | <50 | Low | Timeout-based recovery |
| Fast Retransmit | >500 | High | Duplicate ACK recovery |

**Mechanism:** When cwnd is small at the moment of loss, not enough duplicate ACKs arrive to trigger fast retransmit. TCP falls back to RTO timeout (200ms-1s), effectively stalling.

**Why BBR is more stable:** BBR's pacing keeps packets in flight consistently, ensuring losses always trigger fast retransmit rather than RTO.

## Plots

![Jitter Cliff by RTT](plots/03_jitter_cliff_by_rtt.png)
![Jitter Cliff Normalized](plots/04_jitter_cliff_normalized.png)
![Chaos Zone Analysis](plots/05_chaos_zone.png)

## Data Source

- `results/verified-2025-12-29/jitter-cliff-verification.csv` - 420 runs
- `results/verified-2025-12-29/chaos-zone-statistical.csv` - 400 runs (20 iterations each)
