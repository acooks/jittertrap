# JitterTrap Issues Found During Testing

Issues discovered while running pathological-porcupines tests.

## TCP Zero-Window Detection

**Test:** `tcp-timing/persist-timer`
**Date:** 2024-12-16

### Issue 1: Zero Window indicator not appearing

**Observed:** The "Zero Window" triangle marker (shown in the TCP Advertised Window chart legend) does not appear during the zero-window condition, even though the window clearly drops to zero.

**Expected:** Zero Window indicator should mark the point(s) where zero-window was advertised.

**Status:** Needs investigation

---

### Issue 2: TCP Window size doesn't visually recover

**Observed:** After the zero-window stall ends and traffic resumes (visible in throughput chart), the TCP Advertised Window chart remains at/near zero instead of showing the window reopening.

**Expected:** Window should visually recover to a non-zero value when the receiver resumes reading and advertises available buffer space.

**Possible causes:**
- Display artifact (log scale making small values look like zero)
- Test ends too quickly after recovery
- Receiver buffer remains nearly full
- Chart not updating correctly after extended zero-window period

**Status:** Needs investigation

---

### Issue 3: Histogram measurement window unclear

**Observed:** The IPG and PPS distribution histograms show a more concentrated distribution than expected for a flow with a multi-second interruption. It's unclear what time window the histograms cover.

**Expected:** Clear documentation or UI indication of:
- What time window the histograms cover (full flow lifetime vs sliding window)
- How gaps with no packets are handled in IPG calculation
- Whether the histogram resets or accumulates over time

**Impact:** Makes it difficult to validate that the tool is correctly measuring flow characteristics during pathological conditions.

**Status:** Needs investigation

---

## RST-Terminated Flows Missing from TCP Charts

**Test:** `tcp-lifecycle/rst-storm`
**Date:** 2024-12-16

### Issue: TCP RTT and Window charts empty for RST flows

**Observed:** RST-terminated connections appear in the Top Flows list but the TCP Round-Trip Time and TCP Advertised Window charts show no data points.

**Expected:** Some indication of RST flows in TCP charts, or at minimum RST markers.

**Cause:** RST connections are terminated immediately after accept (SO_LINGER 0), before:
- Any data exchange occurs (no RTT samples)
- Window advertisements are captured
- Meaningful TCP state is established

**Impact:** RST storms are only observable via:
- Top Flows table (shows many short-lived flows)
- Throughput chart (brief bursts)
- Flow count

**Workaround:** Use tcpdump to observe RST packets directly:
```bash
sudo ip netns exec pp-observer tcpdump -i br0 'tcp[tcpflags] & tcp-rst != 0'
```

**Status:** Known limitation - RST flows too short-lived for TCP metrics

---

## RTP Detection Requires Standard Ports

**Test:** `rtp/rtp-sequence-gap`, `rtp/rtp-jitter-spike`
**Date:** 2024-12-16

### Issue: RTP flows shown as generic UDP

**Observed:** RTP test traffic on port 9999 was classified as "UDP" instead of "RTP" in JitterTrap flow table. No RTP-specific metrics (sequence tracking, jitter calculation) were applied.

**Expected:** JitterTrap should detect RTP traffic and show RTP-specific metrics.

**Cause:** JitterTrap likely uses port-based heuristics to detect RTP traffic. Port 9999 is not a standard RTP port.

**Fix:** Changed RTP tests to use port 5004 (standard RTP data port per RFC 3551).

**Note:** If JitterTrap still doesn't detect RTP, it may require:
- Deep packet inspection of RTP headers
- Configuration to specify which ports/flows are RTP
- Standard RTP port range (even ports 16384-32767)

**Status:** Port changed to 5004 - retest needed

---

## Notes

These issues were discovered using the `tcp-timing/persist-timer` test which creates a TCP flow with:
- 5 seconds normal traffic
- 5 seconds zero-window stall (receiver stops reading)
- Recovery when receiver resumes reading

The test correctly demonstrates the pathology, but JitterTrap's visualization of these conditions needs review.
