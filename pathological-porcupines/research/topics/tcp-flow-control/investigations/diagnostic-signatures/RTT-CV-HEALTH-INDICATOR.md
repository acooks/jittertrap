# RTT Coefficient of Variation as TCP Flow Health Indicator

**Date:** 2025-12-29
**Based on:** 1,640 verified experimental runs

## Summary

RTT CV (standard deviation / mean) is a single observable metric that predicts TCP throughput health across all network conditions.

## Key Finding

| RTT CV | Status | Meaning | Expected Throughput |
|--------|--------|---------|---------------------|
| < 0.15 | 🟢 Healthy | Flow operating normally | ~4000 KB/s |
| 0.15-0.30 | 🟡 Stressed | Performance degrading | ~650-1800 KB/s |
| > 0.30 | 🔴 Impaired | Throughput collapsed | < 500 KB/s |

**Separation ratio:** 6.1x between healthy and impaired flows.

## Why RTT CV Works

### 1. Captures Multiple Impairments

RTT CV increases with:
- **Jitter:** Network delay variance directly inflates RTT stddev
- **Packet loss:** Retransmissions add RTT variance
- **Congestion:** Queue buildup causes RTT spikes

```
Pure jitter (no loss):
  0% jitter  → CV 0.02 → 5319 KB/s 🟢
  16% jitter → CV 0.18 → 3655 KB/s 🟡
  24% jitter → CV 0.46 →  303 KB/s 🔴

Pure loss (no jitter):
  0% loss    → CV 0.02 → 5332 KB/s 🟢
  1% loss    → CV 0.24 →  396 KB/s 🟡
  5% loss    → CV 0.54 →  119 KB/s 🔴
```

### 2. RTT-Independent

Unlike absolute stddev, CV works across all RTT values:

| RTT | 5ms stddev means... |
|-----|---------------------|
| 24ms | CV = 0.21 (stressed) |
| 50ms | CV = 0.10 (healthy) |
| 100ms | CV = 0.05 (healthy) |

### 3. Maps to TCP Internals

TCP's RTO calculation (RFC 6298):
```
RTO = SRTT + max(G, K × RTTVAR)
```

High RTT CV → High RTTVAR/SRTT ratio → Inflated RTO → Slow loss recovery → Throughput collapse

### 4. Observable in Real-Time

Can be calculated from:
- TCP timestamps (sender-side)
- ACK timing (receiver-side)
- Packet capture (observer)

## Correlation Analysis

| Metric | Correlation with Throughput | Observability |
|--------|----------------------------|---------------|
| window_oscillations | +0.74 | Requires TCP state |
| **rtt_cv** | **-0.67** | **From timestamps** |
| rtt_p99/p50 ratio | -0.55 | From timestamps |
| rtt_stddev | -0.51 | From timestamps |
| inter_packet_gap_p95 | -0.51 | From packets |
| retransmit_count | -0.23 | Cumulative only |

RTT CV has the best balance of predictive power and observability.

## Threshold Validation

From 1,640 experimental runs:

| Threshold | Healthy Mean | Unhealthy Mean | Separation |
|-----------|--------------|----------------|------------|
| CV < 0.10 | 4549 KB/s | 783 KB/s | 5.8x |
| CV < 0.15 | 3962 KB/s | 650 KB/s | 6.1x |
| CV < 0.20 | 3674 KB/s | 512 KB/s | 7.2x |
| CV < 0.30 | 3272 KB/s | 439 KB/s | 7.5x |

**Recommended thresholds:**
- `CV < 0.15` for healthy (21% of samples)
- `CV 0.15-0.30` for stressed (11% of samples)
- `CV > 0.30` for impaired (68% of samples)

## Implementation

### Data Structure

```c
#define RTT_WINDOW_SIZE 20  // ~1 second at typical packet rate

struct flow_health {
    double rtt_samples[RTT_WINDOW_SIZE];
    int sample_count;
    int sample_index;
};
```

### Update Function

```c
void update_rtt_sample(struct flow_health *flow, double rtt_ms) {
    flow->rtt_samples[flow->sample_index] = rtt_ms;
    flow->sample_index = (flow->sample_index + 1) % RTT_WINDOW_SIZE;
    if (flow->sample_count < RTT_WINDOW_SIZE)
        flow->sample_count++;
}
```

### Calculate CV

```c
double calculate_rtt_cv(struct flow_health *flow) {
    if (flow->sample_count < 2) return 0.0;

    double sum = 0, sum_sq = 0;
    for (int i = 0; i < flow->sample_count; i++) {
        sum += flow->rtt_samples[i];
        sum_sq += flow->rtt_samples[i] * flow->rtt_samples[i];
    }

    double mean = sum / flow->sample_count;
    double variance = (sum_sq / flow->sample_count) - (mean * mean);
    double stddev = sqrt(variance);

    return mean > 0 ? stddev / mean : 0.0;
}
```

### Health Classification

```c
enum health_status { HEALTHY, STRESSED, IMPAIRED };

enum health_status get_flow_health(double cv) {
    if (cv < 0.15) return HEALTHY;
    if (cv < 0.30) return STRESSED;
    return IMPAIRED;
}
```

## RTT Measurement Methods

### 1. TCP Timestamps (Best)
If TSopt is present, calculate RTT from:
```
RTT = current_time - timestamp_echo_reply
```

### 2. SYN/SYN-ACK (Connection Only)
```
RTT = time(SYN-ACK received) - time(SYN sent)
```

### 3. Data/ACK Pairs (Approximate)
Match sequence numbers to estimate RTT:
```
RTT = time(ACK for seq N) - time(packet with seq N)
```

## Caveats

1. **Minimum samples:** Need at least 10-20 RTT samples for stable CV
2. **Delayed ACKs:** May inflate apparent RTT variance
3. **Multipath:** Different paths show different RTTs (expected high CV)
4. **Initial phase:** CV unstable during slow start

## Data Source

- `results/verified-2025-12-29/jitter-cliff-verification.csv` - 420 runs
- `results/verified-2025-12-29/loss-tolerance-clean.csv` - 140 runs
- `results/verified-2025-12-29/chaos-zone-statistical.csv` - 400 runs
- `results/verified-2025-12-29/starlink-canonical-*.csv` - 600 runs
