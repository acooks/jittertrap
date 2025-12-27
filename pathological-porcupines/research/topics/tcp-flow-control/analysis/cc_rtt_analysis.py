#!/usr/bin/env python3
"""
Phase 2d: RTT × CC Algorithm Analysis

Generates plots from the comprehensive cc-rtt-sweep experiment showing:
1. Throughput vs Jitter by RTT (with error bars)
2. BBR/CUBIC ratio heatmap
3. Cliff location comparison
4. The chaos zone (high CV regions)
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
DATA_FILE = EXPERIMENT_DIR / 'results' / 'cc-rtt-sweep' / 'cc-rtt-sweep.csv'
OUTPUT_DIR = EXPERIMENT_DIR / 'results' / 'cc-rtt-sweep' / 'plots'
OUTPUT_DIR.mkdir(exist_ok=True, parents=True)

plt.style.use('seaborn-v0_8-whitegrid')
plt.rcParams['figure.figsize'] = (14, 8)
plt.rcParams['font.size'] = 11


def load_data():
    """Load and preprocess the data."""
    df = pd.read_csv(DATA_FILE)
    df['rtt'] = df['net_delay_ms'] * 2
    df['jitter_pct_rtt'] = (df['net_jitter_ms'] / df['rtt']) * 100
    return df


def compute_stats(df):
    """Compute mean, std, cv for each condition."""
    stats = df.groupby(['rtt', 'net_jitter_ms', 'congestion_algo']).agg({
        'actual_throughput_kbps': ['mean', 'std', 'count']
    }).reset_index()
    stats.columns = ['rtt', 'jitter', 'algo', 'mean', 'std', 'count']
    stats['cv'] = (stats['std'] / stats['mean'] * 100).fillna(0)
    stats['jitter_pct'] = (stats['jitter'] / stats['rtt']) * 100
    return stats


def plot_throughput_by_rtt(stats):
    """Plot 1: Throughput vs Jitter for each RTT, with error bars."""
    fig, axes = plt.subplots(1, 3, figsize=(16, 5), sharey=False)

    colors = {'cubic': '#d62728', 'bbr': '#2ca02c'}
    markers = {'cubic': 'o', 'bbr': 's'}

    for idx, rtt in enumerate([24, 50, 100]):
        ax = axes[idx]
        for algo in ['cubic', 'bbr']:
            data = stats[(stats['rtt'] == rtt) & (stats['algo'] == algo)].sort_values('jitter')
            ax.errorbar(data['jitter'], data['mean'] / 1024,
                       yerr=data['std'] / 1024,
                       marker=markers[algo], color=colors[algo],
                       linewidth=2, markersize=8, capsize=4,
                       label=algo.upper(), markeredgecolor='white', markeredgewidth=1)

        ax.set_xlabel('Jitter (±ms)', fontsize=11, fontweight='bold')
        ax.set_ylabel('Throughput (MB/s)', fontsize=11, fontweight='bold')
        ax.set_title(f'RTT = {rtt}ms', fontsize=12, fontweight='bold')
        ax.legend(loc='upper right')
        ax.set_xlim(-1, 26)
        ax.grid(True, alpha=0.3)

        # Shade chaos zone (10-25% of RTT)
        chaos_start = rtt * 0.10
        chaos_end = rtt * 0.25
        ax.axvspan(chaos_start, chaos_end, alpha=0.15, color='orange', label='Chaos zone')

    fig.suptitle('Throughput vs Jitter by RTT\n(Error bars show ±1 std dev from 3 runs)',
                 fontsize=14, fontweight='bold', y=1.02)
    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'throughput_by_rtt.png', dpi=150, bbox_inches='tight')
    print(f"Saved: {OUTPUT_DIR / 'throughput_by_rtt.png'}")
    plt.close()


def plot_bbr_ratio_heatmap(stats):
    """Plot 2: BBR/CUBIC ratio heatmap."""
    fig, ax = plt.subplots(figsize=(10, 6))

    # Pivot to get ratio
    rtts = [24, 50, 100]
    jitters = sorted(stats['jitter'].unique())

    ratio_matrix = np.zeros((len(rtts), len(jitters)))
    for i, rtt in enumerate(rtts):
        for j, jitter in enumerate(jitters):
            cubic = stats[(stats['rtt'] == rtt) & (stats['jitter'] == jitter) &
                         (stats['algo'] == 'cubic')]['mean'].values
            bbr = stats[(stats['rtt'] == rtt) & (stats['jitter'] == jitter) &
                       (stats['algo'] == 'bbr')]['mean'].values
            if len(cubic) > 0 and len(bbr) > 0 and cubic[0] > 0:
                ratio_matrix[i, j] = bbr[0] / cubic[0]

    # Custom colormap: red (BBR worse) -> white (equal) -> green (BBR better)
    from matplotlib.colors import TwoSlopeNorm
    norm = TwoSlopeNorm(vmin=0.3, vcenter=1.0, vmax=5.0)

    im = ax.imshow(ratio_matrix, cmap='RdYlGn', norm=norm, aspect='auto')

    # Labels
    ax.set_xticks(range(len(jitters)))
    ax.set_xticklabels([f'±{j}' for j in jitters])
    ax.set_yticks(range(len(rtts)))
    ax.set_yticklabels([f'{r}ms' for r in rtts])
    ax.set_xlabel('Jitter (ms)', fontsize=12, fontweight='bold')
    ax.set_ylabel('RTT', fontsize=12, fontweight='bold')

    # Annotate cells
    for i in range(len(rtts)):
        for j in range(len(jitters)):
            val = ratio_matrix[i, j]
            color = 'white' if val < 0.6 or val > 2.5 else 'black'
            ax.text(j, i, f'{val:.1f}x', ha='center', va='center',
                   fontsize=10, fontweight='bold', color=color)

    cbar = plt.colorbar(im, ax=ax, shrink=0.8)
    cbar.set_label('BBR / CUBIC ratio', fontsize=11)

    ax.set_title('BBR vs CUBIC: When Does Each Win?\n'
                 '(Green = BBR better, Red = CUBIC better)',
                 fontsize=14, fontweight='bold')

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'bbr_cubic_ratio_heatmap.png', dpi=150, bbox_inches='tight')
    print(f"Saved: {OUTPUT_DIR / 'bbr_cubic_ratio_heatmap.png'}")
    plt.close()


def plot_cv_heatmap(stats):
    """Plot 3: Coefficient of Variation heatmap showing chaos zones."""
    fig, axes = plt.subplots(1, 2, figsize=(14, 5))

    for idx, algo in enumerate(['cubic', 'bbr']):
        ax = axes[idx]
        rtts = [24, 50, 100]
        jitters = sorted(stats['jitter'].unique())

        cv_matrix = np.zeros((len(rtts), len(jitters)))
        for i, rtt in enumerate(rtts):
            for j, jitter in enumerate(jitters):
                cv = stats[(stats['rtt'] == rtt) & (stats['jitter'] == jitter) &
                          (stats['algo'] == algo)]['cv'].values
                if len(cv) > 0:
                    cv_matrix[i, j] = cv[0]

        im = ax.imshow(cv_matrix, cmap='YlOrRd', vmin=0, vmax=100, aspect='auto')

        ax.set_xticks(range(len(jitters)))
        ax.set_xticklabels([f'±{j}' for j in jitters])
        ax.set_yticks(range(len(rtts)))
        ax.set_yticklabels([f'{r}ms' for r in rtts])
        ax.set_xlabel('Jitter (ms)', fontsize=11, fontweight='bold')
        ax.set_ylabel('RTT', fontsize=11, fontweight='bold')
        ax.set_title(f'{algo.upper()}', fontsize=12, fontweight='bold')

        # Annotate high CV cells
        for i in range(len(rtts)):
            for j in range(len(jitters)):
                val = cv_matrix[i, j]
                if val > 20:
                    color = 'white' if val > 50 else 'black'
                    ax.text(j, i, f'{val:.0f}%', ha='center', va='center',
                           fontsize=9, fontweight='bold', color=color)

    fig.suptitle('Coefficient of Variation: Where Results Are Unpredictable\n'
                 '(Higher = more chaotic, single measurements unreliable)',
                 fontsize=14, fontweight='bold', y=1.02)

    cbar = fig.colorbar(im, ax=axes, shrink=0.8, location='right')
    cbar.set_label('CV (%)', fontsize=11)

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'cv_chaos_zones.png', dpi=150, bbox_inches='tight')
    print(f"Saved: {OUTPUT_DIR / 'cv_chaos_zones.png'}")
    plt.close()


def plot_cliff_comparison(stats):
    """Plot 4: Where does throughput collapse for each algorithm?"""
    fig, ax = plt.subplots(figsize=(12, 7))

    colors = {'cubic': '#d62728', 'bbr': '#2ca02c'}

    # For each RTT and algo, find where throughput drops below 50% of baseline
    results = []
    for rtt in [24, 50, 100]:
        for algo in ['cubic', 'bbr']:
            data = stats[(stats['rtt'] == rtt) & (stats['algo'] == algo)].sort_values('jitter')
            baseline = data[data['jitter'] == 0]['mean'].values[0]

            for _, row in data.iterrows():
                retention = (row['mean'] / baseline) * 100
                results.append({
                    'rtt': rtt,
                    'algo': algo,
                    'jitter': row['jitter'],
                    'jitter_pct': row['jitter_pct'],
                    'retention': retention
                })

    results_df = pd.DataFrame(results)

    # Plot retention vs jitter as % of RTT
    for algo in ['cubic', 'bbr']:
        algo_data = results_df[results_df['algo'] == algo]
        # Average across RTTs for each jitter_pct bucket
        for rtt in [24, 50, 100]:
            rtt_data = algo_data[algo_data['rtt'] == rtt].sort_values('jitter_pct')
            linestyle = '-' if rtt == 50 else ('--' if rtt == 24 else ':')
            ax.plot(rtt_data['jitter_pct'], rtt_data['retention'],
                   color=colors[algo], linestyle=linestyle, linewidth=2,
                   marker='o' if algo == 'cubic' else 's', markersize=6,
                   label=f'{algo.upper()} ({rtt}ms RTT)')

    ax.axhline(y=50, color='gray', linestyle='--', alpha=0.7, linewidth=1.5)
    ax.axhline(y=10, color='red', linestyle='--', alpha=0.7, linewidth=1.5)
    ax.text(102, 52, '50% - Degraded', fontsize=9, color='gray')
    ax.text(102, 12, '10% - Unusable', fontsize=9, color='red')

    ax.set_xlabel('Jitter as % of RTT', fontsize=12, fontweight='bold')
    ax.set_ylabel('Throughput Retention (%)', fontsize=12, fontweight='bold')
    ax.set_title('Throughput Retention vs Jitter (normalized by RTT)\n'
                 'The cliff location varies by algorithm AND RTT',
                 fontsize=14, fontweight='bold')
    ax.legend(loc='upper right', fontsize=9, ncol=2)
    ax.set_xlim(-5, 105)
    ax.set_ylim(0, 110)
    ax.grid(True, alpha=0.3)

    # Shade regions
    ax.axhspan(50, 110, alpha=0.1, color='green')
    ax.axhspan(10, 50, alpha=0.1, color='yellow')
    ax.axhspan(0, 10, alpha=0.1, color='red')

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'cliff_comparison.png', dpi=150, bbox_inches='tight')
    print(f"Saved: {OUTPUT_DIR / 'cliff_comparison.png'}")
    plt.close()


def plot_summary(stats):
    """Plot 5: Executive summary - the key takeaway."""
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))

    # Panel 1: Best algorithm by condition
    ax = axes[0, 0]
    rtts = [24, 50, 100]
    jitters = sorted(stats['jitter'].unique())

    winner_matrix = np.zeros((len(rtts), len(jitters)))
    for i, rtt in enumerate(rtts):
        for j, jitter in enumerate(jitters):
            cubic = stats[(stats['rtt'] == rtt) & (stats['jitter'] == jitter) &
                         (stats['algo'] == 'cubic')]['mean'].values
            bbr = stats[(stats['rtt'] == rtt) & (stats['jitter'] == jitter) &
                       (stats['algo'] == 'bbr')]['mean'].values
            cubic_cv = stats[(stats['rtt'] == rtt) & (stats['jitter'] == jitter) &
                            (stats['algo'] == 'cubic')]['cv'].values
            bbr_cv = stats[(stats['rtt'] == rtt) & (stats['jitter'] == jitter) &
                          (stats['algo'] == 'bbr')]['cv'].values

            if len(cubic) > 0 and len(bbr) > 0:
                ratio = bbr[0] / cubic[0] if cubic[0] > 0 else 1
                max_cv = max(cubic_cv[0] if len(cubic_cv) > 0 else 0,
                            bbr_cv[0] if len(bbr_cv) > 0 else 0)

                if max_cv > 30:  # High variance - uncertain
                    winner_matrix[i, j] = 0  # Uncertain
                elif ratio > 1.3:
                    winner_matrix[i, j] = 1  # BBR wins
                elif ratio < 0.7:
                    winner_matrix[i, j] = -1  # CUBIC wins
                else:
                    winner_matrix[i, j] = 0  # Tie

    cmap = plt.cm.colors.ListedColormap(['#d62728', '#f0f0f0', '#2ca02c'])
    im = ax.imshow(winner_matrix, cmap=cmap, vmin=-1, vmax=1, aspect='auto')

    ax.set_xticks(range(len(jitters)))
    ax.set_xticklabels([f'±{j}' for j in jitters])
    ax.set_yticks(range(len(rtts)))
    ax.set_yticklabels([f'{r}ms' for r in rtts])
    ax.set_xlabel('Jitter (ms)', fontsize=11)
    ax.set_ylabel('RTT', fontsize=11)
    ax.set_title('Which Algorithm Wins?\n(Red=CUBIC, Green=BBR, Gray=Uncertain)', fontsize=11, fontweight='bold')

    # Panel 2: Key finding text
    ax = axes[0, 1]
    ax.axis('off')
    findings = """
KEY FINDINGS

1. The jitter cliff is RTT-relative
   • CUBIC cliff: ~20-33% of RTT
   • BBR cliff: ~8-20% of RTT (earlier!)

2. BBR's advantage is NOT universal
   • Low jitter (<10% RTT): Either works
   • Chaos zone (10-25% RTT): Unpredictable
   • High jitter (>30% RTT): BBR wins 3-5x

3. Near the cliff, results are chaotic
   • CV can exceed 100%
   • Single measurements are unreliable

4. Higher RTT = more jitter tolerance
   • 100ms RTT tolerates ±20ms jitter
   • 24ms RTT collapses at ±8ms jitter
"""
    ax.text(0.1, 0.9, findings, transform=ax.transAxes, fontsize=11,
           verticalalignment='top', fontfamily='monospace',
           bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))

    # Panel 3: Throughput floor comparison
    ax = axes[1, 0]
    # At high jitter, what's the floor?
    high_jitter_data = stats[stats['jitter'] >= 16]
    for algo in ['cubic', 'bbr']:
        algo_data = high_jitter_data[high_jitter_data['algo'] == algo]
        means = algo_data.groupby('rtt')['mean'].mean()
        ax.bar([r + (-0.2 if algo == 'cubic' else 0.2) for r in range(len(means))],
               means.values / 1024, width=0.35,
               color='#d62728' if algo == 'cubic' else '#2ca02c',
               label=algo.upper())
    ax.set_xticks(range(len(means)))
    ax.set_xticklabels([f'{r}ms' for r in means.index])
    ax.set_xlabel('RTT', fontsize=11)
    ax.set_ylabel('Avg Throughput at High Jitter (MB/s)', fontsize=11)
    ax.set_title('Throughput Floor at High Jitter (≥16ms)\nBBR maintains higher floor', fontsize=11, fontweight='bold')
    ax.legend()

    # Panel 4: Recommendation
    ax = axes[1, 1]
    ax.axis('off')
    rec = """
RECOMMENDATIONS

For stable networks (jitter < 10% of RTT):
  → Use CUBIC (default) - slightly more efficient

For moderate jitter (10-25% of RTT):
  → TEST BOTH - results are unpredictable
  → Don't trust single measurements

For high jitter (> 30% of RTT):
  → Use BBR - consistent 3-5x advantage

For Starlink (~50ms RTT, 10-30ms jitter):
  → Jitter/RTT = 20-60%
  → BBR probably better, but test your link

To enable BBR:
  sysctl net.ipv4.tcp_congestion_control=bbr
"""
    ax.text(0.1, 0.9, rec, transform=ax.transAxes, fontsize=11,
           verticalalignment='top', fontfamily='monospace',
           bbox=dict(boxstyle='round', facecolor='lightblue', alpha=0.5))

    fig.suptitle('TCP Congestion Control vs Jitter: Executive Summary',
                fontsize=14, fontweight='bold')
    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'executive_summary.png', dpi=150, bbox_inches='tight')
    print(f"Saved: {OUTPUT_DIR / 'executive_summary.png'}")
    plt.close()


def main():
    print("Phase 2d: RTT × CC Algorithm Analysis")
    print("=" * 60)

    df = load_data()
    print(f"Loaded {len(df)} experiments")

    stats = compute_stats(df)
    print(f"Computed stats for {len(stats)} conditions")

    print("\nGenerating plots...")
    plot_throughput_by_rtt(stats)
    plot_bbr_ratio_heatmap(stats)
    plot_cv_heatmap(stats)
    plot_cliff_comparison(stats)
    plot_summary(stats)

    print(f"\nAll plots saved to: {OUTPUT_DIR}")
    return 0


if __name__ == '__main__':
    exit(main())
