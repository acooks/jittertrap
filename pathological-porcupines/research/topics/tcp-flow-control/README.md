# TCP Flow Control Research

**Last Updated:** 2025-12-29 (Verified Results)

Research into TCP behavior for video streaming diagnostics.

**Goal:** Answer "Why is my video stuttering - is it the network, the receiver, or the sender?"

## Quick Start

See the [guides/](guides/) for practical recommendations:
- **diagnostic-flowchart.png** - Master decision tree
- **video-quality-guide.png** - Network conditions → video quality
- **congestion-control-guide.png** - When to use BBR vs CUBIC

## Verified Results (2025-12-29)

Total: **1,640 experimental runs** across 7 verification presets.

| Experiment | Runs | Key Finding |
|------------|------|-------------|
| baseline-verification | 80 | 50ms RTT baseline: ~5,300 KB/s |
| loss-tolerance-clean | 140 | BBR 17.6x better at 5% loss |
| jitter-cliff-verification | 420 | Cliff at ~20% jitter/RTT |
| chaos-zone-statistical | 400 | CUBIC CV=66% vs BBR CV=18% |
| starlink-canonical (3 profiles) | 600 | BBR 2.9-4.2x advantage |

## Investigations

| Investigation | Key Finding |
|---------------|-------------|
| [diagnostic-signatures](investigations/diagnostic-signatures/) | Zero-window = receiver; retransmits = network; network masks receiver |
| [jitter-cliff](investigations/jitter-cliff/) | Throughput collapses at jitter ~16-24% of RTT; chaos zone explained by RTO bifurcation |
| [congestion-control](investigations/congestion-control/) | BBR wins at high jitter (>24% RTT) and any loss; CUBIC wins at low jitter; chaos zone is unpredictable |
| [loss-tolerance](investigations/loss-tolerance/) | BBR provides 2.7-17.6x advantage under packet loss |
| [starlink-profiles](investigations/starlink-profiles/) | Real Starlink: 27ms RTT, 7ms jitter, 0.13% loss; BBR 2.9-4.2x advantage |
| [ecn-discrimination](investigations/ecn-discrimination/) | ECE>2 distinguishes queue congestion from random loss |
| [sender-stalls](investigations/sender-stalls/) | Large IPG gaps (>60ms) with healthy window = sender problem |

## Key Thresholds

| Metric | Threshold | Diagnosis |
|--------|-----------|-----------|
| Zero-window | >5 in 10s | Receiver problem |
| Retransmits | >10 in 10s | Network problem |
| ECE | >2 | Queue congestion |
| IPG max | >60ms (window healthy) | Sender stall |
| Jitter/RTT | >20% | Expect throughput collapse |

## Running Experiments

```bash
# From research/topics/tcp-flow-control
cd scripts
sudo ./run-sweep.sh --preset <name> --iterations 3

# List available presets
grep -E "^\s+'[a-z-]+'" scripts/run-sweep.sh
```

### Verification Presets

```bash
# Run the same experiments used for verification
sudo ./run-sweep.sh --preset baseline-verification --iterations 5
sudo ./run-sweep.sh --preset loss-tolerance-clean --iterations 5
sudo ./run-sweep.sh --preset jitter-cliff-verification --iterations 5
sudo ./run-sweep.sh --preset chaos-zone-statistical --iterations 20
sudo ./run-sweep.sh --preset starlink-canonical-baseline --iterations 10
sudo ./run-sweep.sh --preset starlink-canonical-handover --iterations 10
sudo ./run-sweep.sh --preset starlink-canonical-degraded --iterations 10
```

## Directory Structure

```
tcp-flow-control/
├── README.md               # This file
├── RERUN-PLAN.md           # Verification plan and status
├── guides/                 # Synthesized, user-facing outputs
├── investigations/         # Evidence and analysis
│   ├── jitter-cliff/
│   │   ├── README.md       # Verified findings
│   │   └── plots/          # Investigation-specific plots
│   ├── loss-tolerance/
│   ├── congestion-control/
│   ├── starlink-profiles/
│   └── ...
├── plots/
│   └── verified-2025-12-29/  # All verification plots
├── analysis/               # Analysis scripts
│   └── plot_verified_results.py
├── scripts/                # Experiment runners
└── results/
    ├── verified-2025-12-29/  # Verified experiment data
    └── ...                   # Other experiment runs
```

## Regenerating Plots

```bash
cd analysis
python3 plot_verified_results.py
```
