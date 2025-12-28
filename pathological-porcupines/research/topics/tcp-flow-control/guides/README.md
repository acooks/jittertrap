# Practical Guides

Synthesized recommendations from the investigations.

## Available Guides

### diagnostic-flowchart.png
**"Why is my video stuttering?"**

Master decision tree for diagnosing video streaming problems. Start here.

Covers:
- Zero-window → Receiver problem
- Retransmits → Network problem
- ECE flags → Congestion vs random loss
- IPG gaps → Sender stalls

### video-quality-guide.png
**"What video quality can my network support?"**

Maps network conditions to achievable video quality with BBR vs CUBIC.

### congestion-control-guide.png
**"Which congestion control algorithm should I use?"**

Quick reference: jitter region → algorithm recommendation.

## Using These Guides

1. Start with the diagnostic flowchart to identify the problem type
2. Use the video quality guide to set expectations
3. Use the congestion control guide to optimize

For the evidence behind these guides, see the [investigations](../investigations/).
