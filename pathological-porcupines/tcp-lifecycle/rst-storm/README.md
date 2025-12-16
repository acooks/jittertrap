# RST Storm Demonstration

## Overview

Demonstrates abrupt TCP connection termination using RST (Reset) packets instead of the normal FIN handshake. The server closes connections with SO_LINGER set to 0, which forces the kernel to send RST instead of FIN. The client detects these resets and tracks the pattern.

## Network Effect

- **Normal close**: FIN -> FIN+ACK -> ACK (graceful)
- **RST close**: RST (immediate, no handshake)
- **Result**: Connections terminate abruptly without completing data transfer

## JitterTrap Indicators

| Metric | Expected Value | Why |
|--------|---------------|-----|
| RST flag | Set on connections | Abrupt termination |
| FIN flag | Not set | No graceful close |
| Connection duration | Very short | Immediate close after accept |
| Flow count | Many short-lived flows | Rapid connect/disconnect |

## Root Cause

RST (Reset) packets are sent in these scenarios:

1. **SO_LINGER with timeout=0**: Application requests immediate close
2. **No listener**: Connection to port with no process listening
3. **Resource exhaustion**: Kernel can't maintain connection state
4. **Firewall/IDS**: Intrusion system terminates suspicious connection
5. **Application crash**: Process dies with open sockets
6. **Invalid state**: Packet arrives for unknown connection

**Why SO_LINGER(0) causes RST**:
- Normal close: FIN sent, wait for data drain, wait for FIN response
- LINGER(0): Skip drain, skip wait, send RST immediately
- Any unsent data in buffer is discarded

**When RST is used intentionally**:
- Connection pooling cleanup
- Timeout enforcement
- Abort misbehaving connections
- Fast resource cleanup under load

## Simulation Method

**Server**:
- Accepts connections on listen socket
- Sets SO_LINGER(on, 0) on accepted socket
- Closes socket immediately
- Kernel sends RST instead of FIN

**Client**:
- Connects at configurable rate (default 10/s)
- Attempts to receive data
- Catches ConnectionResetError
- Counts RST vs normal closes

## Usage

### Manual execution

```bash
# Terminal 1: Start server (in destination namespace)
sudo ip netns exec pp-dest python3 server.py --port 9999

# Terminal 2: Start client (in source namespace)
sudo ip netns exec pp-source python3 client.py --host 10.0.1.2 --port 9999
```

### With test runner

```bash
# Basic run
sudo ./infra/run-test.sh tcp-lifecycle/rst-storm

# Auto-start mode
sudo ./infra/run-test.sh tcp-lifecycle/rst-storm --auto
```

## Configuration Options

### Server options

| Argument | Default | Description |
|----------|---------|-------------|
| `--port` | 9999 | Listen port |
| `--duration` | 12 | Server duration in seconds |
| `--read-first` | false | Read data before sending RST |

### Client options

| Argument | Default | Description |
|----------|---------|-------------|
| `--host` | 10.0.1.2 | Server address |
| `--port` | 9999 | Server port |
| `--duration` | 10 | Test duration in seconds |
| `--rate` | 10 | Connections per second |
| `--send-data` | false | Send data before expecting RST |

## Variations

### High connection rate
```bash
python3 server.py &
python3 client.py --rate 100
```

### With data exchange
```bash
python3 server.py --read-first &
python3 client.py --send-data
```

### Slow rate for observation
```bash
python3 server.py &
python3 client.py --rate 2 --duration 30
```

### Stress test
```bash
python3 server.py --duration 60 &
python3 client.py --rate 500 --duration 30
```

## Self-Check Assertions

**Server verifies**:
1. Handled multiple connections (>10)
2. All connections ended with RST

**Client verifies**:
1. Made multiple connection attempts (>10)
2. Received at least one RST
3. Majority of connections ended with RST (>80%)

## RST vs FIN Comparison

| Aspect | FIN (Graceful) | RST (Abrupt) |
|--------|----------------|--------------|
| Handshake | 4-way | None |
| Data delivery | Guaranteed | Lost |
| TIME_WAIT | Yes (2*MSL) | No |
| Socket reuse | Delayed | Immediate |
| Error to app | EOF on read | ConnectionResetError |
| Resource cleanup | Gradual | Immediate |

## tcpdump Commands

Observe RST packets:

```bash
# In observer namespace - watch TCP flags
sudo ip netns exec pp-observer tcpdump -i br0 -n 'tcp port 9999' -v 2>&1 | grep -E 'Flags|\.R\.'

# Count RST packets
sudo ip netns exec pp-observer tcpdump -i br0 -n 'tcp[tcpflags] & tcp-rst != 0' -c 20

# Compare RST vs FIN
sudo ip netns exec pp-observer tcpdump -i br0 -n 'tcp port 9999 and (tcp[tcpflags] & (tcp-rst|tcp-fin) != 0)' -c 50

# Watch all connection lifecycle
sudo ip netns exec pp-observer tcpdump -i br0 -n 'tcp port 9999' -c 30
```

## Expected Output

### Server
```
RST Storm server listening on port 9999
Will close connections with RST (SO_LINGER 0)
Accepting connections for 12s...
Elapsed: 2.0s, Connections: 20 (10.0/s), RSTs sent: 20
Elapsed: 4.0s, Connections: 40 (10.0/s), RSTs sent: 40
...

RST STORM SERVER RESULTS
Duration: 10.2s
Total connections: 102
RST packets sent: 102
Connection rate: 10.0/s

Self-check results:
  [PASS] Connections handled: 102 connections
  [PASS] All RSTs sent: 102 RSTs = 102 connections
```

### Client
```
Connecting to 10.0.1.2:9999
Rate: 10.0 connections/second
Duration: 10.0s
Expecting RST responses from server...

Elapsed: 2.0s, Connections: 20 (10.0/s), RSTs: 20, Normal: 0
Elapsed: 4.0s, Connections: 40 (10.0/s), RSTs: 40, Normal: 0
...

RST STORM CLIENT RESULTS
Duration: 10.0s
Total connection attempts: 100
Successful connects: 100
Connection rate: 10.0/s

Connection outcomes:
  RST received: 100
  Normal close (FIN): 0
  Connection refused: 0
  Timeout: 0
  Other errors: 0

RST rate: 100.0% of successful connections

Self-check results:
  [PASS] Connections made: 100 attempts
  [PASS] RST received: 100 RSTs
  [PASS] RST dominant: 100/100 ended with RST
```

## Real-World RST Scenarios

| Scenario | Cause | Impact |
|----------|-------|--------|
| Load balancer health check timeout | LB closes idle connections | Clients see reset |
| Application crash | Process terminates | All connections reset |
| Firewall rule change | Connection no longer allowed | Immediate termination |
| SYN flood mitigation | Kernel drops SYN backlog | Appears as RST |
| Connection limit reached | Server at max connections | New connections reset |

## Security Considerations

RST can be used maliciously:

- **RST injection**: Attacker spoofs RST to terminate connections
- **RST DoS**: Flood of RST to disrupt legitimate connections
- **Side-channel**: RST timing reveals connection state

**Defenses**:
- TCP sequence number validation (RFC 5961)
- Challenge ACK for RST packets
- Encrypted transports (TLS) hide connection state

## References

- RFC 793 - Transmission Control Protocol
- RFC 1122 - Requirements for Internet Hosts
- RFC 5961 - Improving TCP's Robustness to Blind In-Window Attacks
- Stevens, W. R. "TCP/IP Illustrated, Volume 1" - Connection Termination
- "The TCP/IP Guide" - RST Segment
