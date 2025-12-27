# Diagnostic Signatures: Receiver vs Network Problems

## Question

**"Why is my video stuttering - is it the network or the receiver?"**

## Key Finding

TCP metrics can reliably distinguish between receiver problems and network problems, but **network problems mask receiver problems**.

| Problem Type | Zero-Window Events | Retransmits | Detection |
|--------------|-------------------|-------------|-----------|
| Receiver overload | HIGH (>5) | Low | 77% accuracy |
| Network impairment | Zero | HIGH (>10) | 84% accuracy |
| Compound (both) | Zero (masked!) | High | 0% - appears as network only |

## The Masking Effect

When network impairment exists, TCP congestion control throttles the sender. The receiver never gets overwhelmed because data arrives slowly. Zero-window events never occur - even if the receiver is slow.

**Implication:** If you detect a network problem, fix it first, then re-test. A receiver problem may become visible once the network is healthy.

## Diagnostic Thresholds

| Metric | Threshold | Diagnosis |
|--------|-----------|-----------|
| Zero-window events | > 5 in 10s | Receiver problem (definite) |
| Retransmits | > 10 in 10s | Network problem (receiver status unknown) |
| Both below threshold | - | Healthy OR problem too mild to detect |

## Secondary Findings

### Read Size Effect (H5)
Smaller read sizes produce more zero-window events at the same processing rate:
- 1KB reads: ~50 zero-window events
- 64KB reads: 0 zero-window events

**Implication:** Use larger read sizes (16-64KB) to reduce zero-window noise.

### Buffer Sizing
Buffer must cover both stall absorption and bandwidth-delay product:
```
buffer = max(stall × bitrate, RTT × bandwidth) × 1.5
```

## Plots

- `plots/02_diagnostic_discrimination.png` - Zero-window vs retransmit scatter
- `plots/07_diagnostic_confusion_matrix.png` - Classification accuracy
- `plots/10_read_size_h5_validation.png` - Read size effect
- `plots/12_buffer_sizing_summary.png` - Buffer recommendations
- `plots/13_bdp_coverage.png` - Bandwidth-delay product analysis
- `plots/14_stall_tolerance.png` - CPU stall tolerance curves
