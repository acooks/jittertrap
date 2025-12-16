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

## Notes

These issues were discovered using the `tcp-timing/persist-timer` test which creates a TCP flow with:
- 5 seconds normal traffic
- 5 seconds zero-window stall (receiver stops reading)
- Recovery when receiver resumes reading

The test correctly demonstrates the pathology, but JitterTrap's visualization of these conditions needs review.
