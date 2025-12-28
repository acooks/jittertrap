#!/usr/bin/env python3
"""Generate more intuitive plots for TCP flow control experiment results."""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

# Load all cluster1 data
script_dir = Path(__file__).parent
experiment_dir = script_dir.parent
data_dir = experiment_dir / 'results' / 'cluster1'
output_dir = data_dir / 'plots'
output_dir.mkdir(exist_ok=True)

dfs = []
for csv in data_dir.glob('lan-*.csv'):
    dfs.append(pd.read_csv(csv))
df = pd.concat(dfs, ignore_index=True)

print(f"Loaded {len(df)} experiments")

# Set style
plt.style.use('seaborn-v0_8-whitegrid')
plt.rcParams['figure.figsize'] = (12, 8)
plt.rcParams['font.size'] = 12

# =============================================================================
# Plot 1: The Key Question - How long can the receiver stall before problems?
# =============================================================================
fig, ax = plt.subplots(figsize=(12, 7))

# For each send rate, find the max delay that has zero zero-window events
rates = sorted(df['send_rate_mbps'].unique())
colors = plt.cm.viridis(np.linspace(0.2, 0.9, len(rates)))

for rate, color in zip(rates, colors):
    rate_df = df[df['send_rate_mbps'] == rate]
    
    # Group by delay, check if ANY experiment at that delay had zero-window
    delays = sorted(rate_df['delay_ms'].unique())
    safe_delays = []
    problem_delays = []
    
    for delay in delays:
        delay_df = rate_df[rate_df['delay_ms'] == delay]
        if delay_df['zero_window_count'].sum() == 0:
            safe_delays.append(delay)
        else:
            problem_delays.append(delay)
    
    # Find the threshold
    if safe_delays and problem_delays:
        threshold = max(safe_delays)
        ax.axhline(y=threshold, color=color, linestyle='--', alpha=0.3)
        ax.scatter([rate], [threshold], s=200, color=color, marker='o', 
                   edgecolors='black', linewidth=2, zorder=5,
                   label=f'{rate} MB/s → safe up to {threshold}ms stall')
    elif not problem_delays:
        ax.scatter([rate], [max(delays)], s=200, color=color, marker='^',
                   edgecolors='black', linewidth=2, zorder=5,
                   label=f'{rate} MB/s → safe at all tested delays')
    else:
        ax.scatter([rate], [0], s=200, color=color, marker='x',
                   linewidth=3, zorder=5,
                   label=f'{rate} MB/s → problems even at {min(delays)}ms')

ax.set_xlabel('Video Bitrate (MB/s)', fontsize=14)
ax.set_ylabel('Maximum Safe CPU Stall Duration (ms)', fontsize=14)
ax.set_title('How Long Can Your Receiver Stall Before TCP Flow Control Kicks In?', fontsize=16, fontweight='bold')
ax.legend(loc='upper right', fontsize=10)
ax.set_xlim(0, max(rates) + 2)
ax.set_ylim(0, 25)

# Add annotations
ax.annotate('Safe Zone\n(no zero-window)', xy=(15, 15), fontsize=12, 
            color='green', alpha=0.7, ha='center')
ax.annotate('Problem Zone\n(buffer starvation)', xy=(15, 3), fontsize=12,
            color='red', alpha=0.7, ha='center')

plt.tight_layout()
plt.savefig(output_dir / 'plot1_max_safe_stall.png', dpi=150)
print(f"Saved: plot1_max_safe_stall.png")
plt.close()

# =============================================================================
# Plot 2: Throughput vs What You Asked For
# =============================================================================
fig, axes = plt.subplots(2, 2, figsize=(14, 10))

delays_to_show = [1, 5, 10, 20]
for ax, delay in zip(axes.flat, delays_to_show):
    delay_df = df[df['delay_ms'] == delay]
    
    for buf_size in sorted(delay_df['recv_buf'].unique()):
        buf_df = delay_df[delay_df['recv_buf'] == buf_size]
        buf_df = buf_df.groupby('send_rate_mbps').agg({
            'actual_throughput_kbps': 'mean',
            'zero_window_count': 'mean'
        }).reset_index()
        
        # Convert to MB/s for consistency
        buf_df['actual_MB'] = buf_df['actual_throughput_kbps'] / 1024
        buf_df['requested_MB'] = buf_df['send_rate_mbps']
        
        label = f'{buf_size//1024}KB buf'
        ax.plot(buf_df['requested_MB'], buf_df['actual_MB'], 'o-', label=label, alpha=0.7)
    
    # Add ideal line
    max_rate = delay_df['send_rate_mbps'].max()
    ax.plot([0, max_rate], [0, max_rate], 'k--', alpha=0.3, label='Ideal (got what you asked)')
    
    # Theoretical max based on delay
    theoretical_max = (4096 / (delay / 1000)) / (1024 * 1024)  # MB/s with 4KB reads
    ax.axhline(y=theoretical_max, color='red', linestyle=':', alpha=0.5)
    ax.annotate(f'Receiver limit\n({theoretical_max:.1f} MB/s)', 
                xy=(max_rate * 0.7, theoretical_max), fontsize=9, color='red')
    
    ax.set_xlabel('Requested Rate (MB/s)')
    ax.set_ylabel('Actual Throughput (MB/s)')
    ax.set_title(f'Receiver Stall: {delay}ms between reads', fontweight='bold')
    ax.legend(fontsize=8, loc='lower right')
    ax.set_xlim(0, max_rate + 1)
    ax.set_ylim(0, max(delay_df['actual_throughput_kbps'] / 1024) * 1.1)

plt.suptitle('What Throughput Do You Actually Get?', fontsize=16, fontweight='bold', y=1.02)
plt.tight_layout()
plt.savefig(output_dir / 'plot2_throughput_reality.png', dpi=150)
print(f"Saved: plot2_throughput_reality.png")
plt.close()

# =============================================================================
# Plot 3: Zero-Window Events = Video Stutter Potential
# =============================================================================
fig, ax = plt.subplots(figsize=(12, 8))

# Pivot for heatmap: delay vs send_rate, colored by zero_window severity
pivot = df.groupby(['delay_ms', 'send_rate_mbps'])['zero_window_count'].mean().reset_index()
pivot_table = pivot.pivot(index='delay_ms', columns='send_rate_mbps', values='zero_window_count')

# Custom colormap: green (0) -> yellow -> red (high)
from matplotlib.colors import LinearSegmentedColormap
colors_list = ['#2ecc71', '#f1c40f', '#e74c3c', '#8e44ad']
cmap = LinearSegmentedColormap.from_list('stutter', colors_list)

im = ax.imshow(pivot_table.values, cmap=cmap, aspect='auto', origin='lower')

# Labels
ax.set_xticks(range(len(pivot_table.columns)))
ax.set_xticklabels([f'{x:.0f}' for x in pivot_table.columns])
ax.set_yticks(range(len(pivot_table.index)))
ax.set_yticklabels([f'{x:.0f}' for x in pivot_table.index])

ax.set_xlabel('Send Rate (MB/s)', fontsize=14)
ax.set_ylabel('Receiver Stall Duration (ms)', fontsize=14)
ax.set_title('Video Stutter Risk: Zero-Window Events per 10-Second Test', fontsize=16, fontweight='bold')

# Add text annotations
for i in range(len(pivot_table.index)):
    for j in range(len(pivot_table.columns)):
        val = pivot_table.values[i, j]
        color = 'white' if val > 200 else 'black'
        if val > 0:
            ax.text(j, i, f'{val:.0f}', ha='center', va='center', color=color, fontsize=10)
        else:
            ax.text(j, i, '✓', ha='center', va='center', color='darkgreen', fontsize=14, fontweight='bold')

cbar = plt.colorbar(im, ax=ax, label='Zero-Window Events (more = more stutter risk)')

plt.tight_layout()
plt.savefig(output_dir / 'plot3_stutter_risk.png', dpi=150)
print(f"Saved: plot3_stutter_risk.png")
plt.close()

# =============================================================================
# Plot 4: Buffer Size - Does It Help?
# =============================================================================
fig, axes = plt.subplots(1, 2, figsize=(14, 6))

# Left: Zero-window count by buffer size
ax = axes[0]
buf_data = df.groupby('recv_buf').agg({
    'zero_window_count': ['mean', 'std'],
    'actual_throughput_kbps': 'mean'
}).reset_index()
buf_data.columns = ['recv_buf', 'zw_mean', 'zw_std', 'throughput']
buf_data['buf_kb'] = buf_data['recv_buf'] / 1024

ax.bar(range(len(buf_data)), buf_data['zw_mean'], 
       yerr=buf_data['zw_std'], capsize=5, color='steelblue', alpha=0.7)
ax.set_xticks(range(len(buf_data)))
ax.set_xticklabels([f'{int(x)}KB' for x in buf_data['buf_kb']])
ax.set_xlabel('Receive Buffer Size', fontsize=12)
ax.set_ylabel('Average Zero-Window Events', fontsize=12)
ax.set_title('Does Bigger Buffer Help?', fontweight='bold')

# Annotate the finding
ax.annotate('Diminishing returns\nafter 32KB', xy=(2, buf_data['zw_mean'].iloc[2]), 
            xytext=(3.5, buf_data['zw_mean'].max() * 0.8),
            arrowprops=dict(arrowstyle='->', color='red'),
            fontsize=11, color='red')

# Right: Success rate (no zero-window) by buffer size
ax = axes[1]
success_by_buf = df.groupby('recv_buf').apply(
    lambda x: (x['zero_window_count'] == 0).sum() / len(x) * 100
).reset_index()
success_by_buf.columns = ['recv_buf', 'success_pct']
success_by_buf['buf_kb'] = success_by_buf['recv_buf'] / 1024

bars = ax.bar(range(len(success_by_buf)), success_by_buf['success_pct'], 
              color='mediumseagreen', alpha=0.7)
ax.set_xticks(range(len(success_by_buf)))
ax.set_xticklabels([f'{int(x)}KB' for x in success_by_buf['buf_kb']])
ax.set_xlabel('Receive Buffer Size', fontsize=12)
ax.set_ylabel('% of Tests with No Zero-Window', fontsize=12)
ax.set_title('Stutter-Free Success Rate', fontweight='bold')
ax.set_ylim(0, 100)

for bar, pct in zip(bars, success_by_buf['success_pct']):
    ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 2, 
            f'{pct:.0f}%', ha='center', fontsize=10)

plt.suptitle('Buffer Size Impact on TCP Flow Control', fontsize=16, fontweight='bold', y=1.02)
plt.tight_layout()
plt.savefig(output_dir / 'plot4_buffer_impact.png', dpi=150)
print(f"Saved: plot4_buffer_impact.png")
plt.close()

# =============================================================================
# Plot 5: Practical Guidance Chart
# =============================================================================
fig, ax = plt.subplots(figsize=(12, 8))

# Create zones
delays = np.array([1, 2, 3, 5, 7, 10, 15, 20])
rates = np.array([2, 4, 5, 8, 10, 16, 20, 32])

# Calculate receiver capacity for each delay (assuming 8KB reads)
read_size = 8192
capacities = read_size / (delays / 1000) / (1024 * 1024)  # MB/s

# For each delay, what's the max safe rate?
ax.fill_between([0, 35], [0, 0], [22, 22], alpha=0.1, color='red', label='_')
ax.fill_between([0, 35], [0, 0], [capacities.min(), capacities.min()], alpha=0.2, color='green', label='_')

# Plot the receiver capacity curve
ax.plot(delays, capacities, 'b-', linewidth=3, label='Receiver Capacity (8KB reads)')
ax.fill_between(delays, 0, capacities, alpha=0.3, color='green')

# Add common video bitrates
video_rates = {
    'SD 480p': 1.5,
    'HD 720p': 3,
    'HD 1080p': 6,
    '4K': 15,
    '4K HDR': 25
}

for name, rate in video_rates.items():
    ax.axhline(y=rate, color='orange', linestyle='--', alpha=0.5)
    ax.text(21, rate + 0.3, name, fontsize=10, color='darkorange')

ax.set_xlabel('Receiver CPU Stall Duration (ms)', fontsize=14)
ax.set_ylabel('Video Bitrate (MB/s)', fontsize=14)
ax.set_title('Can Your System Handle This Video Stream?', fontsize=16, fontweight='bold')
ax.set_xlim(0, 22)
ax.set_ylim(0, 30)

# Add zone labels
ax.text(5, 20, 'WILL STUTTER', fontsize=14, color='red', fontweight='bold', alpha=0.7)
ax.text(15, 2, 'SAFE ZONE', fontsize=14, color='green', fontweight='bold', alpha=0.7)

ax.legend(loc='upper right', fontsize=11)
plt.tight_layout()
plt.savefig(output_dir / 'plot5_practical_guide.png', dpi=150)
print(f"Saved: plot5_practical_guide.png")
plt.close()

print("\nDone! New plots saved to:", output_dir)
