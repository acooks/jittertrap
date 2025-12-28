# Video Stream Playback

JitterTrap can detect and play back video streams directly in your browser using WebRTC. This allows you to visually verify the quality of video streams while simultaneously analyzing their network characteristics.

## Overview

When JitterTrap detects an RTP video stream (H.264, H.265/HEVC, VP8, VP9, or AV1), a play button appears next to the flow in the Top Talkers view. Clicking this button opens a video overlay that displays the live stream.

The video playback works by:
1. Depacketizing RTP packets received on the capture interface
2. Re-packetizing them into WebRTC format with SRTP encryption
3. Streaming to the browser via a WebRTC PeerConnection

This approach provides sub-second latency playback without requiring any plugins or external players.

## Supported Codecs

| Codec | Browser Support |
|-------|-----------------|
| H.264 | All modern browsers (Chrome, Firefox, Edge, Safari) |
| H.265/HEVC | Chrome 136+, Safari, Edge 136+ |

**Note:** H.265/HEVC WebRTC support was added to Chrome in version 136 (May 2025). Older browsers will show an error when attempting to play H.265 streams.

VP8, VP9, and AV1 streams are detected by JitterTrap but WebRTC playback is not yet implemented for these codecs.

## Using Video Playback

1. Start JitterTrap and select a network interface
2. Wait for video streams to be detected (shown in Top Talkers)
3. Click the **▶** play button next to a video flow
4. The video overlay opens and begins playback once a keyframe is received

### Video Overlay Controls

- **Close (×)** — Stop playback and close the overlay
- **Fullscreen (F)** — Toggle fullscreen mode
- **Escape** — Close the overlay (or exit fullscreen if active)

The overlay displays:
- Current playback status (Connecting, Waiting for keyframe, Playing)
- Video resolution and framerate

## Technical Details

### Mid-Stream Joins

When you start playback on an already-running stream, JitterTrap waits for the next keyframe (IDR/IRAP) before beginning playback. This ensures the decoder receives a clean starting point. For streams with long GOP intervals (e.g., 10 seconds), there may be a delay before video appears.

### Parameter Set Handling

For H.264, JitterTrap stores SPS (Sequence Parameter Set) and PPS (Picture Parameter Set) NAL units and bundles them with the first IDR frame sent to the browser.

For H.265, JitterTrap stores VPS (Video Parameter Set), SPS, and PPS NAL units and bundles them with every IRAP frame. This ensures the decoder always has the necessary parameters.

### NACK Support

The WebRTC bridge includes a NACK responder that can retransmit lost packets when requested by the browser. This improves reliability on lossy networks.

## Build Configuration

WebRTC playback is enabled by default. To disable it:

```bash
make ENABLE_WEBRTC_PLAYBACK=0
```

This removes the libdatachannel dependency and reduces the binary size.

### libdatachannel

JitterTrap bundles libdatachannel 0.23.3 rather than using system packages because:

1. **Ubuntu 22.04** doesn't have libdatachannel in its repositories
2. **Fedora's libdatachannel 0.21.x** has a bug where H.265 RTP timestamps are not set correctly, causing playback to freeze

The bundled library is built automatically on first compile. To use a system-installed libdatachannel instead:

```bash
make LIBDATACHANNEL_DIR=/usr
```

## Troubleshooting

### "Waiting for keyframe" stays forever
The source stream may not be sending keyframes, or the keyframe interval is very long. Try waiting longer or check that the source is actively streaming.

### "Codec not supported" error
Your browser doesn't support the stream's codec via WebRTC. For H.265, use Chrome 136+ or Safari.

### Video freezes periodically
This may indicate packet loss between JitterTrap and the video source. Check the jitter and packet loss statistics in the flow details.

### No play button appears
JitterTrap may not have detected the stream as video. Ensure it's a valid RTP stream with a recognized payload type.
