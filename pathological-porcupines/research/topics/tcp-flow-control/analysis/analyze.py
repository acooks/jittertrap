#!/usr/bin/env python3
"""
TCP Flow Control Analysis and Visualization

Analyzes sweep results and generates visualizations showing:
- Zero-window probability vs buffer size and delay
- Throughput degradation curves
- Parameter sensitivity analysis
- Threshold detection for starvation onset

Part of Pathological Porcupines: Network Application Failure Simulations
"""

import argparse
import csv
import json
import sys
from pathlib import Path
from typing import Optional

# Check for optional dependencies
try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False

try:
    import matplotlib.pyplot as plt
    import matplotlib.colors as mcolors
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False


def load_results(csv_path: str) -> list[dict]:
    """Load sweep results from CSV file."""
    results = []
    with open(csv_path, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            # Convert numeric fields
            for key in row:
                if key in ('success',):
                    row[key] = row[key].lower() == 'true'
                elif key not in ('timestamp', 'error'):
                    try:
                        if '.' in str(row[key]):
                            row[key] = float(row[key])
                        else:
                            row[key] = int(row[key])
                    except (ValueError, TypeError):
                        pass
            # Add source file for tracking
            row['_source_file'] = Path(csv_path).name
            results.append(row)
    return results


def load_multiple_results(csv_paths: list[str]) -> list[dict]:
    """Load and combine results from multiple CSV files."""
    all_results = []
    for path in csv_paths:
        print(f"Loading {path}...")
        results = load_results(path)
        all_results.extend(results)
        print(f"  Loaded {len(results)} experiments")
    print(f"Total: {len(all_results)} experiments from {len(csv_paths)} files")
    return all_results


def compute_statistics(results: list[dict]) -> dict:
    """Compute summary statistics from results."""
    successful = [r for r in results if r.get('success', False)]

    if not successful:
        return {'error': 'No successful experiments'}

    stats = {
        'total_experiments': len(results),
        'successful': len(successful),
        'failed': len(results) - len(successful),
    }

    # Zero-window statistics
    with_zw = [r for r in successful if r.get('zero_window_count', 0) > 0]
    stats['with_zero_window'] = len(with_zw)
    stats['zero_window_rate'] = len(with_zw) / len(successful) * 100

    if with_zw:
        zw_counts = [r['zero_window_count'] for r in with_zw]
        stats['zero_window_count_mean'] = sum(zw_counts) / len(zw_counts)
        stats['zero_window_count_max'] = max(zw_counts)

    # Parameter ranges
    stats['recv_buf_range'] = sorted(set(r['recv_buf'] for r in successful))
    stats['delay_ms_range'] = sorted(set(r['delay_ms'] for r in successful))
    stats['send_rate_range'] = sorted(set(r['send_rate_mbps'] for r in successful))

    # Throughput statistics
    throughputs = [r['actual_throughput_kbps'] for r in successful if r.get('actual_throughput_kbps', 0) > 0]
    if throughputs:
        stats['throughput_mean_kbps'] = sum(throughputs) / len(throughputs)
        stats['throughput_min_kbps'] = min(throughputs)
        stats['throughput_max_kbps'] = max(throughputs)

    return stats


def find_starvation_thresholds(results: list[dict]) -> dict:
    """
    Find the oversubscription ratio thresholds where zero-window events begin.
    Groups by (recv_buf, delay) and finds the critical send rate.
    """
    successful = [r for r in results if r.get('success', False)]

    # Group by (recv_buf, delay_ms)
    groups = {}
    for r in successful:
        key = (r['recv_buf'], r['delay_ms'])
        if key not in groups:
            groups[key] = []
        groups[key].append(r)

    thresholds = {}
    for key, group in groups.items():
        # Sort by send rate
        group.sort(key=lambda x: x['send_rate_mbps'])

        # Find first rate with zero-window
        threshold_rate = None
        for r in group:
            if r.get('zero_window_count', 0) > 0:
                threshold_rate = r['send_rate_mbps']
                break

        recv_buf, delay_ms = key
        capacity_kbps = (group[0].get('read_size', 4096) / (delay_ms / 1000)) / 1024 if delay_ms > 0 else float('inf')

        thresholds[key] = {
            'recv_buf': recv_buf,
            'delay_ms': delay_ms,
            'receiver_capacity_kbps': capacity_kbps,
            'threshold_send_rate_mbps': threshold_rate,
            'threshold_oversub_ratio': (threshold_rate * 1024) / capacity_kbps if threshold_rate and capacity_kbps > 0 else None,
        }

    return thresholds


def generate_text_report(results: list[dict], output_path: Optional[str] = None) -> str:
    """Generate text summary report."""
    stats = compute_statistics(results)
    thresholds = find_starvation_thresholds(results)

    lines = []
    lines.append("=" * 70)
    lines.append("TCP FLOW CONTROL PARAMETER SWEEP ANALYSIS")
    lines.append("=" * 70)
    lines.append("")

    lines.append("SUMMARY STATISTICS")
    lines.append("-" * 40)
    lines.append(f"Total experiments:        {stats['total_experiments']}")
    lines.append(f"Successful:               {stats['successful']}")
    lines.append(f"Failed:                   {stats['failed']}")
    lines.append(f"With zero-window events:  {stats['with_zero_window']} ({stats['zero_window_rate']:.1f}%)")
    lines.append("")

    if 'throughput_mean_kbps' in stats:
        lines.append("THROUGHPUT")
        lines.append("-" * 40)
        lines.append(f"Mean:  {stats['throughput_mean_kbps']:.0f} KB/s")
        lines.append(f"Min:   {stats['throughput_min_kbps']:.0f} KB/s")
        lines.append(f"Max:   {stats['throughput_max_kbps']:.0f} KB/s")
        lines.append("")

    lines.append("STARVATION THRESHOLDS")
    lines.append("-" * 40)
    lines.append("The oversubscription ratio at which zero-window events begin:")
    lines.append("")
    lines.append(f"{'Buffer':>10}  {'Delay':>8}  {'Capacity':>12}  {'Threshold':>10}  {'Oversub':>8}")
    lines.append(f"{'(bytes)':>10}  {'(ms)':>8}  {'(KB/s)':>12}  {'(MB/s)':>10}  {'Ratio':>8}")
    lines.append("-" * 60)

    for key in sorted(thresholds.keys()):
        t = thresholds[key]
        threshold_str = f"{t['threshold_send_rate_mbps']:.2f}" if t['threshold_send_rate_mbps'] else "N/A"
        oversub_str = f"{t['threshold_oversub_ratio']:.1f}x" if t['threshold_oversub_ratio'] else "N/A"
        lines.append(f"{t['recv_buf']:>10}  {t['delay_ms']:>8.0f}  {t['receiver_capacity_kbps']:>12.0f}  {threshold_str:>10}  {oversub_str:>8}")

    lines.append("")
    lines.append("KEY FINDINGS")
    lines.append("-" * 40)

    # Analyze threshold patterns
    valid_thresholds = [t for t in thresholds.values() if t['threshold_oversub_ratio'] is not None]
    if valid_thresholds:
        avg_oversub = sum(t['threshold_oversub_ratio'] for t in valid_thresholds) / len(valid_thresholds)
        lines.append(f"- Average oversubscription threshold: {avg_oversub:.1f}x receiver capacity")

        # Check buffer size effect
        by_buffer = {}
        for t in valid_thresholds:
            buf = t['recv_buf']
            if buf not in by_buffer:
                by_buffer[buf] = []
            by_buffer[buf].append(t['threshold_oversub_ratio'])

        if len(by_buffer) > 1:
            lines.append("- Buffer size effect on threshold:")
            for buf in sorted(by_buffer.keys()):
                avg = sum(by_buffer[buf]) / len(by_buffer[buf])
                lines.append(f"    {buf:,} bytes: {avg:.1f}x average threshold")

    lines.append("")
    lines.append("=" * 70)

    report = '\n'.join(lines)

    if output_path:
        with open(output_path, 'w') as f:
            f.write(report)

    return report


def generate_heatmap(results: list[dict], output_path: str,
                     x_param: str = 'recv_buf', y_param: str = 'delay_ms',
                     value_param: str = 'zero_window_count',
                     fixed_params: dict = None):
    """
    Generate heatmap visualization.
    Requires matplotlib.
    """
    if not HAS_MATPLOTLIB:
        print("Warning: matplotlib not available, skipping heatmap generation")
        return

    successful = [r for r in results if r.get('success', False)]

    # Filter by fixed params
    if fixed_params:
        for key, val in fixed_params.items():
            successful = [r for r in successful if r.get(key) == val]

    if not successful:
        print("Warning: No data after filtering, skipping heatmap")
        return

    # Get unique values for axes
    x_vals = sorted(set(r[x_param] for r in successful))
    y_vals = sorted(set(r[y_param] for r in successful))

    # Build matrix
    matrix = [[0.0] * len(x_vals) for _ in range(len(y_vals))]
    for r in successful:
        xi = x_vals.index(r[x_param])
        yi = y_vals.index(r[y_param])
        matrix[yi][xi] = r.get(value_param, 0)

    # Plot
    fig, ax = plt.subplots(figsize=(10, 8))

    if HAS_NUMPY:
        matrix = np.array(matrix)

    im = ax.imshow(matrix, cmap='YlOrRd', aspect='auto', origin='lower')

    # Labels
    ax.set_xticks(range(len(x_vals)))
    ax.set_yticks(range(len(y_vals)))

    if x_param == 'recv_buf':
        ax.set_xticklabels([f"{v//1024}K" for v in x_vals])
    else:
        ax.set_xticklabels(x_vals)

    ax.set_yticklabels(y_vals)

    ax.set_xlabel(x_param.replace('_', ' ').title())
    ax.set_ylabel(y_param.replace('_', ' ').title())

    title = f"{value_param.replace('_', ' ').title()}"
    if fixed_params:
        fixed_str = ', '.join(f"{k}={v}" for k, v in fixed_params.items())
        title += f" ({fixed_str})"
    ax.set_title(title)

    # Colorbar
    cbar = plt.colorbar(im)
    cbar.set_label(value_param.replace('_', ' ').title())

    # Add value annotations
    for yi in range(len(y_vals)):
        for xi in range(len(x_vals)):
            val = matrix[yi][xi] if HAS_NUMPY else matrix[yi][xi]
            if val > 0:
                ax.text(xi, yi, f"{val:.0f}", ha='center', va='center',
                       color='white' if val > matrix.max() * 0.5 else 'black', fontsize=8)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"Saved heatmap: {output_path}")


def generate_threshold_plot(results: list[dict], output_path: str):
    """
    Generate plot showing starvation threshold vs buffer size.
    """
    if not HAS_MATPLOTLIB:
        print("Warning: matplotlib not available, skipping threshold plot")
        return

    thresholds = find_starvation_thresholds(results)

    # Group by delay
    by_delay = {}
    for t in thresholds.values():
        if t['threshold_oversub_ratio'] is not None:
            delay = t['delay_ms']
            if delay not in by_delay:
                by_delay[delay] = {'buffers': [], 'ratios': []}
            by_delay[delay]['buffers'].append(t['recv_buf'])
            by_delay[delay]['ratios'].append(t['threshold_oversub_ratio'])

    if not by_delay:
        print("Warning: No threshold data, skipping plot")
        return

    fig, ax = plt.subplots(figsize=(10, 6))

    colors = plt.cm.viridis(np.linspace(0, 1, len(by_delay))) if HAS_NUMPY else ['blue', 'green', 'red', 'orange', 'purple']

    for i, (delay, data) in enumerate(sorted(by_delay.items())):
        # Sort by buffer
        sorted_pairs = sorted(zip(data['buffers'], data['ratios']))
        buffers = [p[0] for p in sorted_pairs]
        ratios = [p[1] for p in sorted_pairs]

        color = colors[i] if HAS_NUMPY else colors[i % len(colors)]
        ax.plot(buffers, ratios, 'o-', label=f'{delay}ms delay', color=color, markersize=8)

    ax.set_xlabel('Receive Buffer Size (bytes)')
    ax.set_ylabel('Oversubscription Ratio at Starvation Onset')
    ax.set_title('TCP Flow Control: When Does Starvation Begin?')
    ax.legend()
    ax.grid(True, alpha=0.3)

    # Format x-axis
    ax.set_xscale('log', base=2)
    ax.xaxis.set_major_formatter(plt.FuncFormatter(lambda x, p: f'{int(x//1024)}K' if x >= 1024 else f'{int(x)}'))

    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"Saved threshold plot: {output_path}")


def generate_throughput_degradation_plot(results: list[dict], output_path: str):
    """
    Generate plot showing throughput vs oversubscription ratio.
    """
    if not HAS_MATPLOTLIB:
        print("Warning: matplotlib not available, skipping throughput plot")
        return

    successful = [r for r in results if r.get('success', False) and r.get('receiver_capacity_kbps', 0) > 0]

    if not successful:
        print("Warning: No data, skipping throughput plot")
        return

    fig, ax = plt.subplots(figsize=(10, 6))

    # Group by recv_buf
    by_buffer = {}
    for r in successful:
        buf = r['recv_buf']
        if buf not in by_buffer:
            by_buffer[buf] = {'oversub': [], 'efficiency': []}

        # Efficiency = actual throughput / receiver capacity
        efficiency = r['actual_throughput_kbps'] / r['receiver_capacity_kbps'] * 100 if r['receiver_capacity_kbps'] > 0 else 0
        by_buffer[buf]['oversub'].append(r['oversubscription_ratio'])
        by_buffer[buf]['efficiency'].append(efficiency)

    colors = plt.cm.viridis(np.linspace(0, 1, len(by_buffer))) if HAS_NUMPY else ['blue', 'green', 'red', 'orange', 'purple']

    for i, (buf, data) in enumerate(sorted(by_buffer.items())):
        color = colors[i] if HAS_NUMPY else colors[i % len(colors)]
        ax.scatter(data['oversub'], data['efficiency'], label=f'{buf//1024}K buffer',
                  color=color, alpha=0.7, s=50)

    ax.axhline(y=100, color='gray', linestyle='--', alpha=0.5, label='Ideal (100%)')
    ax.set_xlabel('Oversubscription Ratio (sender rate / receiver capacity)')
    ax.set_ylabel('Throughput Efficiency (%)')
    ax.set_title('Throughput Degradation Under Receiver Starvation')
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.set_ylim(0, 150)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"Saved throughput plot: {output_path}")


def generate_rtt_vs_zerowindow_plot(results: list[dict], output_path: str):
    """
    Generate scatter plot showing RTT vs zero-window events.
    Shows how network latency affects flow control behavior.
    """
    if not HAS_MATPLOTLIB:
        return

    successful = [r for r in results if r.get('success', False) and r.get('rtt_mean_us', 0) > 0]
    if not successful:
        print("Warning: No RTT data, skipping RTT vs zero-window plot")
        return

    fig, ax = plt.subplots(figsize=(10, 6))

    # Color by net_delay_ms if available
    net_delays = sorted(set(r.get('net_delay_ms', 0) for r in successful))
    has_net_delay = len(net_delays) > 1

    if has_net_delay and HAS_NUMPY:
        colors = plt.cm.plasma(np.linspace(0, 1, len(net_delays)))
        for i, nd in enumerate(net_delays):
            subset = [r for r in successful if r.get('net_delay_ms', 0) == nd]
            rtts = [r['rtt_mean_us'] / 1000 for r in subset]  # Convert to ms
            zw = [r['zero_window_count'] for r in subset]
            ax.scatter(rtts, zw, color=colors[i], alpha=0.7, s=60,
                      label=f'{nd}ms injected delay', edgecolors='white', linewidth=0.5)
    else:
        rtts = [r['rtt_mean_us'] / 1000 for r in successful]
        zw = [r['zero_window_count'] for r in successful]
        ax.scatter(rtts, zw, alpha=0.7, s=60, c='steelblue', edgecolors='white', linewidth=0.5)

    ax.set_xlabel('Mean RTT (ms)')
    ax.set_ylabel('Zero Window Events')
    ax.set_title('Network Latency vs Flow Control Events')
    if has_net_delay:
        ax.legend(title='Network Condition')
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"Saved RTT vs zero-window plot: {output_path}")


def generate_network_delay_impact_plot(results: list[dict], output_path: str):
    """
    Generate line plot showing how network delay affects zero-window events and throughput.
    """
    if not HAS_MATPLOTLIB:
        return

    successful = [r for r in results if r.get('success', False)]
    net_delays = sorted(set(r.get('net_delay_ms', 0) for r in successful))

    if len(net_delays) <= 1:
        print("Warning: No network delay variation, skipping network delay impact plot")
        return

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))

    # Group by send_rate
    send_rates = sorted(set(r['send_rate_mbps'] for r in successful))
    colors = plt.cm.viridis(np.linspace(0, 1, len(send_rates))) if HAS_NUMPY else ['blue', 'green', 'red', 'orange']

    for i, rate in enumerate(send_rates):
        subset = [r for r in successful if r['send_rate_mbps'] == rate]
        by_delay = {}
        for r in subset:
            nd = r.get('net_delay_ms', 0)
            if nd not in by_delay:
                by_delay[nd] = {'zw': [], 'throughput': [], 'rtt': []}
            by_delay[nd]['zw'].append(r['zero_window_count'])
            by_delay[nd]['throughput'].append(r['actual_throughput_kbps'])
            by_delay[nd]['rtt'].append(r.get('rtt_mean_us', 0) / 1000)

        delays = sorted(by_delay.keys())
        avg_zw = [sum(by_delay[d]['zw']) / len(by_delay[d]['zw']) for d in delays]
        avg_tp = [sum(by_delay[d]['throughput']) / len(by_delay[d]['throughput']) for d in delays]

        color = colors[i] if HAS_NUMPY else colors[i % len(colors)]
        ax1.plot(delays, avg_zw, 'o-', color=color, label=f'{rate} MB/s', markersize=6)
        ax2.plot(delays, avg_tp, 'o-', color=color, label=f'{rate} MB/s', markersize=6)

    ax1.set_xlabel('Network Delay (ms)')
    ax1.set_ylabel('Zero Window Events')
    ax1.set_title('Zero-Window Events vs Network Latency')
    ax1.legend(title='Send Rate')
    ax1.grid(True, alpha=0.3)

    ax2.set_xlabel('Network Delay (ms)')
    ax2.set_ylabel('Throughput (KB/s)')
    ax2.set_title('Throughput vs Network Latency')
    ax2.legend(title='Send Rate')
    ax2.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"Saved network delay impact plot: {output_path}")


def generate_rtt_distribution_plot(results: list[dict], output_path: str):
    """
    Generate box plot or violin plot showing RTT distribution by network condition.
    """
    if not HAS_MATPLOTLIB:
        return

    successful = [r for r in results if r.get('success', False) and r.get('rtt_mean_us', 0) > 0]
    if not successful:
        return

    net_delays = sorted(set(r.get('net_delay_ms', 0) for r in successful))
    if len(net_delays) <= 1:
        print("Warning: No network delay variation, skipping RTT distribution plot")
        return

    fig, ax = plt.subplots(figsize=(12, 6))

    # Collect RTT data by network delay
    data = []
    labels = []
    for nd in net_delays:
        subset = [r for r in successful if r.get('net_delay_ms', 0) == nd]
        # Use min, p50, mean, p95, max for each experiment
        rtt_data = []
        for r in subset:
            rtt_data.extend([
                r.get('rtt_min_us', 0) / 1000,
                r.get('rtt_p50_us', 0) / 1000,
                r.get('rtt_mean_us', 0) / 1000,
                r.get('rtt_p95_us', 0) / 1000,
            ])
        if rtt_data:
            data.append([x for x in rtt_data if x > 0])
            labels.append(f'{nd}ms')

    if data:
        bp = ax.boxplot(data, labels=labels, patch_artist=True)
        colors = plt.cm.coolwarm(np.linspace(0, 1, len(data))) if HAS_NUMPY else ['lightblue'] * len(data)
        for patch, color in zip(bp['boxes'], colors):
            patch.set_facecolor(color)
            patch.set_alpha(0.7)

    ax.set_xlabel('Injected Network Delay')
    ax.set_ylabel('Measured RTT (ms)')
    ax.set_title('RTT Distribution by Network Condition')
    ax.grid(True, alpha=0.3, axis='y')

    # Add reference line for expected RTT (2x delay for bidirectional)
    if net_delays:
        expected = [2 * nd for nd in net_delays if nd > 0]
        if expected:
            ax.axhline(y=min(expected), color='gray', linestyle=':', alpha=0.5)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"Saved RTT distribution plot: {output_path}")


def generate_congestion_control_comparison(results: list[dict], output_path: str):
    """
    Generate comparison plot for different congestion control algorithms.
    """
    if not HAS_MATPLOTLIB:
        return

    successful = [r for r in results if r.get('success', False)]
    algos = sorted(set(r.get('congestion_algo', 'cubic') for r in successful))

    if len(algos) <= 1:
        print("Warning: Only one congestion control algorithm, skipping comparison plot")
        return

    fig, axes = plt.subplots(1, 3, figsize=(15, 5))

    # Colors for algorithms
    algo_colors = {'cubic': 'steelblue', 'reno': 'coral', 'bbr': 'seagreen'}

    # Metrics to compare
    metrics = [
        ('zero_window_count', 'Zero Window Events', axes[0]),
        ('retransmit_count', 'Retransmissions', axes[1]),
        ('actual_throughput_kbps', 'Throughput (KB/s)', axes[2]),
    ]

    for metric, label, ax in metrics:
        for algo in algos:
            subset = [r for r in successful if r.get('congestion_algo', 'cubic') == algo]
            values = [r.get(metric, 0) for r in subset]
            if values:
                color = algo_colors.get(algo, 'gray')
                ax.hist(values, bins=15, alpha=0.6, label=algo.upper(), color=color, edgecolor='white')

        ax.set_xlabel(label)
        ax.set_ylabel('Frequency')
        ax.set_title(f'{label} by Algorithm')
        ax.legend()
        ax.grid(True, alpha=0.3, axis='y')

    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"Saved congestion control comparison: {output_path}")


def generate_multi_dimension_heatmap(results: list[dict], output_path: str):
    """
    Generate heatmap with network delay on one axis and send rate on the other.
    Shows zero-window events colored by intensity.
    """
    if not HAS_MATPLOTLIB:
        return

    successful = [r for r in results if r.get('success', False)]
    net_delays = sorted(set(r.get('net_delay_ms', 0) for r in successful))
    send_rates = sorted(set(r['send_rate_mbps'] for r in successful))

    if len(net_delays) <= 1 or len(send_rates) <= 1:
        return

    # Build matrix
    matrix = np.zeros((len(send_rates), len(net_delays))) if HAS_NUMPY else [[0] * len(net_delays) for _ in range(len(send_rates))]

    for r in successful:
        nd = r.get('net_delay_ms', 0)
        sr = r['send_rate_mbps']
        if nd in net_delays and sr in send_rates:
            yi = send_rates.index(sr)
            xi = net_delays.index(nd)
            if HAS_NUMPY:
                matrix[yi, xi] = r['zero_window_count']
            else:
                matrix[yi][xi] = r['zero_window_count']

    fig, ax = plt.subplots(figsize=(12, 8))

    im = ax.imshow(matrix, cmap='RdYlGn_r', aspect='auto', origin='lower')

    ax.set_xticks(range(len(net_delays)))
    ax.set_yticks(range(len(send_rates)))
    ax.set_xticklabels([f'{nd}ms' for nd in net_delays])
    ax.set_yticklabels([f'{sr} MB/s' for sr in send_rates])

    ax.set_xlabel('Network Delay (injected)')
    ax.set_ylabel('Send Rate')
    ax.set_title('Zero-Window Events: Network Delay vs Send Rate\n(Green = fewer events, Red = more events)')

    cbar = plt.colorbar(im)
    cbar.set_label('Zero Window Count')

    # Add annotations
    for yi in range(len(send_rates)):
        for xi in range(len(net_delays)):
            val = matrix[yi, xi] if HAS_NUMPY else matrix[yi][xi]
            color = 'white' if val > (matrix.max() if HAS_NUMPY else max(max(row) for row in matrix)) * 0.5 else 'black'
            ax.text(xi, yi, f'{int(val)}', ha='center', va='center', color=color, fontsize=9, fontweight='bold')

    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"Saved multi-dimension heatmap: {output_path}")


def generate_rtt_throughput_scatter(results: list[dict], output_path: str):
    """
    Generate scatter plot showing relationship between RTT and throughput.
    """
    if not HAS_MATPLOTLIB:
        return

    successful = [r for r in results if r.get('success', False) and r.get('rtt_mean_us', 0) > 0]
    if not successful:
        return

    fig, ax = plt.subplots(figsize=(10, 6))

    rtts = [r['rtt_mean_us'] / 1000 for r in successful]  # ms
    throughputs = [r['actual_throughput_kbps'] for r in successful]
    zw_counts = [r['zero_window_count'] for r in successful]

    # Size points by zero-window count
    sizes = [max(20, min(200, zw * 5 + 20)) for zw in zw_counts]

    scatter = ax.scatter(rtts, throughputs, c=zw_counts, cmap='RdYlGn_r',
                        s=sizes, alpha=0.7, edgecolors='white', linewidth=0.5)

    cbar = plt.colorbar(scatter)
    cbar.set_label('Zero Window Events')

    ax.set_xlabel('Mean RTT (ms)')
    ax.set_ylabel('Throughput (KB/s)')
    ax.set_title('RTT vs Throughput\n(point size = zero-window events)')
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    plt.close()
    print(f"Saved RTT-throughput scatter: {output_path}")


def export_json(results: list[dict], stats: dict, thresholds: dict, output_path: str):
    """Export analysis results as JSON for further processing."""
    export = {
        'statistics': stats,
        'thresholds': {f"{k[0]}_{k[1]}": v for k, v in thresholds.items()},
        'raw_results': results,
    }

    with open(output_path, 'w') as f:
        json.dump(export, f, indent=2, default=str)

    print(f"Saved JSON export: {output_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Analyze TCP Flow Control sweep results",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    parser.add_argument('input', nargs='+', help='Input CSV file(s) from sweep.py (supports multiple files or glob patterns)')
    parser.add_argument('--output-dir', '-o', default='.',
                        help='Output directory for plots and reports')
    parser.add_argument('--no-plots', action='store_true',
                        help='Skip plot generation (text report only)')
    parser.add_argument('--json', action='store_true',
                        help='Export analysis as JSON')

    args = parser.parse_args()

    # Load data - support multiple input files
    if len(args.input) == 1:
        print(f"Loading results from {args.input[0]}...")
        results = load_results(args.input[0])
        print(f"Loaded {len(results)} experiment results")
    else:
        print(f"Loading results from {len(args.input)} files...")
        results = load_multiple_results(args.input)

    # Create output directory
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    # Generate text report
    report = generate_text_report(results)
    print("\n" + report)

    report_path = output_dir / 'analysis_report.txt'
    with open(report_path, 'w') as f:
        f.write(report)
    print(f"\nSaved report: {report_path}")

    # Generate plots
    if not args.no_plots and HAS_MATPLOTLIB:
        print("\nGenerating visualizations...")

        # Basic heatmap: zero-window count by buffer and delay
        generate_heatmap(
            results, str(output_dir / 'heatmap_zerowindow.png'),
            x_param='recv_buf', y_param='delay_ms',
            value_param='zero_window_count'
        )

        # Threshold plot
        generate_threshold_plot(results, str(output_dir / 'threshold_plot.png'))

        # Throughput degradation
        generate_throughput_degradation_plot(results, str(output_dir / 'throughput_degradation.png'))

        # NEW: RTT-related visualizations
        generate_rtt_vs_zerowindow_plot(results, str(output_dir / 'rtt_vs_zerowindow.png'))
        generate_rtt_throughput_scatter(results, str(output_dir / 'rtt_throughput_scatter.png'))
        generate_rtt_distribution_plot(results, str(output_dir / 'rtt_distribution.png'))

        # NEW: Network delay impact
        generate_network_delay_impact_plot(results, str(output_dir / 'network_delay_impact.png'))
        generate_multi_dimension_heatmap(results, str(output_dir / 'heatmap_netdelay_sendrate.png'))

        # NEW: Congestion control comparison
        generate_congestion_control_comparison(results, str(output_dir / 'congestion_control_comparison.png'))

        # Heatmap for different send rates
        send_rates = sorted(set(r['send_rate_mbps'] for r in results if r.get('success')))
        for rate in send_rates[:3]:  # Top 3 rates
            generate_heatmap(
                results, str(output_dir / f'heatmap_rate_{rate:.2f}.png'),
                x_param='recv_buf', y_param='delay_ms',
                value_param='zero_window_count',
                fixed_params={'send_rate_mbps': rate}
            )

    elif not HAS_MATPLOTLIB:
        print("\nNote: Install matplotlib for visualizations: pip install matplotlib")

    # JSON export
    if args.json:
        stats = compute_statistics(results)
        thresholds = find_starvation_thresholds(results)
        export_json(results, stats, thresholds, str(output_dir / 'analysis.json'))

    print("\nAnalysis complete!")
    return 0


if __name__ == '__main__':
    sys.exit(main())
