#!/usr/bin/env python3
"""
Chaos Zone Analysis - Phase 11

Analyzes TCP behavior in the "chaos zone" where throughput variance explodes
(CV 30-115%) at jitter levels 10-25% of RTT.

Tests 5 hypotheses:
- H-C1: RTO vs Fast Retransmit bifurcation
- H-C2: Spurious timeout from RTT spikes
- H-C3: Dup-ACK threshold race condition
- H-C4: CUBIC convex growth instability
- H-C5: Netem burst losses (artifact check)
"""

import os
import sys
import pandas as pd
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from pathlib import Path

# Find experiment directory
SCRIPT_DIR = Path(__file__).parent
EXPERIMENT_DIR = SCRIPT_DIR.parent
RESULTS_DIR = EXPERIMENT_DIR / "results"


def load_chaos_zone_data():
    """Load both new chaos zone captures and historical cc-rtt-sweep data."""
    data = {}

    # New chaos zone captures (Phase 11)
    chaos_path = RESULTS_DIR / "chaos-zone" / "chaos-zone-capture.csv"
    if chaos_path.exists():
        data['new'] = pd.read_csv(chaos_path)
        print(f"Loaded {len(data['new'])} new chaos zone experiments")

    # Historical cc-rtt-sweep data for comparison
    sweep_path = RESULTS_DIR / "cc-rtt-sweep" / "cc-rtt-sweep.csv"
    if sweep_path.exists():
        data['historical'] = pd.read_csv(sweep_path)
        print(f"Loaded {len(data['historical'])} historical experiments")

    return data


def analyze_hypothesis_c1(df):
    """
    H-C1: RTO vs Fast Retransmit Bifurcation

    Hypothesis: Low-throughput runs have >80% RTO-triggered retransmits,
    high-throughput runs have >80% fast retransmits.

    We can approximate this by looking at retransmit_count vs fast_retransmit_count.
    If fast_retransmit_count << retransmit_count, most retransmits are RTO-triggered.
    """
    print("\n" + "="*70)
    print("H-C1: RTO vs Fast Retransmit Bifurcation")
    print("="*70)

    # Calculate RTO ratio (retransmits not from fast retransmit)
    df = df.copy()
    df['rto_retransmits'] = df['retransmit_count'] - df['fast_retransmit_count']
    df['rto_ratio'] = df['rto_retransmits'] / df['retransmit_count'].replace(0, 1)

    # Split into high and low throughput
    median_thru = df['actual_throughput_kbps'].median()
    high_thru = df[df['actual_throughput_kbps'] > median_thru]
    low_thru = df[df['actual_throughput_kbps'] <= median_thru]

    print(f"\nMedian throughput: {median_thru:.0f} KB/s")
    print(f"High throughput runs (>{median_thru:.0f}): {len(high_thru)}")
    print(f"Low throughput runs (≤{median_thru:.0f}): {len(low_thru)}")

    if len(high_thru) > 0 and len(low_thru) > 0:
        high_rto_ratio = high_thru['rto_ratio'].mean()
        low_rto_ratio = low_thru['rto_ratio'].mean()

        print(f"\nHigh throughput RTO ratio: {high_rto_ratio:.1%}")
        print(f"Low throughput RTO ratio: {low_rto_ratio:.1%}")

        if low_rto_ratio > high_rto_ratio + 0.2:
            print("\n✓ SUPPORTED: Low throughput runs have higher RTO ratio")
        else:
            print("\n✗ NOT SUPPORTED: RTO ratios are similar")

    # Detailed breakdown by condition
    print("\nDetailed breakdown:")
    for jitter in sorted(df['net_jitter_ms'].unique()):
        for algo in ['cubic', 'bbr']:
            subset = df[(df['net_jitter_ms'] == jitter) & (df['congestion_algo'] == algo)]
            if len(subset) > 0:
                avg_thru = subset['actual_throughput_kbps'].mean()
                std_thru = subset['actual_throughput_kbps'].std()
                cv = std_thru / avg_thru * 100 if avg_thru > 0 else 0
                avg_retx = subset['retransmit_count'].mean()
                avg_fast = subset['fast_retransmit_count'].mean()
                rto_pct = (avg_retx - avg_fast) / avg_retx * 100 if avg_retx > 0 else 0
                print(f"  ±{jitter}ms {algo.upper():5s}: thru={avg_thru:6.0f}±{std_thru:5.0f} (CV={cv:5.1f}%), retx={avg_retx:5.0f}, fast={avg_fast:4.0f}, RTO%={rto_pct:5.1f}%")

    return df


def analyze_hypothesis_c2(df):
    """
    H-C2: Spurious Timeout from RTT Spikes

    Hypothesis: RTT spikes inflate the RTO estimator, causing timeouts.
    We look for correlation between RTT stddev and throughput collapse.
    """
    print("\n" + "="*70)
    print("H-C2: Spurious Timeout from RTT Spikes")
    print("="*70)

    # Estimate RTO = SRTT + 4*RTTVAR (Linux default)
    df = df.copy()
    df['estimated_rto_ms'] = df['rtt_mean_us']/1000 + 4 * df['rtt_stddev_us']/1000

    # Look for correlation
    corr = df['estimated_rto_ms'].corr(df['actual_throughput_kbps'])
    print(f"\nCorrelation between estimated RTO and throughput: {corr:.3f}")

    if corr < -0.3:
        print("✓ SUPPORTED: Higher RTO correlates with lower throughput")
    else:
        print("✗ NOT SUPPORTED: No strong correlation")

    # Show RTT statistics by condition
    print("\nRTT and RTO statistics:")
    for jitter in sorted(df['net_jitter_ms'].unique()):
        for algo in ['cubic', 'bbr']:
            subset = df[(df['net_jitter_ms'] == jitter) & (df['congestion_algo'] == algo)]
            if len(subset) > 0:
                rtt_mean = subset['rtt_mean_us'].mean() / 1000
                rtt_std = subset['rtt_stddev_us'].mean() / 1000
                est_rto = subset['estimated_rto_ms'].mean()
                thru = subset['actual_throughput_kbps'].mean()
                print(f"  ±{jitter}ms {algo.upper():5s}: RTT={rtt_mean:5.1f}±{rtt_std:5.1f}ms, est_RTO={est_rto:6.1f}ms, thru={thru:6.0f} KB/s")

    return df


def analyze_hypothesis_c3(df):
    """
    H-C3: Dup-ACK Threshold Race Condition

    Hypothesis: At exactly 3 dup-ACKs (fast retransmit threshold),
    packet reordering creates a race condition.
    We look at dup_ack_count correlation with throughput.
    """
    print("\n" + "="*70)
    print("H-C3: Dup-ACK Threshold Race Condition")
    print("="*70)

    # Look for correlation between dup_acks and throughput
    corr = df['dup_ack_count'].corr(df['actual_throughput_kbps'])
    print(f"\nCorrelation between dup_ack_count and throughput: {corr:.3f}")

    # Compare dup_acks in high vs low throughput runs
    median_thru = df['actual_throughput_kbps'].median()
    high_thru = df[df['actual_throughput_kbps'] > median_thru]
    low_thru = df[df['actual_throughput_kbps'] <= median_thru]

    if len(high_thru) > 0 and len(low_thru) > 0:
        high_dups = high_thru['dup_ack_count'].mean()
        low_dups = low_thru['dup_ack_count'].mean()

        print(f"\nHigh throughput avg dup_acks: {high_dups:.0f}")
        print(f"Low throughput avg dup_acks: {low_dups:.0f}")

        if low_dups < high_dups * 0.7:
            print("\n✓ SUPPORTED: Low throughput runs have fewer dup_acks (not triggering fast retransmit)")
        else:
            print("\n✗ NOT SUPPORTED: Dup_ack counts are similar")

    # Breakdown by condition
    print("\nDup-ACK counts by condition:")
    for jitter in sorted(df['net_jitter_ms'].unique()):
        for algo in ['cubic', 'bbr']:
            subset = df[(df['net_jitter_ms'] == jitter) & (df['congestion_algo'] == algo)]
            if len(subset) > 0:
                avg_dups = subset['dup_ack_count'].mean()
                thru = subset['actual_throughput_kbps'].mean()
                ratio = avg_dups / thru * 1000 if thru > 0 else 0
                print(f"  ±{jitter}ms {algo.upper():5s}: dup_acks={avg_dups:6.0f}, thru={thru:6.0f}, dup_acks/KB={ratio:.2f}")


def analyze_hypothesis_c4(df):
    """
    H-C4: CUBIC Convex Growth Instability

    Hypothesis: CUBIC's aggressive growth overshoots, causing oscillation.
    We compare window_oscillations and CV between CUBIC and BBR.
    """
    print("\n" + "="*70)
    print("H-C4: CUBIC Convex Growth Instability")
    print("="*70)

    cubic = df[df['congestion_algo'] == 'cubic']
    bbr = df[df['congestion_algo'] == 'bbr']

    if len(cubic) > 0 and len(bbr) > 0:
        # Compare coefficient of variation
        cubic_cv = cubic['actual_throughput_kbps'].std() / cubic['actual_throughput_kbps'].mean() * 100
        bbr_cv = bbr['actual_throughput_kbps'].std() / bbr['actual_throughput_kbps'].mean() * 100

        print(f"\nThroughput CV - CUBIC: {cubic_cv:.1f}%, BBR: {bbr_cv:.1f}%")

        # Compare window oscillations
        cubic_osc = cubic['window_oscillations'].mean()
        bbr_osc = bbr['window_oscillations'].mean()

        print(f"Avg window oscillations - CUBIC: {cubic_osc:.0f}, BBR: {bbr_osc:.0f}")

        if cubic_cv > bbr_cv * 1.5:
            print("\n✓ SUPPORTED: CUBIC shows higher throughput variance than BBR")
        else:
            print("\n✗ NOT SUPPORTED: Similar variance between algorithms")

    # Breakdown by jitter
    print("\nBy jitter level:")
    for jitter in sorted(df['net_jitter_ms'].unique()):
        cubic_j = df[(df['net_jitter_ms'] == jitter) & (df['congestion_algo'] == 'cubic')]
        bbr_j = df[(df['net_jitter_ms'] == jitter) & (df['congestion_algo'] == 'bbr')]
        if len(cubic_j) > 0 and len(bbr_j) > 0:
            c_mean = cubic_j['actual_throughput_kbps'].mean()
            c_std = cubic_j['actual_throughput_kbps'].std()
            c_cv = c_std / c_mean * 100 if c_mean > 0 else 0
            c_osc = cubic_j['window_oscillations'].mean()

            b_mean = bbr_j['actual_throughput_kbps'].mean()
            b_std = bbr_j['actual_throughput_kbps'].std()
            b_cv = b_std / b_mean * 100 if b_mean > 0 else 0
            b_osc = bbr_j['window_oscillations'].mean()

            print(f"  ±{jitter}ms: CUBIC {c_mean:.0f}±{c_std:.0f} (CV={c_cv:.1f}%, osc={c_osc:.0f}) vs BBR {b_mean:.0f}±{b_std:.0f} (CV={b_cv:.1f}%, osc={b_osc:.0f})")


def analyze_hypothesis_c5(df):
    """
    H-C5: Netem Burst Losses (Artifact Check)

    Hypothesis: Netem's jitter implementation creates packet bunching
    that causes correlated losses. This would be a simulation artifact.

    We look at out_of_order_count as a proxy for reordering/bunching.
    """
    print("\n" + "="*70)
    print("H-C5: Netem Burst Losses (Artifact Check)")
    print("="*70)

    # Look at out-of-order counts
    ooo_corr = df['out_of_order_count'].corr(df['actual_throughput_kbps'])
    print(f"\nCorrelation between out_of_order_count and throughput: {ooo_corr:.3f}")

    # Check if out-of-order events increase with jitter
    print("\nOut-of-order packets by jitter level:")
    for jitter in sorted(df['net_jitter_ms'].unique()):
        subset = df[df['net_jitter_ms'] == jitter]
        if len(subset) > 0:
            avg_ooo = subset['out_of_order_count'].mean()
            avg_retx = subset['retransmit_count'].mean()
            print(f"  ±{jitter}ms: out_of_order={avg_ooo:.1f}, retransmits={avg_retx:.0f}")

    if df['out_of_order_count'].sum() > 10:
        print("\n⚠ POSSIBLE ARTIFACT: Significant out-of-order packets detected")
        print("  This may indicate netem is causing packet reordering")
    else:
        print("\n✓ No significant artifact detected")


def generate_summary_plot(df, output_dir):
    """Generate summary visualization of chaos zone analysis."""
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle('Chaos Zone Analysis - Phase 11', fontsize=14, fontweight='bold')

    # Plot 1: Throughput by jitter and algorithm
    ax = axes[0, 0]
    for algo in ['cubic', 'bbr']:
        subset = df[df['congestion_algo'] == algo]
        grouped = subset.groupby('net_jitter_ms')['actual_throughput_kbps'].agg(['mean', 'std'])
        ax.errorbar(grouped.index, grouped['mean'], yerr=grouped['std'],
                   label=algo.upper(), marker='o', capsize=5)
    ax.set_xlabel('Jitter (ms)')
    ax.set_ylabel('Throughput (KB/s)')
    ax.set_title('Throughput vs Jitter')
    ax.legend()
    ax.grid(True, alpha=0.3)

    # Plot 2: Retransmit composition
    ax = axes[0, 1]
    for algo in ['cubic', 'bbr']:
        subset = df[df['congestion_algo'] == algo]
        grouped = subset.groupby('net_jitter_ms').agg({
            'retransmit_count': 'mean',
            'fast_retransmit_count': 'mean'
        })
        rto = grouped['retransmit_count'] - grouped['fast_retransmit_count']
        x = grouped.index + (0.15 if algo == 'bbr' else -0.15)
        ax.bar(x, grouped['fast_retransmit_count'], width=0.3, label=f'{algo.upper()} Fast Retx')
        ax.bar(x, rto, width=0.3, bottom=grouped['fast_retransmit_count'],
              label=f'{algo.upper()} RTO Retx', alpha=0.7)
    ax.set_xlabel('Jitter (ms)')
    ax.set_ylabel('Retransmit Count')
    ax.set_title('Retransmit Composition (Fast vs RTO)')
    ax.legend(loc='upper left', fontsize=8)
    ax.grid(True, alpha=0.3)

    # Plot 3: RTT statistics
    ax = axes[1, 0]
    for algo in ['cubic', 'bbr']:
        subset = df[df['congestion_algo'] == algo]
        grouped = subset.groupby('net_jitter_ms')['rtt_stddev_us'].mean() / 1000
        ax.plot(grouped.index, grouped.values, marker='o', label=f'{algo.upper()} RTT stddev')
    ax.set_xlabel('Jitter (ms)')
    ax.set_ylabel('RTT Stddev (ms)')
    ax.set_title('RTT Variability')
    ax.legend()
    ax.grid(True, alpha=0.3)

    # Plot 4: Window oscillations
    ax = axes[1, 1]
    for algo in ['cubic', 'bbr']:
        subset = df[df['congestion_algo'] == algo]
        grouped = subset.groupby('net_jitter_ms')['window_oscillations'].mean()
        ax.plot(grouped.index, grouped.values, marker='o', label=algo.upper())
    ax.set_xlabel('Jitter (ms)')
    ax.set_ylabel('Window Oscillations')
    ax.set_title('TCP Window Oscillations')
    ax.legend()
    ax.grid(True, alpha=0.3)

    plt.tight_layout()

    output_path = output_dir / "chaos_zone_analysis.png"
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"\nSaved plot to: {output_path}")
    plt.close()


def main():
    print("="*70)
    print("CHAOS ZONE ANALYSIS - PHASE 11")
    print("="*70)

    # Load data
    data = load_chaos_zone_data()

    if 'new' not in data:
        print("ERROR: No chaos zone data found!")
        print("Run: sudo ./scripts/run-sweep.sh --preset chaos-zone-capture --cluster chaos-zone")
        sys.exit(1)

    df = data['new']

    # Run hypothesis analyses
    df = analyze_hypothesis_c1(df)
    df = analyze_hypothesis_c2(df)
    analyze_hypothesis_c3(df)
    analyze_hypothesis_c4(df)
    analyze_hypothesis_c5(df)

    # Generate summary plot
    output_dir = RESULTS_DIR / "chaos-zone" / "plots"
    output_dir.mkdir(parents=True, exist_ok=True)
    generate_summary_plot(df, output_dir)

    # Print summary
    print("\n" + "="*70)
    print("SUMMARY")
    print("="*70)
    print("""
Based on the analysis:

H-C1 (RTO Bifurcation): Check RTO% column - if low throughput runs have
      higher RTO%, this supports the hypothesis that they're stuck in
      timeout-based recovery rather than fast retransmit.

H-C2 (RTT Spikes): Check correlation between estimated RTO and throughput.
      Negative correlation supports the hypothesis.

H-C3 (Dup-ACK Race): If low throughput runs have fewer dup_acks, they may
      not be reaching the 3-dup-ACK threshold for fast retransmit.

H-C4 (CUBIC Instability): Compare CV% between CUBIC and BBR. If CUBIC has
      significantly higher CV, its congestion control is less stable.

H-C5 (Netem Artifact): If out_of_order counts are high, netem may be
      creating unrealistic packet reordering.
""")


if __name__ == '__main__':
    main()
