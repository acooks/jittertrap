# Pathological Porcupines

Network application failure simulations for educational demonstrations and JitterTrap tool qualification testing. Uses Python 3.10 standard library only.

## Quick Start

```bash
# Create test topology (one-time setup)
sudo ./infra/setup-topology.sh

# Run a test - automatically opens browser to JitterTrap UI
sudo ./infra/run-test.sh tcp-timing/persist-timer

# Clean up when done
sudo ./infra/teardown-topology.sh
```

## Test Network Topology

```
┌─────────────┐      veth-src      ┌───────────────────┐      veth-dst      ┌─────────────┐
│   SOURCE    │◄──────────────────►│     OBSERVER      │◄──────────────────►│ DESTINATION │
│  pp-source  │                    │    pp-observer    │                    │   pp-dest   │
│  10.0.1.1   │                    │   br0 (bridge)    │                    │  10.0.1.2   │
│             │                    │   veth-mgmt       │                    │             │
│  sender.py  │                    │    (10.0.0.2)     │                    │ receiver.py │
│  client.py  │                    │   JitterTrap      │                    │ server.py   │
└─────────────┘                    └───────────────────┘                    └─────────────┘
                                            │
                                            │ veth-host (10.0.0.1)
                                            ▼
                                   ┌─────────────────┐
                                   │      HOST       │
                                   │  Browser →      │
                                   │  10.0.0.2:8080  │
                                   └─────────────────┘
```

- **Source (pp-source)**: Runs sender/client scripts (10.0.1.1)
- **Observer (pp-observer)**: Runs JitterTrap, bridges traffic (10.0.0.2 for UI)
- **Destination (pp-dest)**: Runs receiver/server scripts (10.0.1.2)

## Implemented Pathologies

| Category | Pathology | Description | JitterTrap Observable |
|----------|-----------|-------------|----------------------|
| TCP Flow Control | [receiver-starvation](tcp-flow-control/receiver-starvation/) | Slow receiver causes zero-window | Zero-window events |
| TCP Flow Control | [silly-window-syndrome](tcp-flow-control/silly-window-syndrome/) | Tiny segments from small windows | Small packet sizes |
| TCP Timing | [nagle-delayed-ack](tcp-timing/nagle-delayed-ack/) | 40-200ms latency from Nagle/delayed ACK | RTT histogram spikes |
| TCP Timing | [persist-timer](tcp-timing/persist-timer/) | Zero-window probes at exponential backoff | IPG gaps at 5s, 10s intervals |
| TCP Lifecycle | [rst-storm](tcp-lifecycle/rst-storm/) | Abrupt connection termination with RST | RST flags in flow details |
| UDP | [bursty-sender](udp/bursty-sender/) | Bimodal inter-packet gap distribution | IPG histogram with two peaks |
| RTP/Media | [rtp-jitter-spike](rtp/rtp-jitter-spike/) | Periodic large jitter in media stream | Jitter outliers >100ms |
| RTP/Media | [rtp-sequence-gap](rtp/rtp-sequence-gap/) | Packet loss via sequence discontinuities | seq_loss counter |

## Project Structure

```
pathological-porcupines/
    infra/                      # Test infrastructure
        setup-topology.sh       # Create 3-namespace topology
        teardown-topology.sh    # Remove topology
        run-test.sh             # Orchestrate test with JitterTrap
        add-impairment.sh       # Apply tc/netem impairments
        set-mtu.sh              # Configure MTU
        common.sh               # Shared functions
    common/                     # Shared Python utilities
        network.py              # Socket creation helpers
        timing.py               # Rate limiting, burst timers
        protocol.py             # RTP packet building/parsing
        logging_utils.py        # Logging setup
    tcp-flow-control/           # Window/buffer pathologies
    tcp-timing/                 # Timer-related issues
    tcp-lifecycle/              # Connection state issues
    tcp-congestion/             # Congestion control behaviors
    udp/                        # UDP pathologies
    rtp/                        # RTP/media stream issues
    ...
```

## Requirements

- Python 3.10+ (standard library only, no pip install needed)
- Linux with network namespace support
- Root/sudo for namespace and network configuration
- JitterTrap for visualization

## Infrastructure Scripts

| Script | Purpose |
|--------|---------|
| `infra/setup-topology.sh` | Create namespaces, veth pairs, and L2 bridge |
| `infra/teardown-topology.sh` | Remove all namespaces and interfaces |
| `infra/run-test.sh <path>` | Run a test with JitterTrap orchestration |
| `infra/add-impairment.sh <profile>` | Apply network impairment (wan, lossy, etc.) |
| `infra/set-mtu.sh <mtu>` | Set MTU on test interfaces |
| `infra/cleanup-processes.sh` | Kill orphaned test processes (tcpdump, jt-server, python) |

### Impairment Profiles

```bash
# Apply WAN-like delay
sudo ./infra/add-impairment.sh wan

# Apply packet loss
sudo ./infra/add-impairment.sh lossy

# Custom impairment
sudo ./infra/add-impairment.sh custom delay 100ms loss 2%

# Clear impairments
sudo ./infra/add-impairment.sh clean
```

## Running Tests

### With Test Runner (Recommended)

The test runner handles JitterTrap startup, waits for you to connect, then runs the test:

```bash
# Basic execution - opens browser, prompts before starting test
sudo ./infra/run-test.sh tcp-timing/persist-timer

# Auto-start mode (no prompt, starts after 5s)
sudo ./infra/run-test.sh tcp-timing/persist-timer --auto

# With network impairment
sudo ./infra/run-test.sh udp/bursty-sender --impairment wan

# Skip JitterTrap (for debugging)
sudo ./infra/run-test.sh rtp/rtp-jitter-spike --no-jittertrap

# Don't auto-open browser (just print URL)
sudo ./infra/run-test.sh tcp-timing/persist-timer --no-browser

# Reset network config after test
sudo ./infra/run-test.sh tcp-lifecycle/rst-storm --reset
```

### Manual Execution

For more control, run components separately in different terminals:

```bash
# Terminal 1: Start JitterTrap in observer namespace
sudo ip netns exec pp-observer jt-server --allowed veth-src:veth-dst -p 8080
# Open http://10.0.0.2:8080 in browser

# Terminal 2: Start server/receiver in destination namespace
sudo ip netns exec pp-dest python3 tcp-timing/persist-timer/server.py --port 9999

# Terminal 3: Start client/sender in source namespace
sudo ip netns exec pp-source python3 tcp-timing/persist-timer/client.py --host 10.0.1.2 --port 9999
```

Each pathology directory contains:
- `README.md` - Detailed explanation, usage, and expected output
- `server.py` or `receiver.py` - Destination component
- `client.py` or `sender.py` - Source component

## Observing with JitterTrap

1. Create topology: `sudo ./infra/setup-topology.sh`
2. Start JitterTrap in observer namespace:
   ```bash
   sudo ip netns exec pp-observer jt-server --allowed veth-src:veth-dst -p 8080
   ```
3. Open http://10.0.0.2:8080 in browser
4. Select observation interface (veth-src or veth-dst)
5. Run a pathology test
6. Watch metrics:
   - **IPG histogram** - Inter-packet gap distribution
   - **Jitter** - RFC 3550 jitter calculation
   - **Window size** - TCP flow control
   - **Packet size** - Fragmentation and SWS
   - **Flags** - RST, FIN, zero-window events

## Self-Checking Tests

All tests include self-check assertions that verify the expected pathology occurred:

```
Self-check results:
  [PASS] Zero-window detected: 2 block event(s)
  [PASS] Persist timer triggered: 2 block(s) >= 4s
  [PASS] Data sent: 98.5 KB
```

Exit codes: 0 = pass, 1 = fail

## Troubleshooting

### Cleaning Up Orphaned Processes

If tests are interrupted or crash, processes may be left running. Use the cleanup script:

```bash
# List orphaned processes (dry run)
sudo ./infra/cleanup-processes.sh --list

# Kill orphaned processes
sudo ./infra/cleanup-processes.sh
```

The cleanup script finds and kills:
- `tcpdump` processes in the observer namespace
- `jt-server` processes in the observer namespace
- Python test processes in source/dest namespaces
- Any `jt-server` running outside namespaces

### Port Conflicts

If you see "Address already in use" errors:
```bash
# Check what's using the port
sudo ss -tlnp | grep :9999

# Clean up stale processes
sudo ./infra/cleanup-processes.sh
```

### Namespace Issues

If namespace operations fail:
```bash
# Remove all test infrastructure
sudo ./infra/teardown-topology.sh

# Recreate fresh
sudo ./infra/setup-topology.sh
```

## Documentation

See [pathological-porcupines.md](../pathological-porcupines.md) for the complete
catalog of all 61 planned pathologies across 12 categories.

## License

Part of JitterTrap - see main project for license.
