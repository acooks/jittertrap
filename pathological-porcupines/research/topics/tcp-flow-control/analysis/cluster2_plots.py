#!/usr/bin/env python3
"""Generate plots for Cluster 2 (WAN/Network Impairments) results."""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

# Load all cluster2 data
script_dir = Path(__file__).parent
experiment_dir = script_dir.parent
data_dir = experiment_dir / 'results' / 'cluster2'
output_dir = data_dir / 'plots'
output_dir.mkdir(exist_ok=True)

def extract_preset_name(filename_stem):
    """Extract preset name from filename, stripping timestamp if present.

    Handles both formats:
    - Old: 'starlink-excellent_20251223_150858' -> 'starlink-excellent'
    - New: 'starlink-excellent' -> 'starlink-excellent'
    """
    # Try to strip timestamp pattern _YYYYMMDD_HHMMSS from the end
    import re
    match = re.match(r'^(.+?)_\d{8}_\d{6}$', filename_stem)
    if match:
        return match.group(1)
    return filename_stem

dfs = []
for csv in data_dir.glob('*.csv'):
    df = pd.read_csv(csv)
    df['_source'] = extract_preset_name(csv.stem)
    dfs.append(df)
df = pd.concat(dfs, ignore_index=True)

print(f"Loaded {len(df)} experiments")

plt.style.use('seaborn-v0_8-whitegrid')
plt.rcParams['figure.figsize'] = (12, 8)
plt.rcParams['font.size'] = 12

# =============================================================================
# Plot 3: Starlink Profile Comparison (fixed)
# =============================================================================
fig, ax = plt.subplots(figsize=(12, 7))

starlink_df = df[df['_source'].str.contains('starlink')]

profiles = [
    ('starlink-excellent', 'Excellent\n(25ms, 0.05% loss)', 'green'),
    ('starlink-normal', 'Normal\n(50ms, 0.3% loss)', 'blue'),
    ('starlink-degraded', 'Degraded\n(100ms, 1.5% loss)', 'orange'),
    ('starlink-severe', 'Severe\n(250ms, 3% loss)', 'red'),
]

throughputs = []
labels = []
colors = []

for profile, label, color in profiles:
    profile_df = starlink_df[starlink_df['_source'] == profile]
    if len(profile_df) > 0:
        throughputs.append(profile_df['actual_throughput_kbps'].values / 1024)
        labels.append(label)
        colors.append(color)

if throughputs:
    bp = ax.boxplot(throughputs, tick_labels=labels, patch_artist=True)
    for patch, color in zip(bp['boxes'], colors):
        patch.set_facecolor(color)
        patch.set_alpha(0.6)

ax.set_ylabel('Throughput (MB/s)', fontsize=14)
ax.set_title('Starlink Performance by Condition', fontsize=16, fontweight='bold')
ax.set_ylim(0, None)

plt.tight_layout()
plt.savefig(output_dir / 'plot3_starlink_profiles.png', dpi=150)
print(f"Saved: plot3_starlink_profiles.png")
plt.close()

# =============================================================================
# Plot 4: Buffer Size vs RTT (BDP relationship)
# =============================================================================
fig, ax = plt.subplots(figsize=(12, 7))

df['bdp_bytes'] = (df['net_delay_ms'] * 2 / 1000) * (df['send_rate_mbps'] * 1024 * 1024)
df['buf_to_bdp_ratio'] = df['recv_buf'] / df['bdp_bytes'].replace(0, np.nan)
df['throughput_efficiency'] = (df['actual_throughput_kbps'] * 1024) / (df['send_rate_mbps'] * 1024 * 1024) * 100

valid_df = df[(df['bdp_bytes'] > 0) & (df['buf_to_bdp_ratio'] < 100) & (df['buf_to_bdp_ratio'].notna())]

if len(valid_df) > 0:
    scatter = ax.scatter(valid_df['buf_to_bdp_ratio'], valid_df['throughput_efficiency'],
                         c=valid_df['net_delay_ms'], cmap='viridis', alpha=0.5, s=30)
    cbar = plt.colorbar(scatter, ax=ax, label='Network Delay (ms)')

ax.axvline(x=1, color='red', linestyle='--', linewidth=2, label='Buffer = BDP')
ax.axhline(y=100, color='gray', linestyle=':', alpha=0.5)

ax.set_xlabel('Buffer Size / Bandwidth-Delay Product', fontsize=14)
ax.set_ylabel('Throughput Efficiency (%)', fontsize=14)
ax.set_title('Buffer Sizing: You Need at Least 1x BDP', fontsize=16, fontweight='bold')
ax.set_xlim(0, 10)
ax.set_ylim(0, 120)
ax.legend(loc='lower right', fontsize=11)

plt.tight_layout()
plt.savefig(output_dir / 'plot4_bdp_relationship.png', dpi=150)
print(f"Saved: plot4_bdp_relationship.png")
plt.close()

# =============================================================================
# Plot 5: Practical Guide for WAN Video
# =============================================================================
fig, ax = plt.subplots(figsize=(12, 8))

rtts = np.array([10, 25, 50, 100, 150, 200, 300])
buffer_sizes = [64*1024, 128*1024, 256*1024, 512*1024]

for buf in buffer_sizes:
    throughputs = (buf / (rtts / 1000)) / (1024 * 1024)
    ax.plot(rtts, throughputs, 'o-', label=f'{buf//1024}KB buffer', linewidth=2)

video_rates = {'SD 480p': 1.5, 'HD 720p': 3, 'HD 1080p': 6, '4K': 15}
for name, rate in video_rates.items():
    ax.axhline(y=rate, color='orange', linestyle='--', alpha=0.4)
    ax.text(310, rate, name, fontsize=9, color='darkorange', va='center')

ax.set_xlabel('Round-Trip Time (ms)', fontsize=14)
ax.set_ylabel('Achievable Throughput (MB/s)', fontsize=14)
ax.set_title('Buffer Sizing Guide for WAN Video', fontsize=16, fontweight='bold')
ax.legend(loc='upper right', fontsize=10)
ax.set_xlim(0, 350)
ax.set_ylim(0, 20)

ax.annotate('For 1080p over 100ms RTT:\nNeed at least 256KB buffer', 
            xy=(100, 6), xytext=(150, 12),
            arrowprops=dict(arrowstyle='->', color='red'),
            fontsize=11, color='red')

plt.tight_layout()
plt.savefig(output_dir / 'plot5_wan_buffer_guide.png', dpi=150)
print(f"Saved: plot5_wan_buffer_guide.png")
plt.close()

# =============================================================================
# Plot 6: LTE vs Starlink Comparison
# =============================================================================
fig, ax = plt.subplots(figsize=(12, 7))

categories = []
mean_throughputs = []
std_throughputs = []
colors_list = []

# LTE
lte_df = df[df['_source'].str.contains('lte')]
if len(lte_df) > 0:
    categories.append('LTE\n(30-150ms)')
    mean_throughputs.append(lte_df['actual_throughput_kbps'].mean() / 1024)
    std_throughputs.append(lte_df['actual_throughput_kbps'].std() / 1024)
    colors_list.append('purple')

# Starlink profiles
for profile, label, color in profiles:
    profile_df = df[df['_source'] == profile]
    if len(profile_df) > 0:
        categories.append(label.replace('\n', ' '))
        mean_throughputs.append(profile_df['actual_throughput_kbps'].mean() / 1024)
        std_throughputs.append(profile_df['actual_throughput_kbps'].std() / 1024)
        colors_list.append(color)

x = np.arange(len(categories))
bars = ax.bar(x, mean_throughputs, yerr=std_throughputs, capsize=5, color=colors_list, alpha=0.7)

ax.set_xticks(x)
ax.set_xticklabels(categories, fontsize=10)
ax.set_ylabel('Throughput (MB/s)', fontsize=14)
ax.set_title('Network Type Comparison: What Can You Actually Get?', fontsize=16, fontweight='bold')

for bar, val in zip(bars, mean_throughputs):
    ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.1, 
            f'{val:.1f}', ha='center', fontsize=10)

plt.tight_layout()
plt.savefig(output_dir / 'plot6_network_comparison.png', dpi=150)
print(f"Saved: plot6_network_comparison.png")
plt.close()

# =============================================================================
# Summary Statistics
# =============================================================================
print("\n=== CLUSTER 2 KEY FINDINGS ===\n")

print("1. Zero-window events are RARE in WAN scenarios:")
print(f"   Only {(df['zero_window_count'] > 0).sum()} of {len(df)} experiments had zero-window")

print("\n2. Throughput by network type:")
for profile, label, _ in profiles:
    profile_df = df[df['_source'] == profile]
    if len(profile_df) > 0:
        mean_tp = profile_df['actual_throughput_kbps'].mean() / 1024
        print(f"   Starlink {label.split(chr(10))[0]}: {mean_tp:.2f} MB/s")

lte_df = df[df['_source'].str.contains('lte')]
if len(lte_df) > 0:
    print(f"   LTE: {lte_df['actual_throughput_kbps'].mean() / 1024:.2f} MB/s")

print("\n3. The bottleneck in WAN is NOT receiver buffer - it's network BDP")

print("\nDone! Plots saved to:", output_dir)
