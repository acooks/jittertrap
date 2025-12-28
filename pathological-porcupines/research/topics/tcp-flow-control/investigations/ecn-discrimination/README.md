# ECN: Distinguishing Congestion from Random Loss

## Question

**"Is the packet loss from network congestion or random drops?"**

## Key Finding

ECN (Explicit Congestion Notification) can distinguish between:
- **Queue congestion** (ECE flags present) - router buffers filling up
- **Random packet loss** (no ECE) - interference, cable issues, wireless

## The Mechanism

1. TCP handshake includes 2 ECE packets (baseline)
2. When routers mark packets with CE (Congestion Experienced), receiver sends ECE
3. **ECE > 2** indicates congestion beyond the handshake baseline

## Diagnostic Rules

| ECE>2 | Retransmits>10 | Zero-Window>5 | Diagnosis |
|-------|----------------|---------------|-----------|
| No | No | No | Healthy |
| No | No | Yes | Receiver overload |
| No | Yes | - | Random loss (not congestion) |
| Yes | No | - | Queue congestion |
| Yes | Yes | - | Congestion + loss |

## Accuracy

| Scenario | Detection Rate |
|----------|---------------|
| Congestion only | 100% |
| Random loss only | 97% |
| Mixed | 71% |

## Practical Value

ECN adds discrimination power:
- **Queue buildup** → reduce sending rate (the network is saturated)
- **Random packet loss** → network path issue, rate reduction won't help

## Enabling ECN

```bash
# Enable ECN (sender requests ECN-capable connections)
sudo sysctl -w net.ipv4.tcp_ecn=1

# Passive mode (respond to ECN but don't request)
sudo sysctl -w net.ipv4.tcp_ecn=2
```

Note: Both endpoints AND intermediate routers must support ECN for it to work.

## Plots

- `plots/16a_ecn_scatter_discrimination.png` - ECE vs retransmit scatter
- `plots/16b_ecn_decision_tree.png` - ECN-enhanced decision tree
- `plots/16c_ecn_diagnostic_accuracy.png` - Accuracy by scenario
