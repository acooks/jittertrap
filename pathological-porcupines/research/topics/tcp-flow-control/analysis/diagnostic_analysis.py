#!/usr/bin/env python3
"""
Diagnostic Analysis for TCP Video Streaming

This script analyzes experiment results to validate the diagnostic decision tree:
"Why is my video stuttering - is it the network or the receiver?"

Key diagnostic signatures:
- Receiver Problem: Many zero-window events, few retransmits, low RTT
- Network Problem: Few zero-window events, many retransmits, high RTT
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path
import argparse
import sys

# Thresholds for classification (can be calibrated)
ZERO_WINDOW_THRESHOLD = 5  # More than this = likely receiver problem
RETRANSMIT_RATE_THRESHOLD = 10  # More than this per 10s = likely network problem
RTT_BASELINE_THRESHOLD_US = 50000  # Above 50ms baseline = network problem indicator


def load_diagnostic_data(data_dir: Path) -> dict:
    """Load all diagnostic experiment CSVs."""
    datasets = {}
    for csv_file in data_dir.glob('*.csv'):
        name = csv_file.stem
        # Skip our own output files
        if name.startswith('diagnostic_'):
            continue
        try:
            df = pd.read_csv(csv_file)
            datasets[name] = df
            print(f"Loaded {name}: {len(df)} experiments")
        except Exception as e:
            print(f"Warning: Could not load {csv_file}: {e}")
    return datasets


def classify_experiment(row) -> str:
    """Classify a single experiment as receiver, network, or compound problem."""
    has_zero_window = row['zero_window_count'] > ZERO_WINDOW_THRESHOLD
    has_retransmits = row['retransmit_count'] > RETRANSMIT_RATE_THRESHOLD

    if has_zero_window and not has_retransmits:
        return 'receiver'
    elif has_retransmits and not has_zero_window:
        return 'network'
    elif has_zero_window and has_retransmits:
        return 'compound'
    else:
        return 'healthy'


def analyze_signatures(datasets: dict) -> pd.DataFrame:
    """Analyze diagnostic signatures across all datasets."""
    results = []

    for name, df in datasets.items():
        if len(df) == 0:
            continue

        # Determine ground truth from preset name
        if 'receiver' in name.lower():
            ground_truth = 'receiver'
        elif 'network' in name.lower():
            ground_truth = 'network'
        elif 'compound' in name.lower():
            ground_truth = 'compound'
        else:
            ground_truth = 'unknown'

        # Classify each experiment
        df['predicted'] = df.apply(classify_experiment, axis=1)
        df['ground_truth'] = ground_truth
        df['correct'] = df['predicted'] == df['ground_truth']
        df['_source'] = name

        results.append(df)

    if not results:
        return pd.DataFrame()

    return pd.concat(results, ignore_index=True)


def compute_accuracy(df: pd.DataFrame) -> dict:
    """Compute classification accuracy by ground truth category."""
    accuracy = {}

    for truth in df['ground_truth'].unique():
        subset = df[df['ground_truth'] == truth]
        if len(subset) > 0:
            correct = (subset['predicted'] == truth).sum()
            accuracy[truth] = {
                'total': len(subset),
                'correct': correct,
                'accuracy': correct / len(subset) * 100,
                'predictions': subset['predicted'].value_counts().to_dict()
            }

    return accuracy


def plot_signature_discrimination(df: pd.DataFrame, output_dir: Path):
    """Plot zero-window vs retransmit to show signature discrimination."""
    from matplotlib.patches import Rectangle

    colors = {
        'receiver': '#2563eb',   # Blue
        'network': '#dc2626',    # Red
        'compound': '#9333ea',   # Purple
        'unknown': '#6b7280'     # Gray
    }
    markers = {
        'receiver': 'o',
        'network': 's',
        'compound': '^',
        'unknown': 'x'
    }

    # Helper function to add jitter for better visibility (clamped to non-negative)
    def add_jitter(data, scale=0.3):
        jittered = data + np.random.normal(0, scale, size=len(data))
        return np.maximum(jittered, 0)  # Clamp to non-negative

    # Calculate quadrant counts for annotations
    def get_quadrant_counts(data):
        healthy = data[(data['zero_window_count'] <= ZERO_WINDOW_THRESHOLD) &
                       (data['retransmit_count'] <= RETRANSMIT_RATE_THRESHOLD)]
        receiver_q = data[(data['zero_window_count'] > ZERO_WINDOW_THRESHOLD) &
                          (data['retransmit_count'] <= RETRANSMIT_RATE_THRESHOLD)]
        network_q = data[(data['zero_window_count'] <= ZERO_WINDOW_THRESHOLD) &
                         (data['retransmit_count'] > RETRANSMIT_RATE_THRESHOLD)]
        compound_q = data[(data['zero_window_count'] > ZERO_WINDOW_THRESHOLD) &
                          (data['retransmit_count'] > RETRANSMIT_RATE_THRESHOLD)]
        return {
            'healthy': len(healthy),
            'receiver': len(receiver_q),
            'network': len(network_q),
            'compound': len(compound_q)
        }

    # ===== PLOT 1: Threshold Zone Detail (new focused view) =====
    fig1, ax1 = plt.subplots(figsize=(10, 8))

    # Focus on the critical decision boundary region
    zone_x_max = 30
    zone_y_max = 50

    for truth in ['receiver', 'network', 'compound']:
        if truth not in df['ground_truth'].values:
            continue
        subset = df[df['ground_truth'] == truth]
        # Filter to zone
        in_zone = subset[(subset['zero_window_count'] <= zone_x_max) &
                         (subset['retransmit_count'] <= zone_y_max)]
        if len(in_zone) > 0:
            # Add jitter to separate overlapping points
            x_jitter = add_jitter(in_zone['zero_window_count'].values, scale=0.5)
            y_jitter = add_jitter(in_zone['retransmit_count'].values, scale=0.8)
            ax1.scatter(x_jitter, y_jitter,
                       c=colors.get(truth, 'gray'),
                       marker=markers.get(truth, 'o'),
                       label=f'{truth.title()} (n={len(in_zone)} in zone)',
                       alpha=0.7, s=60, edgecolors='white', linewidths=0.5)

    # Add threshold lines
    ax1.axvline(x=ZERO_WINDOW_THRESHOLD, color='#2563eb', linestyle='--', linewidth=2.5, alpha=0.9)
    ax1.axhline(y=RETRANSMIT_RATE_THRESHOLD, color='#dc2626', linestyle='--', linewidth=2.5, alpha=0.9)

    # Shade quadrants with light colors
    ax1.fill_between([0, ZERO_WINDOW_THRESHOLD], 0, RETRANSMIT_RATE_THRESHOLD,
                     color='#059669', alpha=0.08, label='_nolegend_')  # Healthy
    ax1.fill_between([ZERO_WINDOW_THRESHOLD, zone_x_max], 0, RETRANSMIT_RATE_THRESHOLD,
                     color='#2563eb', alpha=0.08, label='_nolegend_')  # Receiver
    ax1.fill_between([0, ZERO_WINDOW_THRESHOLD], RETRANSMIT_RATE_THRESHOLD, zone_y_max,
                     color='#dc2626', alpha=0.08, label='_nolegend_')  # Network
    ax1.fill_between([ZERO_WINDOW_THRESHOLD, zone_x_max], RETRANSMIT_RATE_THRESHOLD, zone_y_max,
                     color='#9333ea', alpha=0.08, label='_nolegend_')  # Compound

    # Quadrant labels with counts - these show PREDICTED classification, not ground truth
    zone_data = df[(df['zero_window_count'] <= zone_x_max) & (df['retransmit_count'] <= zone_y_max)]
    q_counts = get_quadrant_counts(zone_data)

    ax1.text(ZERO_WINDOW_THRESHOLD/2, RETRANSMIT_RATE_THRESHOLD/2,
             f'Predicted:\nHEALTHY\n(n={q_counts["healthy"]})', fontsize=11, color='#059669',
             ha='center', va='center', fontweight='bold',
             bbox=dict(boxstyle='round,pad=0.4', facecolor='white', alpha=0.9, edgecolor='#059669', linewidth=2))
    ax1.text((ZERO_WINDOW_THRESHOLD + zone_x_max)/2, RETRANSMIT_RATE_THRESHOLD/2,
             f'Predicted:\nRECEIVER\n(n={q_counts["receiver"]})', fontsize=11, color='#2563eb',
             ha='center', va='center', fontweight='bold',
             bbox=dict(boxstyle='round,pad=0.4', facecolor='white', alpha=0.9, edgecolor='#2563eb', linewidth=2))
    ax1.text(ZERO_WINDOW_THRESHOLD/2, (RETRANSMIT_RATE_THRESHOLD + zone_y_max)/2,
             f'Predicted:\nNETWORK\n(n={q_counts["network"]})', fontsize=11, color='#dc2626',
             ha='center', va='center', fontweight='bold',
             bbox=dict(boxstyle='round,pad=0.4', facecolor='white', alpha=0.9, edgecolor='#dc2626', linewidth=2))
    ax1.text((ZERO_WINDOW_THRESHOLD + zone_x_max)/2, (RETRANSMIT_RATE_THRESHOLD + zone_y_max)/2,
             f'Predicted:\nCOMPOUND\n(n={q_counts["compound"]})', fontsize=11, color='#9333ea',
             ha='center', va='center', fontweight='bold',
             bbox=dict(boxstyle='round,pad=0.4', facecolor='white', alpha=0.9, edgecolor='#9333ea', linewidth=2))

    ax1.set_xlim(0, zone_x_max)
    ax1.set_ylim(0, zone_y_max)
    ax1.set_xlabel('Zero-Window Events', fontsize=13)
    ax1.set_ylabel('Retransmit Count', fontsize=13)
    ax1.set_title('Threshold Zone Detail\n(Decision Boundary Region)', fontsize=14, fontweight='bold')
    ax1.legend(loc='upper right', fontsize=10)
    ax1.grid(True, alpha=0.3)

    # Add threshold value annotations
    ax1.annotate(f'ZW = {ZERO_WINDOW_THRESHOLD}', xy=(ZERO_WINDOW_THRESHOLD, zone_y_max),
                fontsize=11, color='#2563eb', ha='left', va='top', fontweight='bold',
                xytext=(ZERO_WINDOW_THRESHOLD + 1, zone_y_max - 2))
    ax1.annotate(f'RT = {RETRANSMIT_RATE_THRESHOLD}', xy=(zone_x_max, RETRANSMIT_RATE_THRESHOLD),
                fontsize=11, color='#dc2626', ha='right', va='bottom', fontweight='bold',
                xytext=(zone_x_max - 1, RETRANSMIT_RATE_THRESHOLD + 1))

    plt.tight_layout()
    plt.savefig(output_dir / 'diagnostic_threshold_zone.png', dpi=150, bbox_inches='tight')
    print(f"Saved: diagnostic_threshold_zone.png")
    plt.close()

    # ===== PLOT 2: Log-scale overview (better data spread) =====
    fig2, ax2 = plt.subplots(figsize=(10, 8))

    for truth in ['receiver', 'network', 'compound']:
        if truth not in df['ground_truth'].values:
            continue
        subset = df[df['ground_truth'] == truth]
        # Add small offset to zeros for log scale, plus jitter
        x_data = subset['zero_window_count'].values + 0.5
        y_data = subset['retransmit_count'].values + 0.5
        x_jitter = x_data * np.exp(np.random.normal(0, 0.05, size=len(x_data)))
        y_jitter = y_data * np.exp(np.random.normal(0, 0.05, size=len(y_data)))

        ax2.scatter(x_jitter, y_jitter,
                   c=colors.get(truth, 'gray'),
                   marker=markers.get(truth, 'o'),
                   label=f'{truth.title()} (n={len(subset)})',
                   alpha=0.6, s=50, edgecolors='white', linewidths=0.5)

    # Add threshold lines (offset by 0.5 to match data)
    ax2.axvline(x=ZERO_WINDOW_THRESHOLD + 0.5, color='#2563eb', linestyle='--', linewidth=2, alpha=0.8)
    ax2.axhline(y=RETRANSMIT_RATE_THRESHOLD + 0.5, color='#dc2626', linestyle='--', linewidth=2, alpha=0.8)

    ax2.set_xscale('log')
    ax2.set_yscale('log')
    ax2.set_xlabel('Zero-Window Events (+0.5 offset for log scale)', fontsize=12)
    ax2.set_ylabel('Retransmit Count (+0.5 offset for log scale)', fontsize=12)
    ax2.set_title('Log-Scale Overview\n(Shows full data spread)', fontsize=14, fontweight='bold')
    ax2.legend(loc='upper left', fontsize=10)
    ax2.grid(True, alpha=0.3, which='both')

    # Add quadrant labels - these show PREDICTED classification based on thresholds
    q_counts = get_quadrant_counts(df)
    ax2.text(1.5, 3, f'Predicted:\nHEALTHY\n(n={q_counts["healthy"]})', fontsize=9, color='#059669',
             ha='center', va='center', fontweight='bold',
             bbox=dict(boxstyle='round,pad=0.3', facecolor='white', alpha=0.9, edgecolor='#059669'))
    ax2.text(50, 3, f'Predicted:\nRECEIVER\n(n={q_counts["receiver"]})', fontsize=9, color='#2563eb',
             ha='center', va='center', fontweight='bold',
             bbox=dict(boxstyle='round,pad=0.3', facecolor='white', alpha=0.9, edgecolor='#2563eb'))
    ax2.text(1.5, 100, f'Predicted:\nNETWORK\n(n={q_counts["network"]})', fontsize=9, color='#dc2626',
             ha='center', va='center', fontweight='bold',
             bbox=dict(boxstyle='round,pad=0.3', facecolor='white', alpha=0.9, edgecolor='#dc2626'))
    ax2.text(50, 100, f'Predicted:\nCOMPOUND\n(n={q_counts["compound"]})', fontsize=9, color='#9333ea',
             ha='center', va='center', fontweight='bold',
             bbox=dict(boxstyle='round,pad=0.3', facecolor='white', alpha=0.9, edgecolor='#9333ea'))

    plt.tight_layout()
    plt.savefig(output_dir / 'diagnostic_logscale.png', dpi=150, bbox_inches='tight')
    print(f"Saved: diagnostic_logscale.png")
    plt.close()

    # ===== PLOT 3: Original two-panel view (refined) =====
    fig3, (ax3, ax4) = plt.subplots(1, 2, figsize=(16, 7))

    # Plot on both axes with jitter
    for ax in [ax3, ax4]:
        for truth in ['receiver', 'network', 'compound']:
            if truth not in df['ground_truth'].values:
                continue
            subset = df[df['ground_truth'] == truth]
            # Add jitter
            x_jitter = add_jitter(subset['zero_window_count'].values, scale=1.5)
            y_jitter = add_jitter(subset['retransmit_count'].values, scale=2.0)
            ax.scatter(x_jitter, y_jitter,
                      c=colors.get(truth, 'gray'),
                      marker=markers.get(truth, 'o'),
                      label=f'{truth.title()} (n={len(subset)})',
                      alpha=0.6, s=40, edgecolors='white', linewidths=0.5)

        ax.axvline(x=ZERO_WINDOW_THRESHOLD, color='#2563eb', linestyle='--', linewidth=2, alpha=0.8)
        ax.axhline(y=RETRANSMIT_RATE_THRESHOLD, color='#dc2626', linestyle='--', linewidth=2, alpha=0.8)
        ax.grid(True, alpha=0.3)

    # Left plot: Zoomed view
    zoom_x_max = 120
    zoom_y_max = 200
    ax3.set_xlim(0, zoom_x_max)
    ax3.set_ylim(0, zoom_y_max)
    ax3.set_xlabel('Zero-Window Events', fontsize=12)
    ax3.set_ylabel('Retransmit Count', fontsize=12)
    ax3.set_title('Zoomed View (Near Thresholds)', fontsize=14, fontweight='bold')

    # Quadrant labels
    ax3.text(60, 4, 'RECEIVER', fontsize=11, color='#2563eb',
             ha='center', va='center', fontweight='bold',
             bbox=dict(boxstyle='round,pad=0.3', facecolor='white', alpha=0.8, edgecolor='#2563eb'))
    ax3.text(2, 100, 'NETWORK', fontsize=11, color='#dc2626',
             ha='left', va='center', fontweight='bold',
             bbox=dict(boxstyle='round,pad=0.3', facecolor='white', alpha=0.8, edgecolor='#dc2626'))
    ax3.text(60, 100, 'COMPOUND', fontsize=11, color='#9333ea',
             ha='center', va='center', fontweight='bold',
             bbox=dict(boxstyle='round,pad=0.3', facecolor='white', alpha=0.8, edgecolor='#9333ea'))
    ax3.text(2, 4, 'HEALTHY', fontsize=11, color='#059669',
             ha='left', va='center', fontweight='bold',
             bbox=dict(boxstyle='round,pad=0.3', facecolor='white', alpha=0.8, edgecolor='#059669'))

    ax3.legend(loc='upper right', fontsize=10)

    # Right plot: Full scale view
    x_max = df['zero_window_count'].max() * 1.05
    y_max = df['retransmit_count'].max() * 1.05
    ax4.set_xlim(0, x_max)
    ax4.set_ylim(0, y_max)
    ax4.set_xlabel('Zero-Window Events', fontsize=12)
    ax4.set_ylabel('Retransmit Count', fontsize=12)
    ax4.set_title('Full Scale View', fontsize=14, fontweight='bold')
    ax4.legend(loc='upper right', fontsize=10)

    # Add a rectangle showing the zoomed region
    rect = Rectangle((0, 0), zoom_x_max, zoom_y_max, linewidth=2, edgecolor='#059669',
                      facecolor='#059669', alpha=0.1, linestyle='-')
    ax4.add_patch(rect)
    ax4.annotate('Zoomed\nregion', xy=(zoom_x_max, zoom_y_max), xytext=(250, 350),
                 fontsize=10, color='#059669', fontweight='bold',
                 arrowprops=dict(arrowstyle='->', color='#059669', lw=1.5))

    plt.suptitle('Diagnostic Signature Discrimination:\nZero-Window (Receiver) vs Retransmits (Network)',
                 fontsize=16, fontweight='bold', y=1.02)
    plt.tight_layout()
    plt.savefig(output_dir / 'diagnostic_discrimination.png', dpi=150, bbox_inches='tight')
    print(f"Saved: diagnostic_discrimination.png")
    plt.close()


def plot_signature_distributions(df: pd.DataFrame, output_dir: Path):
    """Plot distribution of signatures by ground truth category using box plots."""
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))

    colors = {
        'receiver': '#2563eb',
        'network': '#dc2626',
        'compound': '#9333ea'
    }

    # Prepare data for box plots
    categories = ['receiver', 'network', 'compound']
    present_categories = [c for c in categories if c in df['ground_truth'].values]

    metrics = [
        ('zero_window_count', 'Zero-Window Events', ZERO_WINDOW_THRESHOLD),
        ('retransmit_count', 'Retransmit Count', RETRANSMIT_RATE_THRESHOLD),
        ('rtt_mean_us', 'Mean RTT (ms)', None),  # Will convert to ms
        ('window_full_count', 'Window Full Events', None)
    ]

    for ax, (metric, label, threshold) in zip(axes.flat, metrics):
        box_data = []
        box_labels = []
        box_colors = []

        for truth in present_categories:
            subset = df[df['ground_truth'] == truth]
            if len(subset) > 0 and metric in subset.columns:
                data = subset[metric].values
                # Convert RTT from microseconds to milliseconds
                if metric == 'rtt_mean_us':
                    data = data / 1000.0
                box_data.append(data)
                box_labels.append(f'{truth.title()}\n(n={len(subset)})')
                box_colors.append(colors[truth])

        if box_data:
            bp = ax.boxplot(box_data, tick_labels=box_labels, patch_artist=True,
                           medianprops=dict(color='black', linewidth=2))

            # Color the boxes
            for patch, color in zip(bp['boxes'], box_colors):
                patch.set_facecolor(color)
                patch.set_alpha(0.6)

            # Add individual points with jitter for better visualization
            for i, (data, color) in enumerate(zip(box_data, box_colors)):
                # Add jittered points
                x = np.random.normal(i + 1, 0.04, size=len(data))
                ax.scatter(x, data, alpha=0.3, color=color, s=10, zorder=2)

            # Add threshold line if applicable
            if threshold is not None:
                ax.axhline(y=threshold, color='#374151', linestyle='--',
                          linewidth=2, alpha=0.8)
                # Add threshold label on the line
                ax.annotate(f'Threshold = {threshold}', xy=(0.98, threshold),
                           xycoords=('axes fraction', 'data'),
                           fontsize=10, color='#374151', ha='right', va='bottom',
                           bbox=dict(boxstyle='round,pad=0.2', facecolor='white', alpha=0.9))

        ax.set_ylabel(label, fontsize=12)
        ax.set_title(f'{label} by Problem Type', fontsize=12, fontweight='bold')
        ax.grid(True, axis='y', alpha=0.3)

        # Ensure y-axis handles data appropriately
        all_data = np.concatenate(box_data) if box_data else np.array([0])
        data_max = all_data.max()
        data_min = all_data[all_data > 0].min() if np.any(all_data > 0) else 0
        has_zeros = np.any(all_data == 0)

        if metric in ['zero_window_count', 'retransmit_count', 'window_full_count']:
            # For count data with large range and no zeros, use log scale
            # If there are zeros, use symlog which handles 0 properly
            if data_max > 100:
                if has_zeros:
                    # symlog handles zeros well - linear near zero, log further out
                    ax.set_yscale('symlog', linthresh=1)
                    ax.set_ylim(bottom=0, top=data_max * 1.5)
                else:
                    ax.set_yscale('log')
                    ax.set_ylim(bottom=max(0.5, data_min * 0.5), top=data_max * 1.5)
                ax.grid(True, axis='y', which='minor', alpha=0.2)
            else:
                ax.set_ylim(bottom=0, top=data_max * 1.1 if data_max > 0 else 10)
        elif metric == 'rtt_mean_us':
            ax.set_ylim(bottom=0, top=data_max * 1.1 if data_max > 0 else 100)

    plt.suptitle('Diagnostic Metric Distributions by Problem Type',
                 fontsize=16, fontweight='bold')
    plt.tight_layout()
    plt.savefig(output_dir / 'diagnostic_distributions.png', dpi=150, bbox_inches='tight')
    print(f"Saved: diagnostic_distributions.png")
    plt.close()


def plot_confusion_matrix(df: pd.DataFrame, output_dir: Path):
    """Plot confusion matrix for classification with percentages."""
    # Only include categories that have ground truth data
    all_categories = ['receiver', 'network', 'compound', 'healthy']
    ground_truth_categories = [c for c in all_categories if c in df['ground_truth'].values]
    predicted_categories = [c for c in all_categories if c in df['predicted'].values]

    # Use ground truth categories for rows, predicted for columns
    row_categories = ground_truth_categories
    col_categories = predicted_categories

    if len(row_categories) < 1 or len(col_categories) < 1:
        print("Not enough categories for confusion matrix")
        return

    # Build confusion matrix
    matrix = np.zeros((len(row_categories), len(col_categories)))
    for i, truth in enumerate(row_categories):
        for j, pred in enumerate(col_categories):
            matrix[i, j] = ((df['ground_truth'] == truth) & (df['predicted'] == pred)).sum()

    # Calculate row totals for percentages
    row_totals = matrix.sum(axis=1, keepdims=True)
    row_totals[row_totals == 0] = 1  # Avoid division by zero

    # Create figure with two subplots: counts and percentages
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))

    # Left: Raw counts
    im1 = ax1.imshow(matrix, cmap='Blues')
    ax1.set_xticks(range(len(col_categories)))
    ax1.set_yticks(range(len(row_categories)))
    ax1.set_xticklabels([c.title() for c in col_categories], fontsize=11)
    ax1.set_yticklabels([c.title() for c in row_categories], fontsize=11)

    # Add count values with styling
    for i in range(len(row_categories)):
        for j in range(len(col_categories)):
            val = int(matrix[i, j])
            # Highlight diagonal (correct predictions)
            is_diagonal = row_categories[i] == col_categories[j]
            fontweight = 'bold' if is_diagonal else 'normal'
            color = 'white' if matrix[i, j] > matrix.max()/2 else 'black'
            ax1.text(j, i, str(val), ha='center', va='center',
                    color=color, fontsize=14, fontweight=fontweight)

    ax1.set_xlabel('Predicted', fontsize=12)
    ax1.set_ylabel('Ground Truth', fontsize=12)
    ax1.set_title('Raw Counts', fontsize=14, fontweight='bold')
    plt.colorbar(im1, ax=ax1, shrink=0.8)

    # Right: Row-normalized percentages
    matrix_pct = (matrix / row_totals) * 100
    im2 = ax2.imshow(matrix_pct, cmap='Blues', vmin=0, vmax=100)
    ax2.set_xticks(range(len(col_categories)))
    ax2.set_yticks(range(len(row_categories)))
    ax2.set_xticklabels([c.title() for c in col_categories], fontsize=11)
    ax2.set_yticklabels([c.title() for c in row_categories], fontsize=11)

    # Add percentage values
    for i in range(len(row_categories)):
        for j in range(len(col_categories)):
            pct = matrix_pct[i, j]
            is_diagonal = row_categories[i] == col_categories[j]
            fontweight = 'bold' if is_diagonal else 'normal'
            color = 'white' if pct > 50 else 'black'
            ax2.text(j, i, f'{pct:.0f}%', ha='center', va='center',
                    color=color, fontsize=14, fontweight=fontweight)

    ax2.set_xlabel('Predicted', fontsize=12)
    ax2.set_ylabel('Ground Truth', fontsize=12)
    ax2.set_title('Row-Normalized (%)', fontsize=14, fontweight='bold')
    cbar2 = plt.colorbar(im2, ax=ax2, shrink=0.8)
    cbar2.set_label('Percentage', fontsize=10)

    # Add row totals annotation
    for i, truth in enumerate(row_categories):
        total = int(row_totals[i, 0])
        ax1.annotate(f'n={total}', xy=(len(col_categories) - 0.5, i),
                    xytext=(len(col_categories) + 0.1, i),
                    fontsize=10, va='center', color='gray')

    plt.suptitle('Diagnostic Classification Confusion Matrix',
                 fontsize=16, fontweight='bold')
    plt.tight_layout()
    plt.savefig(output_dir / 'diagnostic_confusion_matrix.png', dpi=150, bbox_inches='tight')
    print(f"Saved: diagnostic_confusion_matrix.png")
    plt.close()


def print_summary(df: pd.DataFrame, accuracy: dict):
    """Print summary of diagnostic analysis."""
    print("\n" + "="*60)
    print("DIAGNOSTIC ANALYSIS SUMMARY")
    print("="*60)

    print(f"\nTotal experiments analyzed: {len(df)}")

    print("\n--- Classification Accuracy ---")
    for truth, stats in accuracy.items():
        print(f"\n{truth.upper()} (ground truth):")
        print(f"  Total: {stats['total']}")
        print(f"  Correctly classified: {stats['correct']} ({stats['accuracy']:.1f}%)")
        print(f"  Predictions breakdown: {stats['predictions']}")

    # Overall accuracy
    total = sum(s['total'] for s in accuracy.values())
    correct = sum(s['correct'] for s in accuracy.values())
    if total > 0:
        print(f"\nOVERALL ACCURACY: {correct}/{total} = {correct/total*100:.1f}%")

    print("\n--- Signature Statistics ---")
    for truth in df['ground_truth'].unique():
        subset = df[df['ground_truth'] == truth]
        print(f"\n{truth.upper()}:")
        print(f"  Zero-window: mean={subset['zero_window_count'].mean():.1f}, "
              f"median={subset['zero_window_count'].median():.1f}")
        print(f"  Retransmits: mean={subset['retransmit_count'].mean():.1f}, "
              f"median={subset['retransmit_count'].median():.1f}")
        if 'rtt_mean_us' in subset.columns:
            print(f"  RTT mean: {subset['rtt_mean_us'].mean()/1000:.1f}ms")
        if 'window_full_count' in subset.columns:
            print(f"  Window full: mean={subset['window_full_count'].mean():.1f}")


def main():
    parser = argparse.ArgumentParser(description='Diagnostic Analysis for TCP experiments')
    parser.add_argument('data_dir', type=Path, help='Directory containing diagnostic CSV files')
    parser.add_argument('-o', '--output', type=Path, default=None,
                       help='Output directory for plots (default: same as data_dir)')
    args = parser.parse_args()

    if not args.data_dir.exists():
        print(f"Error: Directory {args.data_dir} does not exist")
        sys.exit(1)

    output_dir = args.output or args.data_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    # Load data
    print("Loading diagnostic experiment data...")
    datasets = load_diagnostic_data(args.data_dir)

    if not datasets:
        print("No CSV files found in", args.data_dir)
        sys.exit(1)

    # Analyze signatures
    print("\nAnalyzing diagnostic signatures...")
    df = analyze_signatures(datasets)

    if len(df) == 0:
        print("No valid data to analyze")
        sys.exit(1)

    # Compute accuracy
    accuracy = compute_accuracy(df)

    # Generate plots
    print("\nGenerating diagnostic plots...")
    plot_signature_discrimination(df, output_dir)
    plot_signature_distributions(df, output_dir)
    plot_confusion_matrix(df, output_dir)

    # Print summary
    print_summary(df, accuracy)

    # Save combined data
    combined_csv = output_dir / 'diagnostic_combined.csv'
    df.to_csv(combined_csv, index=False)
    print(f"\nSaved combined data to: {combined_csv}")


if __name__ == '__main__':
    main()
