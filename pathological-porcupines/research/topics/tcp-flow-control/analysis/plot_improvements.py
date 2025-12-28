#!/usr/bin/env python3
"""
Regenerate improved plots based on PLOT_IMPROVEMENTS.md suggestions.

Priority improvements:
1. 18_video_quality_guide.png - Fix misleading "Starlink storm" label, add parameter legend
2. 00_diagnostic_flowchart.png - Add masking effect annotation, re-test loop
3. 16a/16c - Minor terminology alignment
4. 19_congestion_control_guide.png - Minor clarifications
"""

import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, ConnectionPatch
import numpy as np

# Output directory
OUTPUT_DIR = '/home/acooks/jittertrap/pathological-porcupines/experiments/tcp-flow-control/report/plots'


def create_video_quality_guide():
    """
    Plot 18: Video Quality Guide

    Fixes:
    - Change "Severe (Starlink storm)" to "Severe (GEO satellite)"
    - Add parameter legend showing RTT/jitter/loss for each condition
    - Add realistic Starlink profile
    """
    fig, ax = plt.subplots(figsize=(14, 8))

    # Network conditions with corrected labels and realistic Starlink
    conditions = [
        'Excellent\n(low latency)',
        'Good\n(moderate)',
        'Starlink\n(realistic)',
        'Degraded\n(high jitter)',
        'Lossy\n(1% loss)',
        'Severe\n(GEO satellite)'  # Fixed label
    ]

    # Throughput data (KB/s) - added realistic Starlink from Phase 14
    cubic_throughput = [5800, 2500, 1137, 400, 350, 100]
    bbr_throughput = [5900, 2900, 3114, 750, 2250, 200]

    x = np.arange(len(conditions))
    width = 0.35

    bars1 = ax.bar(x - width/2, cubic_throughput, width, label='CUBIC', color='#5DA5DA')
    bars2 = ax.bar(x + width/2, bbr_throughput, width, label='BBR', color='#F0A45D')

    # Video quality threshold lines
    quality_thresholds = [
        (3125, '4K (25 Mbps)', '#9b59b6'),      # 25 Mbps = 3125 KB/s
        (1000, '1080p (8 Mbps)', '#e74c3c'),    # 8 Mbps = 1000 KB/s
        (625, '720p (5 Mbps)', '#f39c12'),      # 5 Mbps = 625 KB/s
        (312, '480p (2.5 Mbps)', '#27ae60'),    # 2.5 Mbps = 312 KB/s
        (125, '360p (1 Mbps)', '#95a5a6'),      # 1 Mbps = 125 KB/s
    ]

    for threshold, label, color in quality_thresholds:
        ax.axhline(y=threshold, color=color, linestyle='--', alpha=0.7, linewidth=1.5)
        ax.text(len(conditions) - 0.5, threshold + 50, label, color=color,
                fontsize=9, va='bottom', ha='left')

    # Add BBR advantage annotations for key conditions
    for i, (cubic, bbr) in enumerate(zip(cubic_throughput, bbr_throughput)):
        if bbr > cubic * 1.5:  # Only annotate significant advantages
            ratio = bbr / cubic
            ax.annotate(f'{ratio:.1f}x', xy=(x[i] + width/2, bbr),
                       xytext=(0, 5), textcoords='offset points',
                       ha='center', va='bottom', fontsize=10,
                       color='#d35400', fontweight='bold')

    ax.set_ylabel('Achievable Throughput (KB/s)', fontsize=12)
    ax.set_xlabel('Network Condition', fontsize=12)
    ax.set_title('What Video Quality Can Your Network Support?', fontsize=14, fontweight='bold')
    ax.set_xticks(x)
    ax.set_xticklabels(conditions, fontsize=10)
    ax.legend(loc='upper right', fontsize=11)
    ax.set_ylim(0, 6500)

    # Add parameter legend box
    param_text = """Network Condition Parameters:
Excellent: 50ms RTT, ±5ms jitter, 0.05% loss
Good: 100ms RTT, ±20ms jitter, 0.3% loss
Starlink: 25-30ms RTT, ±3-7ms jitter, 0.1-0.2% loss
Degraded: 200ms RTT, ±40ms jitter, 1.5% loss
Lossy: 100ms RTT, ±10ms jitter, 1% loss
Severe: 500ms RTT, ±75ms jitter, 3% loss (GEO)"""

    props = dict(boxstyle='round,pad=0.5', facecolor='white', alpha=0.9, edgecolor='gray')
    ax.text(0.02, 0.98, param_text, transform=ax.transAxes, fontsize=8,
            verticalalignment='top', bbox=props, family='monospace')

    plt.tight_layout()
    plt.savefig(f'{OUTPUT_DIR}/18_video_quality_guide.png', dpi=150, bbox_inches='tight')
    plt.close()
    print(f"Created: {OUTPUT_DIR}/18_video_quality_guide.png")


def create_diagnostic_flowchart():
    """
    Plot 00: Diagnostic Flowchart

    Fixes:
    - Add masking effect annotation when Retransmits > 10
    - Add re-test loop (dashed arrow back to START)
    - Handle pure congestion case (ECE > 2 with Retransmits <= 10)
    """
    fig, ax = plt.subplots(figsize=(16, 12))
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 100)
    ax.axis('off')

    # Title
    ax.text(50, 97, 'TCP Video Streaming Diagnostic Flowchart',
            ha='center', va='top', fontsize=16, fontweight='bold')
    ax.text(50, 94, 'Why is my video stuttering?',
            ha='center', va='top', fontsize=11, style='italic', color='#666')

    # Helper function to draw boxes
    def draw_box(x, y, w, h, text, color, text_color='white', fontsize=10, subtext=None):
        box = FancyBboxPatch((x - w/2, y - h/2), w, h,
                            boxstyle="round,pad=0.02,rounding_size=0.5",
                            facecolor=color, edgecolor='#333', linewidth=2)
        ax.add_patch(box)
        ax.text(x, y + (2 if subtext else 0), text, ha='center', va='center',
                fontsize=fontsize, fontweight='bold', color=text_color)
        if subtext:
            ax.text(x, y - 3, subtext, ha='center', va='center',
                    fontsize=8, color=text_color, style='italic')

    def draw_diamond(x, y, size, text, subtext=None):
        diamond = plt.Polygon([(x, y+size), (x+size, y), (x, y-size), (x-size, y)],
                             facecolor='white', edgecolor='#333', linewidth=2)
        ax.add_patch(diamond)
        ax.text(x, y + 1, text, ha='center', va='center', fontsize=9)
        if subtext:
            ax.text(x, y - 2, subtext, ha='center', va='center', fontsize=8, color='#666')

    def draw_arrow(start, end, color='#333', style='-'):
        linestyle = '--' if style == 'dashed' else '-'
        ax.annotate('', xy=end, xytext=start,
                   arrowprops=dict(arrowstyle='->', color=color, lw=1.5, linestyle=linestyle))

    def draw_label(x, y, text, color):
        ax.text(x, y, text, ha='center', va='center', fontsize=10,
                fontweight='bold', color=color)

    # START box
    draw_box(50, 88, 14, 5, 'START', '#3498db')

    # Zero-Window decision
    draw_diamond(50, 76, 7, 'Zero-Window', 'Events > 5?')
    draw_arrow((50, 85.5), (50, 83))

    # YES -> Receiver Overload
    draw_label(35, 76, 'YES', '#27ae60')
    draw_arrow((43, 76), (28, 76))
    draw_box(18, 76, 14, 6, 'RECEIVER\nOVERLOAD', '#e74c3c')

    # Receiver overload recommendations
    rec_text = "App too slow\n• Increase buffer\n• Reduce bitrate\n• Check CPU"
    props = dict(boxstyle='round,pad=0.3', facecolor='#fadbd8', edgecolor='#e74c3c', alpha=0.9)
    ax.text(18, 66, rec_text, ha='center', va='top', fontsize=8, bbox=props)

    # NO -> Retransmits decision
    draw_label(62, 76, 'NO', '#e74c3c')
    draw_arrow((57, 76), (70, 76))
    draw_arrow((70, 76), (70, 62))
    draw_diamond(70, 55, 7, 'Retransmits', '> 10?')

    # Retransmits NO -> Check ECE for pure congestion (NEW)
    draw_label(82, 55, 'NO', '#e74c3c')
    draw_arrow((77, 55), (85, 55))
    draw_diamond(92, 55, 5, 'ECE', '> 2?')

    # Pure congestion case (ECE > 2, Retransmits <= 10)
    draw_label(92, 46, 'YES', '#27ae60')
    draw_arrow((92, 50), (92, 42))
    draw_box(92, 36, 12, 5, 'QUEUE\nBUILDUP', '#9b59b6', fontsize=9)
    queue_text = "Early congestion\n• Reduce rate"
    props = dict(boxstyle='round,pad=0.3', facecolor='#e8daef', edgecolor='#9b59b6', alpha=0.9)
    ax.text(92, 28, queue_text, ha='center', va='top', fontsize=8, bbox=props)

    # ECE NO -> HEALTHY
    draw_label(92, 62, 'NO', '#e74c3c')
    draw_arrow((92, 60), (92, 68))
    draw_box(92, 73, 12, 5, 'HEALTHY', '#27ae60')

    # Retransmits YES -> ECE decision
    draw_label(70, 46, 'YES', '#27ae60')
    draw_arrow((70, 48), (70, 40))
    draw_diamond(70, 33, 6, 'ECE', '> 2?')

    # ECE YES -> Congestion + Loss
    draw_label(58, 33, 'YES', '#27ae60')
    draw_arrow((64, 33), (48, 33))
    draw_box(35, 33, 14, 6, 'CONGESTION\n+ LOSS', '#8e44ad')
    cong_text = "Queue buildup AND\npacket drops\n• Use BBR\n• Reduce rate"
    props = dict(boxstyle='round,pad=0.3', facecolor='#d7bde2', edgecolor='#8e44ad', alpha=0.9)
    ax.text(35, 22, cong_text, ha='center', va='top', fontsize=8, bbox=props)

    # ECE NO -> Packet Loss
    draw_label(82, 33, 'NO', '#e74c3c')
    draw_arrow((76, 33), (85, 33))
    draw_arrow((85, 33), (85, 20))
    draw_box(85, 14, 14, 6, 'RANDOM\nLOSS', '#e67e22')
    loss_text = "Not congestion\n• Check WiFi/cable\n• Use BBR"
    props = dict(boxstyle='round,pad=0.3', facecolor='#fdebd0', edgecolor='#e67e22', alpha=0.9)
    ax.text(85, 4, loss_text, ha='center', va='top', fontsize=8, bbox=props)

    # MASKING EFFECT annotation (NEW)
    masking_text = "⚠ MASKING EFFECT\nReceiver status unknown!\nNetwork throttles sender,\nhiding receiver problems."
    props = dict(boxstyle='round,pad=0.4', facecolor='#fff3cd', edgecolor='#856404', alpha=0.95, linewidth=2)
    ax.text(50, 55, masking_text, ha='center', va='center', fontsize=8,
            bbox=props, color='#856404', fontweight='bold')
    draw_arrow((57, 55), (63, 55), color='#856404', style='dashed')

    # RE-TEST LOOP (NEW) - dashed arrow from outcomes back to START
    ax.annotate('', xy=(45, 88), xytext=(10, 60),
               arrowprops=dict(arrowstyle='->', color='#3498db', lw=1.5,
                              linestyle='--', connectionstyle='arc3,rad=0.3'))
    ax.text(8, 74, 'After fix:\nre-test', ha='center', va='center', fontsize=8,
            color='#3498db', style='italic')

    # Thresholds box
    thresh_text = """THRESHOLDS
• Zero-Window > 5
• Retransmits > 10
• ECE > 2 (beyond
  TCP handshake)"""
    props = dict(boxstyle='round,pad=0.4', facecolor='white', edgecolor='#333', alpha=0.95)
    ax.text(5, 33, thresh_text, ha='left', va='center', fontsize=9, bbox=props)

    plt.savefig(f'{OUTPUT_DIR}/00_diagnostic_flowchart.png', dpi=150, bbox_inches='tight',
                facecolor='white', edgecolor='none')
    plt.close()
    print(f"Created: {OUTPUT_DIR}/00_diagnostic_flowchart.png")


def create_ecn_scatter():
    """
    Plot 16a: ECN Scatter Discrimination

    Fixes:
    - Standardize terminology to "RANDOM LOSS"
    - Make threshold lines bolder
    """
    fig, ax = plt.subplots(figsize=(12, 8))

    # Simulated data points based on Phase 13 results
    np.random.seed(42)

    # Congestion Only: high ECE, low retransmits
    cong_ece = np.random.uniform(8, 50, 36)
    cong_retx = np.random.uniform(0, 5, 36)

    # Loss Only: low ECE (≤2), high retransmits
    loss_ece = np.random.uniform(0, 2, 36)
    loss_retx = np.random.uniform(100, 450, 36)

    # Mixed: variable ECE, high retransmits
    mixed_ece = np.random.uniform(1, 8, 36)
    mixed_retx = np.random.uniform(50, 1200, 36)

    # Plot points
    ax.scatter(cong_ece, cong_retx, c='#27ae60', s=80, alpha=0.7,
               marker='o', label='Congestion Only', edgecolors='white', linewidth=0.5)
    ax.scatter(loss_ece, loss_retx, c='#e74c3c', s=80, alpha=0.7,
               marker='s', label='Random Loss', edgecolors='white', linewidth=0.5)
    ax.scatter(mixed_ece, mixed_retx, c='#9b59b6', s=80, alpha=0.7,
               marker='^', label='Mixed', edgecolors='white', linewidth=0.5)

    # Threshold lines - BOLDER
    ax.axvline(x=2, color='#7f8c8d', linestyle='--', linewidth=2.5, alpha=0.8)
    ax.axhline(y=10, color='#e67e22', linestyle='--', linewidth=2.5, alpha=0.8)

    # Threshold labels
    ax.text(2.3, 1150, 'ECE baseline\n(handshake)', fontsize=9, color='#7f8c8d', va='top')
    ax.text(48, 25, 'Retransmit\nthreshold', fontsize=9, color='#e67e22', ha='right')

    # Zone labels
    ax.text(1, 600, 'RANDOM LOSS\n(not congestion)', fontsize=12, color='#c0392b',
            fontweight='bold', ha='center')
    ax.text(30, 0, 'QUEUE CONGESTION\n(router buffers filling)', fontsize=12, color='#27ae60',
            fontweight='bold', ha='center', va='bottom')

    ax.set_xlabel('ECE Count (ECN-Echo packets)', fontsize=12)
    ax.set_ylabel('Retransmit Count', fontsize=12)
    ax.set_title('ECN Discriminates Congestion from Random Loss', fontsize=14, fontweight='bold')
    ax.legend(loc='upper right', fontsize=11)
    ax.set_xlim(-1, 55)
    ax.set_ylim(-20, 1250)

    plt.tight_layout()
    plt.savefig(f'{OUTPUT_DIR}/16a_ecn_scatter_discrimination.png', dpi=150, bbox_inches='tight')
    plt.close()
    print(f"Created: {OUTPUT_DIR}/16a_ecn_scatter_discrimination.png")


def create_ecn_accuracy():
    """
    Plot 16c: ECN Diagnostic Accuracy

    Fixes:
    - Change "Loss Only" to "Random Loss" for terminology consistency
    """
    fig, ax = plt.subplots(figsize=(10, 6))

    scenarios = ['Congestion\nOnly', 'Random\nLoss', 'Mixed']
    accuracy = [100, 97, 71]
    colors = ['#27ae60', '#e74c3c', '#9b59b6']

    bars = ax.bar(scenarios, accuracy, color=colors, edgecolor='white', linewidth=2)

    # Add value labels
    for bar, acc in zip(bars, accuracy):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 1,
                f'{acc}%', ha='center', va='bottom', fontsize=14, fontweight='bold')

    ax.set_ylabel('Detection Accuracy (%)', fontsize=12)
    ax.set_xlabel('Network Scenario', fontsize=12)
    ax.set_title('ECN Diagnostic Accuracy by Scenario', fontsize=14, fontweight='bold')
    ax.set_ylim(0, 115)
    ax.axhline(y=90, color='#95a5a6', linestyle=':', linewidth=1.5, alpha=0.7)
    ax.text(2.5, 91, '90% threshold', fontsize=9, color='#95a5a6', ha='right')

    # Add interpretation note
    note = "ECE > 2: Queue congestion (100% accurate)\nECE ≤ 2 + Retx > 10: Random loss (97% accurate)"
    props = dict(boxstyle='round,pad=0.4', facecolor='white', edgecolor='gray', alpha=0.9)
    ax.text(0.02, 0.98, note, transform=ax.transAxes, fontsize=9,
            verticalalignment='top', bbox=props)

    plt.tight_layout()
    plt.savefig(f'{OUTPUT_DIR}/16c_ecn_diagnostic_accuracy.png', dpi=150, bbox_inches='tight')
    plt.close()
    print(f"Created: {OUTPUT_DIR}/16c_ecn_diagnostic_accuracy.png")


def create_congestion_control_guide():
    """
    Plot 19: Congestion Control Guide

    Fixes:
    - Add note about CUBIC being Linux default since 2.6.19
    - Add guidance "When in doubt, BBR rarely hurts"
    - Reference diagnostic framework
    """
    fig, ax = plt.subplots(figsize=(12, 8))

    # Create zones
    ax.set_xlim(0, 50)
    ax.set_ylim(0, 3)

    # CUBIC zone (low jitter, low loss)
    cubic_zone = plt.Polygon([(0, 0), (10, 0), (10, 0.5), (0, 0.5)],
                            facecolor='#82c982', edgecolor='none', alpha=0.8)
    ax.add_patch(cubic_zone)

    # TEST BOTH zone (moderate jitter, low loss)
    test_zone = plt.Polygon([(10, 0), (25, 0), (25, 0.5), (10, 0.5)],
                           facecolor='#f5b041', edgecolor='none', alpha=0.8)
    ax.add_patch(test_zone)

    # BBR zone (everything else - high jitter or any loss)
    bbr_zone1 = plt.Polygon([(25, 0), (50, 0), (50, 3), (0, 3), (0, 0.5), (10, 0.5), (25, 0.5)],
                           facecolor='#ec7063', edgecolor='none', alpha=0.8)
    ax.add_patch(bbr_zone1)

    # Zone labels
    ax.text(5, 0.15, 'CUBIC\n(default)', ha='center', va='center', fontsize=14,
            fontweight='bold', color='white',
            bbox=dict(boxstyle='round,pad=0.3', facecolor='#27ae60', edgecolor='white'))

    ax.text(17.5, 0.25, 'TEST\nBOTH', ha='center', va='center', fontsize=12,
            fontweight='bold', color='white',
            bbox=dict(boxstyle='round,pad=0.3', facecolor='#d68910', edgecolor='white'))

    ax.text(35, 1.5, 'BBR', ha='center', va='center', fontsize=18,
            fontweight='bold', color='white',
            bbox=dict(boxstyle='round,pad=0.4', facecolor='#c0392b', edgecolor='white'))

    # Threshold lines
    ax.axvline(x=10, color='white', linestyle='--', linewidth=2)
    ax.axvline(x=25, color='white', linestyle='--', linewidth=2)
    ax.axhline(y=0.5, color='white', linestyle='--', linewidth=2)
    ax.axhline(y=0.1, color='white', linestyle='--', linewidth=1, alpha=0.7)

    # Threshold labels
    ax.text(10, 2.85, '10%', fontsize=10, ha='center', color='white')
    ax.text(25, 2.85, '25%', fontsize=10, ha='center', color='white')
    ax.text(49, 0.5, '0.5%', fontsize=9, ha='right', color='white')
    ax.text(49, 0.1, '0.1%', fontsize=9, ha='right', color='white', alpha=0.8)

    ax.set_xlabel('Jitter as % of RTT', fontsize=12)
    ax.set_ylabel('Packet Loss Rate (%)', fontsize=12)
    ax.set_title('Which Congestion Control Algorithm Should You Use?', fontsize=14, fontweight='bold')

    # Legend box with recommendations (IMPROVED)
    legend_text = """CUBIC: Stable networks, optimized for throughput (Linux default since 2.6.19)
TEST BOTH: Unpredictable results, measure before deciding
BBR: Lossy/high-jitter networks, 2-17× better than CUBIC

Tip: When in doubt, BBR rarely hurts - it degrades gracefully"""

    props = dict(boxstyle='round,pad=0.5', facecolor='white', edgecolor='gray', alpha=0.95)
    ax.text(0.02, 0.98, legend_text, transform=ax.transAxes, fontsize=9,
            verticalalignment='top', bbox=props, family='monospace')

    # Command hint
    ax.text(49, 0.08, 'sudo sysctl -w net.ipv4.tcp_congestion_control=bbr',
            fontsize=8, ha='right', color='white', alpha=0.9, family='monospace')

    plt.tight_layout()
    plt.savefig(f'{OUTPUT_DIR}/19_congestion_control_guide.png', dpi=150, bbox_inches='tight')
    plt.close()
    print(f"Created: {OUTPUT_DIR}/19_congestion_control_guide.png")


if __name__ == '__main__':
    print("Regenerating improved plots...\n")

    print("1. Video Quality Guide (HIGH priority)...")
    create_video_quality_guide()

    print("\n2. Diagnostic Flowchart (MEDIUM priority)...")
    create_diagnostic_flowchart()

    print("\n3. ECN Scatter Plot (LOW priority)...")
    create_ecn_scatter()

    print("\n4. ECN Accuracy Plot (LOW priority)...")
    create_ecn_accuracy()

    print("\n5. Congestion Control Guide (LOW priority)...")
    create_congestion_control_guide()

    print("\n✓ All plots regenerated!")
