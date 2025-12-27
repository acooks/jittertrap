#!/usr/bin/env python3
"""
Congestion Control Algorithm Comparison Analysis

Compares CUBIC, BBR, and Reno under varying jitter conditions.
"""

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import pandas as pd
import numpy as np
from pathlib import Path

# Configuration
SCRIPT_DIR = Path(__file__).parent
EXPERIMENT_DIR = SCRIPT_DIR.parent
DATA_DIR = EXPERIMENT_DIR / 'results' / 'cc-comparison'
OUTPUT_DIR = DATA_DIR / 'plots'
OUTPUT_DIR.mkdir(exist_ok=True)

plt.style.use('seaborn-v0_8-whitegrid')
plt.rcParams['figure.figsize'] = (14, 8)
plt.rcParams['font.size'] = 11

# Video streaming thresholds (KB/s)
VIDEO_THRESHOLDS = {
    'SD 480p': 500,    # ~4 Mbps
    'HD 720p': 625,    # ~5 Mbps
    'HD 1080p': 1000,  # ~8 Mbps
    '4K': 3125,        # ~25 Mbps
}


def load_jitter_data():
    """Load jitter comparison data for all algorithms."""
    data = {}
    for algo in ['cubic', 'bbr', 'reno']:
        csv_path = DATA_DIR / f'cc-jitter-{algo}.csv'
        if csv_path.exists():
            df = pd.read_csv(csv_path)
            df['algorithm'] = algo.upper()
            data[algo] = df
            print(f"Loaded cc-jitter-{algo}: {len(df)} experiments")
    return data


def plot_linear_comparison(data):
    """
    Linear scale comparison - good for seeing the full picture.
    """
    fig, ax = plt.subplots(figsize=(12, 7))

    colors = {'cubic': '#d62728', 'bbr': '#2ca02c', 'reno': '#1f77b4'}
    markers = {'cubic': 'o', 'bbr': 's', 'reno': '^'}

    for algo, df in data.items():
        df_sorted = df.sort_values('net_jitter_ms')
        ax.plot(df_sorted['net_jitter_ms'], df_sorted['actual_throughput_kbps'] / 1024,
                marker=markers[algo], color=colors[algo], linewidth=2.5, markersize=8,
                label=f'{algo.upper()}', markeredgecolor='white', markeredgewidth=1)

    # Add video quality thresholds
    for name, threshold in VIDEO_THRESHOLDS.items():
        ax.axhline(y=threshold/1024, color='orange', linestyle=':', alpha=0.4)
        ax.text(33, threshold/1024 + 0.05, name, fontsize=9, color='darkorange', va='bottom')

    ax.set_xlabel('INPUT: Network Jitter (±ms)', fontsize=12, fontweight='bold')
    ax.set_ylabel('OUTPUT: Throughput (MB/s)', fontsize=12, fontweight='bold')
    ax.set_title('Congestion Control vs Jitter: Linear Scale\n'
                 '(50ms base RTT, 256KB buffer, 10 Mbps target)',
                 fontsize=14, fontweight='bold')
    ax.legend(title='Algorithm', fontsize=11, title_fontsize=11)
    ax.grid(True, alpha=0.3)
    ax.set_xlim(-1, 34)
    ax.set_ylim(0, 6.5)

    # Annotate the cliff region
    ax.axvspan(6, 10, alpha=0.15, color='red', label='Cliff region')
    ax.annotate('CUBIC/Reno\ncollapse here', xy=(8, 1), xytext=(15, 2.5),
                fontsize=10, ha='center',
                arrowprops=dict(arrowstyle='->', color='red', lw=1.5),
                color='red')

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'jitter_linear.png', dpi=150, bbox_inches='tight')
    print(f"Saved: {OUTPUT_DIR / 'jitter_linear.png'}")
    plt.close()


def plot_log_comparison(data):
    """
    Log scale comparison - reveals behavior at low throughput.
    """
    fig, ax = plt.subplots(figsize=(12, 7))

    colors = {'cubic': '#d62728', 'bbr': '#2ca02c', 'reno': '#1f77b4'}
    markers = {'cubic': 'o', 'bbr': 's', 'reno': '^'}

    for algo, df in data.items():
        df_sorted = df.sort_values('net_jitter_ms')
        ax.plot(df_sorted['net_jitter_ms'], df_sorted['actual_throughput_kbps'],
                marker=markers[algo], color=colors[algo], linewidth=2.5, markersize=8,
                label=f'{algo.upper()}', markeredgecolor='white', markeredgewidth=1)

    # Add video quality thresholds
    for name, threshold in VIDEO_THRESHOLDS.items():
        ax.axhline(y=threshold, color='orange', linestyle=':', alpha=0.6)
        ax.text(33, threshold * 1.1, name, fontsize=9, color='darkorange', va='bottom')

    ax.set_xlabel('INPUT: Network Jitter (±ms)', fontsize=12, fontweight='bold')
    ax.set_ylabel('OUTPUT: Throughput (KB/s) - LOG SCALE', fontsize=12, fontweight='bold')
    ax.set_title('Congestion Control vs Jitter: Log Scale\n'
                 'Reveals BBR advantage at high jitter',
                 fontsize=14, fontweight='bold')
    ax.legend(title='Algorithm', fontsize=11, title_fontsize=11, loc='lower left')
    ax.set_yscale('log')
    ax.grid(True, alpha=0.3, which='both')
    ax.set_xlim(-1, 34)
    ax.set_ylim(50, 10000)

    # Annotate BBR advantage
    ax.annotate('BBR: 4x better\nat high jitter', xy=(20, 373), xytext=(25, 1000),
                fontsize=10, ha='center',
                arrowprops=dict(arrowstyle='->', color='green', lw=1.5),
                color='green')

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'jitter_logscale.png', dpi=150, bbox_inches='tight')
    print(f"Saved: {OUTPUT_DIR / 'jitter_logscale.png'}")
    plt.close()


def plot_retention_comparison(data):
    """
    Throughput retention as % of baseline - normalized comparison.
    """
    fig, ax = plt.subplots(figsize=(12, 7))

    colors = {'cubic': '#d62728', 'bbr': '#2ca02c', 'reno': '#1f77b4'}
    markers = {'cubic': 'o', 'bbr': 's', 'reno': '^'}

    for algo, df in data.items():
        df_sorted = df.sort_values('net_jitter_ms')
        baseline = df_sorted[df_sorted['net_jitter_ms'] == 0]['actual_throughput_kbps'].values[0]
        retention = (df_sorted['actual_throughput_kbps'] / baseline) * 100
        ax.plot(df_sorted['net_jitter_ms'], retention,
                marker=markers[algo], color=colors[algo], linewidth=2.5, markersize=8,
                label=f'{algo.upper()}', markeredgecolor='white', markeredgewidth=1)

    # Reference lines
    ax.axhline(y=50, color='gray', linestyle='--', alpha=0.5, linewidth=1.5)
    ax.axhline(y=10, color='red', linestyle='--', alpha=0.5, linewidth=1.5)
    ax.text(33, 52, '50% - Degraded', fontsize=9, color='gray', va='bottom')
    ax.text(33, 12, '10% - Unusable', fontsize=9, color='red', va='bottom')

    ax.set_xlabel('INPUT: Network Jitter (±ms)', fontsize=12, fontweight='bold')
    ax.set_ylabel('OUTPUT: Throughput Retention (%)', fontsize=12, fontweight='bold')
    ax.set_title('Throughput Retention vs Jitter\n'
                 '(% of throughput at 0ms jitter)',
                 fontsize=14, fontweight='bold')
    ax.legend(title='Algorithm', fontsize=11, title_fontsize=11)
    ax.grid(True, alpha=0.3)
    ax.set_xlim(-1, 34)
    ax.set_ylim(0, 105)

    # Shade regions
    ax.axhspan(50, 100, alpha=0.1, color='green')
    ax.axhspan(10, 50, alpha=0.1, color='yellow')
    ax.axhspan(0, 10, alpha=0.1, color='red')

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'jitter_retention.png', dpi=150, bbox_inches='tight')
    print(f"Saved: {OUTPUT_DIR / 'jitter_retention.png'}")
    plt.close()


def plot_cliff_detail(data):
    """
    Zoomed view of the 0-16ms region to show exactly where the cliff is.
    """
    fig, ax = plt.subplots(figsize=(12, 7))

    colors = {'cubic': '#d62728', 'bbr': '#2ca02c', 'reno': '#1f77b4'}
    markers = {'cubic': 'o', 'bbr': 's', 'reno': '^'}

    for algo, df in data.items():
        df_sorted = df.sort_values('net_jitter_ms')
        # Filter to 0-16ms range
        df_zoom = df_sorted[df_sorted['net_jitter_ms'] <= 16]
        ax.plot(df_zoom['net_jitter_ms'], df_zoom['actual_throughput_kbps'] / 1024,
                marker=markers[algo], color=colors[algo], linewidth=2.5, markersize=10,
                label=f'{algo.upper()}', markeredgecolor='white', markeredgewidth=1.5)

    ax.set_xlabel('INPUT: Network Jitter (±ms)', fontsize=12, fontweight='bold')
    ax.set_ylabel('OUTPUT: Throughput (MB/s)', fontsize=12, fontweight='bold')
    ax.set_title('Jitter Cliff Detail: 0-16ms Range\n'
                 'Where exactly does throughput collapse?',
                 fontsize=14, fontweight='bold')
    ax.legend(title='Algorithm', fontsize=11, title_fontsize=11)
    ax.grid(True, alpha=0.3)
    ax.set_xlim(-0.5, 16.5)
    ax.set_ylim(0, 6.5)

    # Mark the cliff points
    ax.axvline(x=6, color='red', linestyle='--', alpha=0.7, linewidth=1.5)
    ax.axvline(x=8, color='red', linestyle='--', alpha=0.7, linewidth=1.5)
    ax.axvspan(6, 8, alpha=0.2, color='red')
    ax.text(7, 6, 'CUBIC cliff\n6-8ms', fontsize=11, ha='center', color='red', fontweight='bold')

    # Annotate Reno anomaly at 6ms
    ax.annotate('Reno survives\n6ms better', xy=(6, 4.7), xytext=(10, 5.5),
                fontsize=10, ha='center',
                arrowprops=dict(arrowstyle='->', color='blue', lw=1.5),
                color='blue')

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'jitter_cliff_detail.png', dpi=150, bbox_inches='tight')
    print(f"Saved: {OUTPUT_DIR / 'jitter_cliff_detail.png'}")
    plt.close()


def plot_bbr_advantage(data):
    """
    Bar chart showing BBR's advantage at different jitter levels.
    """
    fig, ax = plt.subplots(figsize=(12, 7))

    jitters = [0, 4, 8, 12, 16, 20, 24, 28, 32]

    cubic_tp = []
    bbr_tp = []
    bbr_advantage = []

    for j in jitters:
        c = data['cubic'][data['cubic']['net_jitter_ms'] == j]['actual_throughput_kbps'].values
        b = data['bbr'][data['bbr']['net_jitter_ms'] == j]['actual_throughput_kbps'].values
        if len(c) > 0 and len(b) > 0:
            cubic_tp.append(c[0])
            bbr_tp.append(b[0])
            if c[0] > 0:
                bbr_advantage.append((b[0] / c[0]))
            else:
                bbr_advantage.append(1)

    x = np.arange(len(jitters))
    width = 0.35

    bars1 = ax.bar(x - width/2, [t/1024 for t in cubic_tp], width, label='CUBIC', color='#d62728', alpha=0.8)
    bars2 = ax.bar(x + width/2, [t/1024 for t in bbr_tp], width, label='BBR', color='#2ca02c', alpha=0.8)

    ax.set_xlabel('Network Jitter (±ms)', fontsize=12, fontweight='bold')
    ax.set_ylabel('Throughput (MB/s)', fontsize=12, fontweight='bold')
    ax.set_title('BBR vs CUBIC: Throughput Comparison\n'
                 'BBR provides significant advantage under jitter',
                 fontsize=14, fontweight='bold')
    ax.set_xticks(x)
    ax.set_xticklabels([f'±{j}' for j in jitters])
    ax.legend(fontsize=11)
    ax.grid(True, alpha=0.3, axis='y')

    # Add advantage labels
    for i, (c, b, adv) in enumerate(zip(cubic_tp, bbr_tp, bbr_advantage)):
        if adv > 1.5:
            ax.annotate(f'{adv:.1f}x', xy=(x[i] + width/2, b/1024),
                       xytext=(0, 5), textcoords='offset points',
                       ha='center', fontsize=9, fontweight='bold', color='green')

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'bbr_advantage.png', dpi=150, bbox_inches='tight')
    print(f"Saved: {OUTPUT_DIR / 'bbr_advantage.png'}")
    plt.close()


def create_summary_table(data):
    """Print and save a summary table."""
    print("\n" + "="*80)
    print("SUMMARY TABLE: Throughput (KB/s) by Algorithm and Jitter")
    print("="*80)

    jitters = sorted(data['cubic']['net_jitter_ms'].unique())

    print(f"\n{'Jitter':<8} {'CUBIC':<12} {'BBR':<12} {'Reno':<12} {'BBR/CUBIC':<12} {'Best':<8}")
    print("-"*80)

    rows = []
    for j in jitters:
        row = {'jitter': j}
        for algo in ['cubic', 'bbr', 'reno']:
            tp = data[algo][data[algo]['net_jitter_ms'] == j]['actual_throughput_kbps'].values
            row[algo] = tp[0] if len(tp) > 0 else 0

        row['bbr_advantage'] = row['bbr'] / row['cubic'] if row['cubic'] > 0 else 0
        row['best'] = max(['cubic', 'bbr', 'reno'], key=lambda a: row[a]).upper()
        rows.append(row)

        print(f"±{j:<7} {row['cubic']:<12.0f} {row['bbr']:<12.0f} {row['reno']:<12.0f} "
              f"{row['bbr_advantage']:<12.1f}x {row['best']:<8}")

    # Key insights
    print("\n" + "="*80)
    print("KEY INSIGHTS")
    print("="*80)

    # Find cliff point for CUBIC
    for i in range(1, len(rows)):
        if rows[i]['cubic'] < rows[i-1]['cubic'] * 0.5:
            print(f"\n1. CUBIC cliff point: between ±{rows[i-1]['jitter']}ms and ±{rows[i]['jitter']}ms")
            print(f"   Throughput drops from {rows[i-1]['cubic']:.0f} to {rows[i]['cubic']:.0f} KB/s")
            break

    # BBR advantage at high jitter
    high_jitter_rows = [r for r in rows if r['jitter'] >= 16]
    if high_jitter_rows:
        avg_advantage = np.mean([r['bbr_advantage'] for r in high_jitter_rows])
        print(f"\n2. BBR advantage at high jitter (≥16ms): {avg_advantage:.1f}x average")

    # Usable jitter range for each algorithm
    print("\n3. Maximum jitter for >1 MB/s throughput:")
    for algo in ['cubic', 'bbr', 'reno']:
        for r in rows:
            if r[algo] < 1024:
                print(f"   {algo.upper()}: ±{r['jitter']-2 if r['jitter'] > 0 else 0}ms")
                break

    return rows


def main():
    print("Congestion Control Comparison Analysis - Revised Plots")
    print("="*70)

    data = load_jitter_data()

    if len(data) < 2:
        print("\nNeed at least 2 algorithms to compare.")
        return 1

    print("\nGenerating plots...")
    plot_linear_comparison(data)
    plot_log_comparison(data)
    plot_retention_comparison(data)
    plot_cliff_detail(data)
    plot_bbr_advantage(data)

    create_summary_table(data)

    print(f"\n\nAll plots saved to: {OUTPUT_DIR}")
    return 0


if __name__ == '__main__':
    exit(main())
