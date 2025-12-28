## JitterTrap

[![Coverity Status](https://scan.coverity.com/projects/4088/badge.svg)](https://scan.coverity.com/projects/4088)
[![Build Status](https://github.com/acooks/jittertrap/actions/workflows/main.yml/badge.svg)](https://github.com/acooks/jittertrap/actions/workflows/main.yml)

JitterTrap is a real-time network performance analysis tool for engineers working with delay-sensitive networked applications and devices. It provides live measurements, per-flow TCP analysis, and network impairment emulation—all through a web-based interface.

### Key Features

**Real-time Traffic Analysis**
- Throughput (bits/s, packets/s) with configurable sampling intervals (5ms–1000ms)
- Inter-packet gap measurement to detect buffering and scheduling issues
- Top Talkers breakdown by flow (source/destination IP:port)

**TCP Flow Analysis**
- **Round-Trip Time (RTT)** — measured from TCP sequence/ACK pairs, with EWMA smoothing
- **Advertised Window** — tracks receive window with proper window scaling (RFC 7323)
- **Connection State** — visual markers for SYN, FIN, RST events
- **Congestion Events** — detects zero window, duplicate ACKs, retransmissions, ECN

**Video & Audio Stream Analysis**
- Automatic detection of RTP video streams (H.264, H.265, VP8/VP9) and MPEG-TS
- Codec identification, resolution, framerate, and bitrate extraction
- Jitter measurement and packet loss tracking per stream
- In-browser video playback via WebRTC (click the play button on any detected stream)

**Network Impairment Emulation**
- Inject delay, jitter, and packet loss on egress traffic
- Scriptable impairment programs for automated testing

**Packet Capture**
- 30-second rolling buffer with trap-triggered capture
- Download as pcap for Wireshark analysis

### Use Cases

- **Characterise source behaviour** — measure throughput, packet rates, and jitter from devices under test
- **Characterise destination behaviour** — inject impairments to verify application resilience to delay/loss
- **Debug TCP performance** — identify RTT spikes, window limitations, and retransmission patterns
- **Network congestion analysis** — real-time visibility into traffic patterns

The user interface is a web application. [Try the demo](http://demo.jittertrap.net) hosted on AWS Sydney.

Please use [GitHub Discussions](https://github.com/acooks/jittertrap/discussions) for questions and suggestions.


## Installing JitterTrap

We're aiming to release packages for Fedora, Ubuntu and OpenWRT and **would appreciate help with that**.

## Building JitterTrap
### Dependencies
* [libnl](https://www.infradead.org/~tgr/libnl/) >= 3.2.24
* [libwebsockets](https://libwebsockets.org/index.html) >= 1.6
* [libjansson](http://www.digip.org/jansson/) >= 2.6

#### Fedora  

Build dependencies:  

    sudo dnf install libnl3-devel jansson-devel libwebsockets-devel libpcap-devel

Run-time dependencies:

    sudo dnf install libnl3 jansson libwebsockets libpcap


#### Ubuntu  

Build dependencies:

    sudo apt-get install libnl-3-dev libnl-route-3-dev libnl-genl-3-dev libjansson-dev libwebsockets-dev libncurses5-dev libpcap-dev pkgconf

Run-time dependencies:

    sudo apt-get install libnl-3-200 libnl-route-3-200 libnl-genl-3-200 libjansson4 libwebsockets6

### Compiling JitterTrap

Fetch (with submodules for WebRTC support):

    git clone --recurse-submodules https://github.com/acooks/jittertrap.git

Or if you've already cloned:

    git submodule update --init --recursive

Build:

    cd jittertrap
    make

Run `make help` to see build configuration options, or `make config` to see current settings.

### WebRTC Video Playback

WebRTC video playback is enabled by default. JitterTrap bundles [libdatachannel](https://github.com/paullouisageneau/libdatachannel) 0.23+ which is built automatically on first compile.

**Supported codecs:** H.264 (all browsers), H.265/HEVC (Chrome 136+, Safari)

**Browser requirements:**
- Chrome, Firefox, Edge, Safari for H.264
- Chrome 136+, Safari, or Edge for H.265/HEVC

To disable WebRTC playback (reduces binary size and build dependencies):

    make ENABLE_WEBRTC_PLAYBACK=0

## Running JitterTrap

Basic usage:

    sudo ./server/jt-server --port 8080 --resource_path html5-client/output/

Then open http://localhost:8080/ in your browser.

### Command-line Options

| Option | Description |
|--------|-------------|
| `-p, --port PORT` | HTTP server port (default: 80) |
| `-r, --resource_path PATH` | Path to web UI files |
| `-a, --allowed IFACE` | Restrict to specific interface(s) (repeatable) |

Example with interface filtering:

    sudo ./server/jt-server -p 8080 -r html5-client/output/ --allowed eth0 --allowed eth1

### Requirements

JitterTrap requires root privileges (or `CAP_NET_ADMIN` capability) for:
- Packet capture on network interfaces
- Network impairment injection via tc/netem

## Contributing

Contributions are welcome! Please use [GitHub Issues](https://github.com/acooks/jittertrap/issues) for bug reports and [GitHub Discussions](https://github.com/acooks/jittertrap/discussions) for questions and feature suggestions.
