# TCP Flow Control Research

Research into TCP behavior for video streaming diagnostics.

**Goal:** Answer "Why is my video stuttering - is it the network, the receiver, or the sender?"

## Quick Start

See the [guides/](guides/) for practical recommendations:
- **diagnostic-flowchart.png** - Master decision tree
- **video-quality-guide.png** - Network conditions → video quality
- **congestion-control-guide.png** - When to use BBR vs CUBIC

## Investigations

| Investigation | Key Finding |
|---------------|-------------|
| [diagnostic-signatures](investigations/diagnostic-signatures/) | Zero-window = receiver; retransmits = network; network masks receiver |
| [jitter-cliff](investigations/jitter-cliff/) | Throughput collapses at jitter ~20% of RTT; chaos zone explained by RTO bifurcation |
| [congestion-control](investigations/congestion-control/) | BBR wins at high jitter (>30% RTT); CUBIC wins at low jitter; chaos zone is unpredictable |
| [loss-tolerance](investigations/loss-tolerance/) | BBR provides 2-17x advantage under packet loss |
| [starlink-profiles](investigations/starlink-profiles/) | Real Starlink: 25-50ms RTT, <10ms jitter, 0.1-0.2% loss; BBR 2.7-3.5x advantage |
| [ecn-discrimination](investigations/ecn-discrimination/) | ECE>2 distinguishes queue congestion from random loss |
| [sender-stalls](investigations/sender-stalls/) | Large IPG gaps (>60ms) with healthy window = sender problem |

## Key Thresholds

| Metric | Threshold | Diagnosis |
|--------|-----------|-----------|
| Zero-window | >5 in 10s | Receiver problem |
| Retransmits | >10 in 10s | Network problem |
| ECE | >2 | Queue congestion |
| IPG max | >60ms (window healthy) | Sender stall |

## Running Experiments

```bash
# From pathological-porcupines root
sudo ./scripts/run-sweep.sh --preset <name> --iterations 3

# List available presets
grep -E "^\s+'[a-z-]+'" scripts/run-sweep.sh
```

## Directory Structure

```
tcp-flow-control/
├── README.md           # This file
├── guides/             # Synthesized, user-facing outputs
├── investigations/     # Evidence and analysis
│   ├── diagnostic-signatures/
│   ├── jitter-cliff/
│   ├── congestion-control/
│   ├── loss-tolerance/
│   ├── starlink-profiles/
│   ├── ecn-discrimination/
│   └── sender-stalls/
├── scripts/            # Experiment runners
├── analysis/           # Analysis scripts
└── results/            # Raw experiment data
```
