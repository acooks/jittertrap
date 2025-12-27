#!/usr/bin/env python3
"""
Buffer Sizing Analysis for Phase 2 Experiments

Produces clear visualizations that distinguish:
- INPUTS (what we controlled): buffer size, stall duration, RTT, jitter
- OUTPUTS (what we measured): throughput, zero-window events

Each plot clearly labels axes as "Input" or "Output (measured)"
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
DATA_DIR = EXPERIMENT_DIR / 'results' / 'buffer-sizing'
OUTPUT_DIR = DATA_DIR / 'plots'
OUTPUT_DIR.mkdir(exist_ok=True)

plt.style.use('seaborn-v0_8-whitegrid')
plt.rcParams['figure.figsize'] = (14, 8)
plt.rcParams['font.size'] = 11


def format_bytes(n):
    """Format bytes as human-readable string."""
    if n >= 1024*1024:
        return f"{n/(1024*1024):.0f}MB"
    elif n >= 1024:
        return f"{n/1024:.0f}KB"
    return f"{n}B"


def load_data():
    """Load all buffer sizing experiment data."""
    data = {}
    for name in ['buf-stall-absorption', 'buf-bdp-coverage', 'buf-jitter-absorption']:
        csv_path = DATA_DIR / f'{name}.csv'
        if csv_path.exists():
            data[name] = pd.read_csv(csv_path)
            print(f"Loaded {name}: {len(data[name])} experiments")
    return data


def plot_stall_absorption(df):
    """
    Experiment 2.1: Stall Absorption

    Question: How much buffer is needed to absorb receiver processing stalls?

    INPUTS (controlled):
    - Buffer size: 16KB to 512KB
    - Stall duration: 5ms to 100ms (time receiver waits between reads)
    - Network: Perfect (0 delay, 0 loss)
    - Send rate: 10 Mbps

    OUTPUTS (measured):
    - Zero-window events: Count of times receiver said "buffer full, stop sending"
    - Actual throughput: What rate was achieved
    """
    print("\n" + "="*70)
    print("EXPERIMENT 2.1: STALL ABSORPTION")
    print("="*70)
    print("\nQuestion: How much buffer absorbs receiver processing stalls?")
    print("\nINPUTS (what we controlled):")
    print("  - Buffer size: 16KB to 512KB")
    print("  - Stall duration: 5ms to 100ms")
    print("  - Network: Perfect (no delay/loss)")
    print("  - Send rate: 10 Mbps")
    print("\nOUTPUTS (what we measured):")
    print("  - Zero-window events")
    print("  - Actual throughput")

    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle('Experiment 2.1: Stall Absorption\n'
                 'Question: How much buffer absorbs receiver processing stalls?',
                 fontsize=14, fontweight='bold')

    # Prepare data
    buffers = sorted(df['recv_buf'].unique())
    stalls = sorted(df['delay_ms'].unique())

    # Create pivot tables
    zw_pivot = df.pivot_table(index='recv_buf', columns='delay_ms',
                               values='zero_window_count', aggfunc='mean')
    tp_pivot = df.pivot_table(index='recv_buf', columns='delay_ms',
                               values='actual_throughput_kbps', aggfunc='mean')

    # Plot 1: Zero-window heatmap
    ax1 = axes[0, 0]
    im1 = ax1.imshow(zw_pivot.values, aspect='auto', cmap='RdYlGn_r',
                     vmin=0, vmax=max(100, zw_pivot.values.max()))
    ax1.set_xticks(range(len(zw_pivot.columns)))
    ax1.set_xticklabels([f"{int(s)}" for s in zw_pivot.columns])
    ax1.set_yticks(range(len(zw_pivot.index)))
    ax1.set_yticklabels([format_bytes(int(b)) for b in zw_pivot.index])
    ax1.set_xlabel('INPUT: Stall Duration (ms)', fontweight='bold')
    ax1.set_ylabel('INPUT: Buffer Size', fontweight='bold')
    ax1.set_title('OUTPUT: Zero-Window Events\n(Green=few, Red=many)')
    plt.colorbar(im1, ax=ax1, label='Zero-Window Count')

    # Add value annotations
    for i in range(len(zw_pivot.index)):
        for j in range(len(zw_pivot.columns)):
            val = zw_pivot.values[i, j]
            color = 'white' if val > 50 else 'black'
            ax1.text(j, i, f'{int(val)}', ha='center', va='center',
                    color=color, fontsize=8)

    # Plot 2: Throughput heatmap
    ax2 = axes[0, 1]
    im2 = ax2.imshow(tp_pivot.values / 1024, aspect='auto', cmap='viridis')
    ax2.set_xticks(range(len(tp_pivot.columns)))
    ax2.set_xticklabels([f"{int(s)}" for s in tp_pivot.columns])
    ax2.set_yticks(range(len(tp_pivot.index)))
    ax2.set_yticklabels([format_bytes(int(b)) for b in tp_pivot.index])
    ax2.set_xlabel('INPUT: Stall Duration (ms)', fontweight='bold')
    ax2.set_ylabel('INPUT: Buffer Size', fontweight='bold')
    ax2.set_title('OUTPUT: Actual Throughput (MB/s)\n(Bright=high, Dark=low)')
    plt.colorbar(im2, ax=ax2, label='Throughput (MB/s)')

    # Plot 3: Line plot - throughput vs stall for each buffer size
    ax3 = axes[1, 0]
    colors = plt.cm.viridis(np.linspace(0.2, 0.8, len(buffers)))
    for i, buf in enumerate(buffers):
        buf_data = df[df['recv_buf'] == buf].sort_values('delay_ms')
        ax3.plot(buf_data['delay_ms'], buf_data['actual_throughput_kbps'] / 1024,
                'o-', color=colors[i], label=format_bytes(buf), linewidth=2)

    ax3.set_xlabel('INPUT: Stall Duration (ms)', fontweight='bold')
    ax3.set_ylabel('OUTPUT: Throughput (MB/s)', fontweight='bold')
    ax3.set_title('Throughput vs Stall Duration')
    ax3.legend(title='Buffer Size', loc='upper right', fontsize=9)
    ax3.grid(True, alpha=0.3)

    # Add theoretical limit line
    # Theoretical throughput = read_size / stall_duration
    read_size = df['read_size'].iloc[0]  # 8192 bytes
    theoretical_stalls = np.array(stalls)
    theoretical_tp = (read_size / (theoretical_stalls / 1000)) / (1024 * 1024)  # MB/s
    ax3.plot(theoretical_stalls, theoretical_tp, 'r--', linewidth=2,
             label='Theoretical max', alpha=0.7)
    ax3.legend(title='Buffer Size', loc='upper right', fontsize=9)

    # Plot 4: Interpretation
    ax4 = axes[1, 1]
    ax4.axis('off')
    interpretation = """
    INTERPRETATION:

    1. Throughput is determined by stall duration, not buffer size
       - At 10ms stall: ~780 KB/s regardless of buffer
       - At 50ms stall: ~160 KB/s regardless of buffer
       - Formula: throughput ≈ read_size / stall_duration

    2. Zero-window events show unexpected pattern:
       - Small buffers (16-32KB): Few zero-window events
       - Large buffers (64KB+): Many zero-window events

    3. Possible explanation:
       - Larger buffers allow more data to accumulate
       - This triggers more "buffer full" signals
       - But throughput is still limited by receiver speed

    4. Key insight:
       Buffer size doesn't help when receiver is slow.
       The receiver's processing speed is the bottleneck.
    """
    ax4.text(0.05, 0.95, interpretation, transform=ax4.transAxes,
             fontsize=11, verticalalignment='top', fontfamily='monospace',
             bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'stall_absorption.png', dpi=150, bbox_inches='tight')
    print(f"\nSaved: {OUTPUT_DIR / 'stall_absorption.png'}")
    plt.close()


def plot_bdp_coverage(df):
    """
    Experiment 2.2: BDP Coverage

    Question: How much buffer is needed for high-latency network links?

    INPUTS (controlled):
    - Buffer size: 16KB to 1MB
    - Network RTT: 10ms to 300ms
    - Receiver: Fast (1ms stall - not a bottleneck)
    - Send rate: 10 Mbps

    OUTPUTS (measured):
    - Actual throughput: What rate was achieved
    - Throughput efficiency: actual / target (%)
    """
    print("\n" + "="*70)
    print("EXPERIMENT 2.2: BDP COVERAGE")
    print("="*70)
    print("\nQuestion: How much buffer for high-latency links?")
    print("\nINPUTS (what we controlled):")
    print("  - Buffer size: 16KB to 1MB")
    print("  - Network RTT: 10ms to 300ms")
    print("  - Receiver: Fast (1ms stall)")
    print("  - Send rate: 10 Mbps")
    print("\nOUTPUTS (what we measured):")
    print("  - Actual throughput")
    print("  - Throughput efficiency (%)")

    # Calculate derived metrics
    df = df.copy()
    df['rtt_s'] = df['net_delay_ms'] * 2 / 1000  # RTT in seconds
    df['bdp_bytes'] = df['rtt_s'] * (10 * 1024 * 1024)  # BDP at 10 MB/s
    df['efficiency'] = (df['actual_throughput_kbps'] / (10 * 1024)) * 100

    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle('Experiment 2.2: BDP Coverage\n'
                 'Question: How much buffer for high-latency links?',
                 fontsize=14, fontweight='bold')

    buffers = sorted(df['recv_buf'].unique())
    rtts = sorted(df['net_delay_ms'].unique())

    # Plot 1: Throughput vs RTT (line plot)
    ax1 = axes[0, 0]
    colors = plt.cm.viridis(np.linspace(0.2, 0.9, len(buffers)))
    for i, buf in enumerate(buffers):
        buf_data = df[df['recv_buf'] == buf].sort_values('net_delay_ms')
        ax1.plot(buf_data['net_delay_ms'] * 2, buf_data['actual_throughput_kbps'] / 1024,
                'o-', color=colors[i], label=format_bytes(buf), linewidth=2, markersize=6)

    ax1.axhline(y=10, color='red', linestyle='--', alpha=0.5, linewidth=2)
    ax1.text(310, 10.2, 'Target: 10 MB/s', color='red', fontsize=10)
    ax1.set_xlabel('INPUT: Round-Trip Time (ms)', fontweight='bold')
    ax1.set_ylabel('OUTPUT: Throughput (MB/s)', fontweight='bold')
    ax1.set_title('Throughput Achieved at Each RTT')
    ax1.legend(title='Buffer Size', loc='upper right', fontsize=9)
    ax1.set_ylim(0, 11)
    ax1.grid(True, alpha=0.3)

    # Plot 2: Efficiency heatmap
    ax2 = axes[0, 1]
    eff_pivot = df.pivot_table(index='recv_buf', columns='net_delay_ms',
                                values='efficiency', aggfunc='mean')
    im2 = ax2.imshow(eff_pivot.values, aspect='auto', cmap='RdYlGn',
                     vmin=0, vmax=100)
    ax2.set_xticks(range(len(eff_pivot.columns)))
    ax2.set_xticklabels([f"{int(r*2)}" for r in eff_pivot.columns])
    ax2.set_yticks(range(len(eff_pivot.index)))
    ax2.set_yticklabels([format_bytes(int(b)) for b in eff_pivot.index])
    ax2.set_xlabel('INPUT: RTT (ms)', fontweight='bold')
    ax2.set_ylabel('INPUT: Buffer Size', fontweight='bold')
    ax2.set_title('OUTPUT: Throughput Efficiency (%)\n(Green=good, Red=bad)')
    plt.colorbar(im2, ax=ax2, label='Efficiency %')

    # Add value annotations
    for i in range(len(eff_pivot.index)):
        for j in range(len(eff_pivot.columns)):
            val = eff_pivot.values[i, j]
            color = 'white' if val < 50 else 'black'
            ax2.text(j, i, f'{val:.0f}', ha='center', va='center',
                    color=color, fontsize=8)

    # Plot 3: Buffer vs BDP comparison
    ax3 = axes[1, 0]
    # For each RTT, show what buffer is needed
    for buf in [65536, 131072, 262144, 524288, 1048576]:
        buf_data = df[df['recv_buf'] == buf].sort_values('net_delay_ms')
        rtt_values = buf_data['net_delay_ms'] * 2
        bdp_values = buf_data['bdp_bytes'] / 1024  # KB
        eff_values = buf_data['efficiency']

        # Color points by efficiency
        scatter = ax3.scatter(rtt_values, [buf/1024]*len(rtt_values),
                             c=eff_values, cmap='RdYlGn', vmin=0, vmax=100,
                             s=150, edgecolors='black', linewidth=0.5)

    # Plot BDP line
    rtt_range = np.array([20, 50, 100, 150, 200, 300, 600])
    bdp_line = (rtt_range / 1000) * (10 * 1024)  # KB
    ax3.plot(rtt_range, bdp_line, 'b--', linewidth=2, label='BDP = bitrate × RTT')

    ax3.set_xlabel('INPUT: RTT (ms)', fontweight='bold')
    ax3.set_ylabel('INPUT: Buffer Size (KB)', fontweight='bold')
    ax3.set_title('Buffer Size vs BDP Requirement\n(Color = efficiency %)')
    ax3.set_yscale('log')
    ax3.set_xscale('log')
    ax3.legend(loc='upper left')
    plt.colorbar(scatter, ax=ax3, label='Efficiency %')
    ax3.grid(True, alpha=0.3)

    # Plot 4: Interpretation
    ax4 = axes[1, 1]
    ax4.axis('off')
    interpretation = """
    INTERPRETATION:

    1. The BDP Rule Works:
       Buffer must be >= Bandwidth × RTT for full throughput

       For 10 Mbps (1.25 MB/s):
       - 20ms RTT → need 25 KB buffer (BDP)
       - 100ms RTT → need 125 KB buffer
       - 200ms RTT → need 250 KB buffer

    2. Practical buffer sizes needed:

       | RTT    | Theoretical | Measured for 90% |
       |--------|-------------|------------------|
       | 20ms   | 25 KB       | ~64 KB           |
       | 50ms   | 62 KB       | ~128 KB          |
       | 100ms  | 125 KB      | ~256 KB          |
       | 200ms  | 250 KB      | ~512 KB          |

    3. Key insight:
       Network latency is absorbed by buffering.
       Buffer >= 2× BDP provides good margin.
    """
    ax4.text(0.05, 0.95, interpretation, transform=ax4.transAxes,
             fontsize=11, verticalalignment='top', fontfamily='monospace',
             bbox=dict(boxstyle='round', facecolor='lightblue', alpha=0.5))

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'bdp_coverage.png', dpi=150, bbox_inches='tight')
    print(f"\nSaved: {OUTPUT_DIR / 'bdp_coverage.png'}")
    plt.close()


def plot_jitter_absorption(df):
    """
    Experiment 2.3: Jitter Absorption

    Question: Does larger buffer help with network jitter?

    INPUTS (controlled):
    - Buffer size: 64KB to 512KB
    - Network jitter: ±10ms to ±100ms
    - Base RTT: 50ms (fixed)
    - Receiver: Fast (1ms stall)
    - Send rate: 10 Mbps

    OUTPUTS (measured):
    - Actual throughput
    """
    print("\n" + "="*70)
    print("EXPERIMENT 2.3: JITTER ABSORPTION")
    print("="*70)
    print("\nQuestion: Does larger buffer help with network jitter?")
    print("\nINPUTS (what we controlled):")
    print("  - Buffer size: 64KB to 512KB")
    print("  - Network jitter: ±10ms to ±100ms")
    print("  - Base RTT: 50ms")
    print("  - Receiver: Fast (1ms stall)")
    print("\nOUTPUTS (what we measured):")
    print("  - Actual throughput")

    fig, axes = plt.subplots(1, 2, figsize=(14, 6))
    fig.suptitle('Experiment 2.3: Jitter Absorption\n'
                 'Question: Does larger buffer help with network jitter?',
                 fontsize=14, fontweight='bold')

    buffers = sorted(df['recv_buf'].unique())
    jitters = sorted(df['net_jitter_ms'].unique())

    # Plot 1: Throughput vs Jitter (line plot)
    ax1 = axes[0]
    colors = plt.cm.viridis(np.linspace(0.2, 0.8, len(buffers)))
    for i, buf in enumerate(buffers):
        buf_data = df[df['recv_buf'] == buf].sort_values('net_jitter_ms')
        ax1.plot(buf_data['net_jitter_ms'], buf_data['actual_throughput_kbps'],
                'o-', color=colors[i], label=format_bytes(buf), linewidth=2, markersize=8)

    ax1.axvline(x=30, color='red', linestyle='--', alpha=0.7, linewidth=2)
    ax1.text(32, ax1.get_ylim()[1]*0.9, 'Cliff at ~30ms', color='red', fontsize=10)

    ax1.set_xlabel('INPUT: Network Jitter (±ms)', fontweight='bold')
    ax1.set_ylabel('OUTPUT: Throughput (KB/s)', fontweight='bold')
    ax1.set_title('Throughput Collapses Above ~30ms Jitter')
    ax1.legend(title='Buffer Size', loc='upper right')
    ax1.grid(True, alpha=0.3)

    # Plot 2: Interpretation
    ax2 = axes[1]
    ax2.axis('off')
    interpretation = """
    INTERPRETATION:

    1. CRITICAL FINDING: Jitter Causes Throughput Collapse

       | Jitter  | Throughput | Notes           |
       |---------|------------|-----------------|
       | ±10ms   | ~1-2 MB/s  | Acceptable      |
       | ±20ms   | ~300 KB/s  | Degraded        |
       | ±30ms+  | ~50 KB/s   | Catastrophic    |

    2. Buffer size does NOT help:
       - All buffer sizes show same cliff at ~30ms
       - 512KB buffer performs same as 64KB

    3. Why this happens:
       - TCP interprets jitter as congestion
       - Out-of-order packets trigger retransmits
       - Congestion window shrinks dramatically
       - This is TCP working as designed

    4. Key insight:
       Jitter > 30ms is a network problem that
       cannot be solved by buffer sizing.
       The network path must be improved.
    """
    ax2.text(0.05, 0.95, interpretation, transform=ax2.transAxes,
             fontsize=11, verticalalignment='top', fontfamily='monospace',
             bbox=dict(boxstyle='round', facecolor='lightyellow', alpha=0.5))

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'jitter_absorption.png', dpi=150, bbox_inches='tight')
    print(f"\nSaved: {OUTPUT_DIR / 'jitter_absorption.png'}")
    plt.close()


def create_summary_plot(data):
    """Create a single summary plot showing all three experiments."""

    fig, axes = plt.subplots(1, 3, figsize=(16, 5))
    fig.suptitle('Phase 2 Summary: Buffer Sizing Experiments\n'
                 'What determines TCP throughput in different scenarios?',
                 fontsize=14, fontweight='bold')

    # Panel 1: Stall Absorption
    if 'buf-stall-absorption' in data:
        df = data['buf-stall-absorption']
        ax = axes[0]

        # Show throughput vs stall for one buffer size
        buf_data = df[df['recv_buf'] == 65536].sort_values('delay_ms')
        ax.plot(buf_data['delay_ms'], buf_data['actual_throughput_kbps'] / 1024,
               'bo-', linewidth=2, markersize=8)

        # Theoretical line
        stalls = buf_data['delay_ms'].values
        theoretical = (8192 / (stalls / 1000)) / (1024 * 1024)
        ax.plot(stalls, theoretical, 'r--', linewidth=2, label='Theoretical max')

        ax.set_xlabel('Stall Duration (ms)')
        ax.set_ylabel('Throughput (MB/s)')
        ax.set_title('Receiver Stalls\n(Buffer size doesn\'t help)')
        ax.legend()
        ax.grid(True, alpha=0.3)
        ax.set_ylim(0, 2)

    # Panel 2: BDP Coverage
    if 'buf-bdp-coverage' in data:
        df = data['buf-bdp-coverage']
        ax = axes[1]

        buffers = [65536, 262144, 1048576]
        colors = ['red', 'orange', 'green']
        labels = ['64KB (too small)', '256KB (marginal)', '1MB (adequate)']

        for buf, color, label in zip(buffers, colors, labels):
            buf_data = df[df['recv_buf'] == buf].sort_values('net_delay_ms')
            ax.plot(buf_data['net_delay_ms'] * 2, buf_data['actual_throughput_kbps'] / 1024,
                   'o-', color=color, linewidth=2, markersize=8, label=label)

        ax.axhline(y=10, color='gray', linestyle=':', alpha=0.5)
        ax.set_xlabel('RTT (ms)')
        ax.set_ylabel('Throughput (MB/s)')
        ax.set_title('Network Latency\n(Larger buffer helps)')
        ax.legend(fontsize=9)
        ax.grid(True, alpha=0.3)
        ax.set_ylim(0, 11)

    # Panel 3: Jitter
    if 'buf-jitter-absorption' in data:
        df = data['buf-jitter-absorption']
        ax = axes[2]

        # Show throughput vs jitter for one buffer size
        buf_data = df[df['recv_buf'] == 262144].sort_values('net_jitter_ms')
        ax.plot(buf_data['net_jitter_ms'], buf_data['actual_throughput_kbps'],
               'go-', linewidth=2, markersize=8)

        ax.axvline(x=30, color='red', linestyle='--', alpha=0.7)
        ax.text(35, 2000, 'Cliff', color='red', fontsize=10)

        ax.set_xlabel('Jitter (±ms)')
        ax.set_ylabel('Throughput (KB/s)')
        ax.set_title('Network Jitter\n(Nothing helps past ±30ms)')
        ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'summary.png', dpi=150, bbox_inches='tight')
    print(f"\nSaved: {OUTPUT_DIR / 'summary.png'}")
    plt.close()


def main():
    print("Buffer Sizing Analysis - Phase 2")
    print("="*70)
    print("\nThis analysis distinguishes:")
    print("  INPUTS  = What we controlled (buffer, stall, RTT, jitter)")
    print("  OUTPUTS = What we measured (throughput, zero-window events)")

    data = load_data()

    if not data:
        print("\nNo data files found. Run experiments first.")
        return 1

    if 'buf-stall-absorption' in data:
        plot_stall_absorption(data['buf-stall-absorption'])

    if 'buf-bdp-coverage' in data:
        plot_bdp_coverage(data['buf-bdp-coverage'])

    if 'buf-jitter-absorption' in data:
        plot_jitter_absorption(data['buf-jitter-absorption'])

    create_summary_plot(data)

    print("\n" + "="*70)
    print("SUMMARY OF FINDINGS")
    print("="*70)
    print("""
    1. RECEIVER STALLS: Buffer doesn't help
       - Throughput = read_size / stall_duration
       - Receiver speed is the bottleneck

    2. NETWORK LATENCY: Buffer helps (BDP rule)
       - Buffer >= Bandwidth × RTT
       - Larger buffer = higher throughput on slow links

    3. NETWORK JITTER: Nothing helps past ~30ms
       - TCP interprets jitter as congestion
       - Throughput collapses regardless of buffer
       - This requires network-level fixes
    """)

    print(f"\nPlots saved to: {OUTPUT_DIR}")
    return 0


if __name__ == '__main__':
    exit(main())
