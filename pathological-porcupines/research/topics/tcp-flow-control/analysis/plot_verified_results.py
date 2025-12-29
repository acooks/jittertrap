#!/usr/bin/env python3
"""
Generate plots from verified experiment results (2025-12-29).

Usage:
    python3 plot_verified_results.py

Output:
    plots/verified-2025-12-29/*.png
    investigations/*/plots/*.png
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from pathlib import Path

# Paths
BASE_DIR = Path(__file__).parent.parent
RESULTS_DIR = BASE_DIR / "results" / "verified-2025-12-29"
PLOTS_DIR = BASE_DIR / "plots" / "verified-2025-12-29"
INVESTIGATIONS_DIR = BASE_DIR / "investigations"

# Ensure plot directories exist
PLOTS_DIR.mkdir(parents=True, exist_ok=True)

# Style settings
plt.style.use('seaborn-v0_8-whitegrid')
COLORS = {'cubic': '#1f77b4', 'bbr': '#ff7f0e'}
FIGSIZE = (10, 6)


def load_data():
    """Load all verified result files."""
    data = {}
    for csv_file in RESULTS_DIR.glob("*.csv"):
        name = csv_file.stem
        data[name] = pd.read_csv(csv_file)
        print(f"Loaded {name}: {len(data[name])} rows")
    return data


def plot_baseline_by_rtt(df, output_path):
    """Plot baseline throughput by RTT."""
    fig, ax = plt.subplots(figsize=FIGSIZE)

    baseline = df[(df['net_jitter_ms'] == 0) & (df['net_loss_pct'] == 0)]

    for cc in ['cubic', 'bbr']:
        cc_data = baseline[baseline['congestion_algo'] == cc]
        grouped = cc_data.groupby('net_delay_ms')['actual_throughput_kbps'].agg(['mean', 'std'])
        rtts = grouped.index * 2  # Convert one-way delay to RTT

        ax.errorbar(rtts, grouped['mean'], yerr=grouped['std'],
                   label=cc.upper(), color=COLORS[cc],
                   marker='o', capsize=5, linewidth=2, markersize=8)

    ax.set_xlabel('RTT (ms)', fontsize=12)
    ax.set_ylabel('Throughput (KB/s)', fontsize=12)
    ax.set_title('Baseline Throughput by RTT\n(0% jitter, 0% loss)', fontsize=14)
    ax.legend(fontsize=11)
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    plt.close()
    print(f"Saved: {output_path}")


def plot_loss_tolerance(df, output_path):
    """Plot throughput vs packet loss."""
    fig, ax = plt.subplots(figsize=FIGSIZE)

    # Filter for 50ms RTT (25ms one-way)
    data_50ms = df[(df['net_delay_ms'] == 25) & (df['net_jitter_ms'] == 0)]

    for cc in ['cubic', 'bbr']:
        cc_data = data_50ms[data_50ms['congestion_algo'] == cc]
        grouped = cc_data.groupby('net_loss_pct')['actual_throughput_kbps'].agg(['mean', 'std'])

        ax.errorbar(grouped.index, grouped['mean'], yerr=grouped['std'],
                   label=cc.upper(), color=COLORS[cc],
                   marker='o', capsize=5, linewidth=2, markersize=8)

    ax.set_xlabel('Packet Loss (%)', fontsize=12)
    ax.set_xlim(-0.2, None)
    ax.set_ylabel('Throughput (KB/s)', fontsize=12)
    ax.set_title('Loss Tolerance: CUBIC vs BBR\n(50ms RTT, 0% jitter)', fontsize=14)
    ax.legend(fontsize=11)
    ax.grid(True, alpha=0.3)
    ax.set_yscale('log')

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    plt.close()
    print(f"Saved: {output_path}")


def plot_jitter_cliff(df, output_path):
    """Plot jitter cliff for multiple RTTs."""
    fig, axes = plt.subplots(1, 3, figsize=(15, 5), sharey=True)

    rtts = [(12, 24), (25, 50), (50, 100)]  # (one-way delay, RTT)

    for ax, (delay, rtt) in zip(axes, rtts):
        data = df[(df['net_delay_ms'] == delay) & (df['net_loss_pct'] == 0)]

        for cc in ['cubic', 'bbr']:
            cc_data = data[data['congestion_algo'] == cc]
            grouped = cc_data.groupby('net_jitter_ms')['actual_throughput_kbps'].agg(['mean', 'std'])

            ax.errorbar(grouped.index, grouped['mean'], yerr=grouped['std'],
                       label=cc.upper(), color=COLORS[cc],
                       marker='o', capsize=3, linewidth=2, markersize=6)

        ax.set_xlabel('Jitter (±ms)', fontsize=11)
        ax.set_title(f'RTT = {rtt}ms', fontsize=12)
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3)

        # Add cliff annotation
        baseline_cubic = data[(data['net_jitter_ms'] == 0) & (data['congestion_algo'] == 'cubic')]['actual_throughput_kbps'].mean()
        for jitter in sorted(data['net_jitter_ms'].unique()):
            jitter_data = data[(data['net_jitter_ms'] == jitter) & (data['congestion_algo'] == 'cubic')]['actual_throughput_kbps'].mean()
            if jitter_data < baseline_cubic * 0.5:
                ax.axvline(x=jitter, color='red', linestyle='--', alpha=0.5, label=f'Cliff @ {jitter}ms')
                break

    axes[0].set_ylabel('Throughput (KB/s)', fontsize=11)
    fig.suptitle('Jitter Cliff: Throughput Collapse by RTT\n(0% loss)', fontsize=14)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    plt.close()
    print(f"Saved: {output_path}")


def plot_jitter_cliff_normalized(df, output_path):
    """Plot jitter cliff with jitter as % of RTT."""
    fig, ax = plt.subplots(figsize=FIGSIZE)

    rtts = [(12, 24), (25, 50), (50, 100)]
    markers = ['o', 's', '^']

    for (delay, rtt), marker in zip(rtts, markers):
        data = df[(df['net_delay_ms'] == delay) & (df['net_loss_pct'] == 0)]

        for cc in ['cubic', 'bbr']:
            cc_data = data[data['congestion_algo'] == cc]
            grouped = cc_data.groupby('net_jitter_ms')['actual_throughput_kbps'].mean()

            # Normalize jitter as % of RTT
            jitter_pct = (grouped.index / rtt) * 100
            # Normalize throughput to baseline
            baseline = grouped.iloc[0] if grouped.iloc[0] > 0 else 1
            throughput_pct = (grouped / baseline) * 100

            linestyle = '-' if cc == 'cubic' else '--'
            ax.plot(jitter_pct, throughput_pct,
                   label=f'{cc.upper()} @ {rtt}ms RTT',
                   color=COLORS[cc], linestyle=linestyle,
                   marker=marker, markersize=6, linewidth=2)

    ax.axhline(y=50, color='red', linestyle=':', alpha=0.7, label='50% threshold')
    ax.axvline(x=20, color='gray', linestyle=':', alpha=0.7)
    ax.text(21, 80, 'Cliff zone\n(~20% of RTT)', fontsize=10, alpha=0.7)

    ax.set_xlabel('Jitter as % of RTT', fontsize=12)
    ax.set_ylabel('Throughput (% of baseline)', fontsize=12)
    ax.set_title('Jitter Cliff is RTT-Relative\n(Throughput collapses at ~20% jitter/RTT)', fontsize=14)
    ax.legend(fontsize=9, loc='upper right')
    ax.grid(True, alpha=0.3)
    ax.set_xlim(0, 110)
    ax.set_ylim(0, 120)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    plt.close()
    print(f"Saved: {output_path}")


def plot_chaos_zone(df, output_path):
    """Plot chaos zone coefficient of variation."""
    fig, axes = plt.subplots(1, 2, figsize=(14, 5))

    # Filter for 50ms RTT (25ms one-way)
    data = df[(df['net_delay_ms'] == 25) & (df['net_loss_pct'] == 0)]

    # Left: Throughput with variance
    ax = axes[0]
    for cc in ['cubic', 'bbr']:
        cc_data = data[data['congestion_algo'] == cc]
        grouped = cc_data.groupby('net_jitter_ms')['actual_throughput_kbps'].agg(['mean', 'std', 'min', 'max'])

        ax.fill_between(grouped.index, grouped['min'], grouped['max'],
                       color=COLORS[cc], alpha=0.2)
        ax.plot(grouped.index, grouped['mean'],
               label=cc.upper(), color=COLORS[cc],
               marker='o', linewidth=2, markersize=6)

    ax.set_xlabel('Jitter (±ms)', fontsize=11)
    ax.set_ylabel('Throughput (KB/s)', fontsize=11)
    ax.set_title('Chaos Zone: High Variance Region\n(shaded = min/max range)', fontsize=12)
    ax.legend(fontsize=10)
    ax.grid(True, alpha=0.3)

    # Right: Coefficient of variation
    ax = axes[1]
    for cc in ['cubic', 'bbr']:
        cc_data = data[data['congestion_algo'] == cc]
        grouped = cc_data.groupby('net_jitter_ms')['actual_throughput_kbps'].agg(['mean', 'std'])
        cv = (grouped['std'] / grouped['mean']) * 100

        ax.bar([j + (0.2 if cc == 'bbr' else -0.2) for j in grouped.index],
               cv, width=0.4, label=cc.upper(), color=COLORS[cc], alpha=0.8)

    ax.axhline(y=30, color='red', linestyle='--', alpha=0.7, label='High variance threshold')
    ax.set_xlabel('Jitter (±ms)', fontsize=11)
    ax.set_ylabel('Coefficient of Variation (%)', fontsize=11)
    ax.set_title('CUBIC Shows Bifurcation in Chaos Zone\n(CV > 30% = unstable)', fontsize=12)
    ax.legend(fontsize=10)
    ax.grid(True, alpha=0.3, axis='y')

    fig.suptitle('Chaos Zone Analysis (50ms RTT, 0% loss, 20 iterations each)', fontsize=14, y=1.02)
    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    plt.close()
    print(f"Saved: {output_path}")


def plot_starlink_profiles(df_baseline, df_handover, df_degraded, output_path):
    """Plot Starlink profile comparison."""
    fig, axes = plt.subplots(1, 3, figsize=(15, 5))

    profiles = [
        (df_baseline, 'Baseline\n(27ms RTT, 7ms jitter)', axes[0]),
        (df_handover, 'Handover\n(60ms RTT, 40ms jitter)', axes[1]),
        (df_degraded, 'Degraded\n(80ms RTT, 15ms jitter)', axes[2]),
    ]

    for df, title, ax in profiles:
        for cc in ['cubic', 'bbr']:
            cc_data = df[df['congestion_algo'] == cc]
            grouped = cc_data.groupby('net_loss_pct')['actual_throughput_kbps'].agg(['mean', 'std'])

            ax.errorbar(grouped.index, grouped['mean'], yerr=grouped['std'],
                       label=cc.upper(), color=COLORS[cc],
                       marker='o', capsize=3, linewidth=2, markersize=6)

        ax.set_xlabel('Packet Loss (%)', fontsize=11)
        ax.set_xlim(-0.05, None)
        ax.set_title(title, fontsize=12)
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3)

    axes[0].set_ylabel('Throughput (KB/s)', fontsize=11)
    fig.suptitle('Starlink Network Profiles: BBR vs CUBIC\n(Based on cited Starlink characteristics)', fontsize=14)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    plt.close()
    print(f"Saved: {output_path}")


def plot_bbr_advantage_heatmap(df_loss, df_jitter, output_path):
    """Plot BBR advantage ratio heatmap."""
    fig, axes = plt.subplots(1, 2, figsize=(14, 5))

    # Loss tolerance heatmap
    ax = axes[0]
    data = df_loss[(df_loss['net_delay_ms'] == 25) & (df_loss['net_jitter_ms'] == 0)]

    losses = sorted(data['net_loss_pct'].unique())
    ratios = []
    for loss in losses:
        cubic = data[(data['net_loss_pct'] == loss) & (data['congestion_algo'] == 'cubic')]['actual_throughput_kbps'].mean()
        bbr = data[(data['net_loss_pct'] == loss) & (data['congestion_algo'] == 'bbr')]['actual_throughput_kbps'].mean()
        ratios.append(bbr / cubic if cubic > 0 else 0)

    bars = ax.bar([f'{l}%' for l in losses], ratios, color='#ff7f0e', alpha=0.8)
    ax.axhline(y=1, color='gray', linestyle='--', alpha=0.7)
    ax.set_xlabel('Packet Loss', fontsize=11)
    ax.set_ylabel('BBR / CUBIC Ratio', fontsize=11)
    ax.set_title('BBR Advantage Under Loss\n(50ms RTT, 0% jitter)', fontsize=12)
    ax.grid(True, alpha=0.3, axis='y')

    # Add ratio labels
    for bar, ratio in zip(bars, ratios):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.2,
               f'{ratio:.1f}x', ha='center', fontsize=9)

    # Jitter tolerance heatmap
    ax = axes[1]
    data = df_jitter[(df_jitter['net_delay_ms'] == 25) & (df_jitter['net_loss_pct'] == 0)]

    jitters = sorted(data['net_jitter_ms'].unique())
    ratios = []
    for jitter in jitters:
        cubic = data[(data['net_jitter_ms'] == jitter) & (data['congestion_algo'] == 'cubic')]['actual_throughput_kbps'].mean()
        bbr = data[(data['net_jitter_ms'] == jitter) & (data['congestion_algo'] == 'bbr')]['actual_throughput_kbps'].mean()
        ratios.append(bbr / cubic if cubic > 0 else 0)

    bars = ax.bar([f'±{j}ms' for j in jitters], ratios, color='#ff7f0e', alpha=0.8)
    ax.axhline(y=1, color='gray', linestyle='--', alpha=0.7)
    ax.set_xlabel('Jitter', fontsize=11)
    ax.set_ylabel('BBR / CUBIC Ratio', fontsize=11)
    ax.set_title('BBR Advantage Under Jitter\n(50ms RTT, 0% loss)', fontsize=12)
    ax.grid(True, alpha=0.3, axis='y')

    for bar, ratio in zip(bars, ratios):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.1,
               f'{ratio:.1f}x', ha='center', fontsize=9)

    fig.suptitle('BBR Advantage Ratio (values > 1 = BBR wins)', fontsize=14, y=1.02)
    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    plt.close()
    print(f"Saved: {output_path}")


def main():
    print("Loading verified results...")
    data = load_data()

    print("\nGenerating plots...")

    # Main plots in plots/verified-2025-12-29/
    plot_baseline_by_rtt(data['baseline-verification'],
                         PLOTS_DIR / '01_baseline_by_rtt.png')

    plot_loss_tolerance(data['loss-tolerance-clean'],
                       PLOTS_DIR / '02_loss_tolerance.png')

    plot_jitter_cliff(data['jitter-cliff-verification'],
                     PLOTS_DIR / '03_jitter_cliff_by_rtt.png')

    plot_jitter_cliff_normalized(data['jitter-cliff-verification'],
                                PLOTS_DIR / '04_jitter_cliff_normalized.png')

    plot_chaos_zone(data['chaos-zone-statistical'],
                   PLOTS_DIR / '05_chaos_zone.png')

    plot_starlink_profiles(data['starlink-canonical-baseline'],
                          data['starlink-canonical-handover'],
                          data['starlink-canonical-degraded'],
                          PLOTS_DIR / '06_starlink_profiles.png')

    plot_bbr_advantage_heatmap(data['loss-tolerance-clean'],
                              data['jitter-cliff-verification'],
                              PLOTS_DIR / '07_bbr_advantage.png')

    # Copy relevant plots to investigation directories
    investigation_plots = {
        'jitter-cliff': ['03_jitter_cliff_by_rtt.png', '04_jitter_cliff_normalized.png', '05_chaos_zone.png'],
        'loss-tolerance': ['02_loss_tolerance.png', '07_bbr_advantage.png'],
        'congestion-control': ['07_bbr_advantage.png'],
        'starlink-profiles': ['06_starlink_profiles.png'],
    }

    for investigation, plots in investigation_plots.items():
        inv_plots_dir = INVESTIGATIONS_DIR / investigation / 'plots'
        inv_plots_dir.mkdir(parents=True, exist_ok=True)
        for plot in plots:
            src = PLOTS_DIR / plot
            dst = inv_plots_dir / plot
            if src.exists():
                import shutil
                shutil.copy(src, dst)
                print(f"Copied: {dst}")

    print("\nDone! Generated plots in:")
    print(f"  - {PLOTS_DIR}")
    for investigation in investigation_plots:
        print(f"  - {INVESTIGATIONS_DIR / investigation / 'plots'}")


if __name__ == '__main__':
    main()
