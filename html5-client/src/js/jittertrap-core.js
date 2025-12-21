/* jittertrap-core.js */

/* global CBuffer */
/* global JT:true */

((my) => {
  'use strict';

  /* module namespace */
  my.core = {};

  let samplePeriod = JT.coreconfig.samplePeriod;

  /* data sample period; microseconds; fixed (backend sampling rate). */
  my.core.samplePeriod = function(sp) {
    if (sp) {
      console.log("sample period set to " + sp + " microseconds");
      samplePeriod = sp;
    }
    return samplePeriod;
  };

  /* number of samples to keep for a complete chart series. */
  const sampleWindowSize = 200;
  /* number of data samples. */
  /* FIXME: see about replacing sampleCount with sampleWindowSize */
  const sampleCount = sampleWindowSize;

  my.core.sampleCount = function () {
    return sampleCount;
  };

  /* count is bytes/sec (from server) */
  const byteCountToBpsRate = function(count) {
    // count is Bps. Multiply by 8 to get bps.
    const rate = count * 8.0;
    return rate;
  };

  /* count is packets/sec (from server) */
  const packetDeltaToPpsRate = function(count) {
    // count is Pps. No conversion needed.
    const rate = count;
    return rate;
  };

  const timeScaleTable = { "5ms": 5, "10ms": 10, "20ms": 20, "50ms": 50, "100ms": 100, "200ms": 200, "500ms": 500, "1000ms": 1000};

  /* Interval refinement mapping for smooth chart animation.
   * When viewing at a slower interval (e.g., 100ms), use a faster interval
   * (e.g., 20ms) for provisional data points that fill gaps between authoritative
   * points. This creates smooth scrolling animation while preserving correct
   * decimation when authoritative data arrives.
   * Key: authoritative interval (ms), Value: refinement interval (ms) */
  const intervalRefinement = {
    100: 20,    // 5 provisional points per authoritative
    50: 10,     // 5 provisional points
    200: 50,    // 4 provisional points
    500: 100,   // 5 provisional points
    1000: 200   // 5 provisional points
  };

  /* TypedRingBuffer - a circular buffer using Float64Arrays for GC-free numeric storage.
   * Stores pairs of (timestamp, value) without creating JS objects.
   * API is compatible with CBuffer where possible. */
  const TypedRingBuffer2 = function(capacity) {
    this.timestamps = new Float64Array(capacity);
    this.values = new Float64Array(capacity);
    this.capacity = capacity;
    this.size = 0;
    this.head = 0;  // Next write position
  };

  TypedRingBuffer2.prototype = {
    push: function(timestamp, value) {
      this.timestamps[this.head] = timestamp;
      this.values[this.head] = value;
      this.head = (this.head + 1) % this.capacity;
      if (this.size < this.capacity) {
        this.size++;
      }
    },

    // Convert logical index to physical array index
    _idx: function(i) {
      const start = (this.head - this.size + this.capacity) % this.capacity;
      return (start + i) % this.capacity;
    },

    // Get item at logical index (0 = oldest) - allocates object
    get: function(i) {
      if (i < 0 || i >= this.size) return null;
      const idx = this._idx(i);
      return { timestamp: this.timestamps[idx], value: this.values[idx] };
    },

    // Zero-allocation accessors for individual fields
    timestampAt: function(i) {
      return this.timestamps[this._idx(i)];
    },

    valueAt: function(i) {
      return this.values[this._idx(i)];
    },

    // Get the last (most recent) item - allocates object
    last: function() {
      if (this.size === 0) return null;
      const idx = (this.head - 1 + this.capacity) % this.capacity;
      return { timestamp: this.timestamps[idx], value: this.values[idx] };
    },

    // Zero-allocation accessors for last item
    lastTimestamp: function() {
      if (this.size === 0) return 0;
      return this.timestamps[(this.head - 1 + this.capacity) % this.capacity];
    },

    lastValue: function() {
      if (this.size === 0) return 0;
      return this.values[(this.head - 1 + this.capacity) % this.capacity];
    },

    empty: function() {
      this.size = 0;
      this.head = 0;
    },

    // For iteration without allocation - call with callback(timestamp, value, index)
    forEach: function(callback) {
      const start = (this.head - this.size + this.capacity) % this.capacity;
      for (let i = 0; i < this.size; i++) {
        const idx = (start + i) % this.capacity;
        callback(this.timestamps[idx], this.values[idx], i);
      }
    },

    // Self-test: verify zero-allocation accessors match allocating ones
    _selfTest: function() {
      for (let i = 0; i < this.size; i++) {
        const obj = this.get(i);
        if (obj.timestamp !== this.timestampAt(i)) {
          throw new Error('TypedRingBuffer2: timestampAt mismatch at ' + i);
        }
        if (obj.value !== this.valueAt(i)) {
          throw new Error('TypedRingBuffer2: valueAt mismatch at ' + i);
        }
      }
      if (this.size > 0) {
        const last = this.last();
        if (last.timestamp !== this.lastTimestamp()) {
          throw new Error('TypedRingBuffer2: lastTimestamp mismatch');
        }
        if (last.value !== this.lastValue()) {
          throw new Error('TypedRingBuffer2: lastValue mismatch');
        }
      }
      return true;
    }
  };

  /* TypedRingBuffer4 - for pgap data with 4 values per entry (timestamp, min, max, mean) */
  const TypedRingBuffer4 = function(capacity) {
    this.timestamps = new Float64Array(capacity);
    this.mins = new Float64Array(capacity);
    this.maxs = new Float64Array(capacity);
    this.means = new Float64Array(capacity);
    this.capacity = capacity;
    this.size = 0;
    this.head = 0;
  };

  TypedRingBuffer4.prototype = {
    push: function(timestamp, min, max, mean) {
      this.timestamps[this.head] = timestamp;
      this.mins[this.head] = min;
      this.maxs[this.head] = max;
      this.means[this.head] = mean;
      this.head = (this.head + 1) % this.capacity;
      if (this.size < this.capacity) {
        this.size++;
      }
    },

    // Convert logical index to physical array index
    _idx: function(i) {
      const start = (this.head - this.size + this.capacity) % this.capacity;
      return (start + i) % this.capacity;
    },

    // Get item at logical index (0 = oldest) - allocates object
    get: function(i) {
      if (i < 0 || i >= this.size) return null;
      const idx = this._idx(i);
      return {
        timestamp: this.timestamps[idx],
        min: this.mins[idx],
        max: this.maxs[idx],
        mean: this.means[idx]
      };
    },

    // Zero-allocation accessors for individual fields
    timestampAt: function(i) {
      return this.timestamps[this._idx(i)];
    },

    minAt: function(i) {
      return this.mins[this._idx(i)];
    },

    maxAt: function(i) {
      return this.maxs[this._idx(i)];
    },

    meanAt: function(i) {
      return this.means[this._idx(i)];
    },

    // Get the last (most recent) item - allocates object
    last: function() {
      if (this.size === 0) return null;
      const idx = (this.head - 1 + this.capacity) % this.capacity;
      return {
        timestamp: this.timestamps[idx],
        min: this.mins[idx],
        max: this.maxs[idx],
        mean: this.means[idx]
      };
    },

    // Zero-allocation accessors for last item
    lastTimestamp: function() {
      if (this.size === 0) return 0;
      return this.timestamps[(this.head - 1 + this.capacity) % this.capacity];
    },

    lastMin: function() {
      if (this.size === 0) return 0;
      return this.mins[(this.head - 1 + this.capacity) % this.capacity];
    },

    lastMax: function() {
      if (this.size === 0) return 0;
      return this.maxs[(this.head - 1 + this.capacity) % this.capacity];
    },

    lastMean: function() {
      if (this.size === 0) return 0;
      return this.means[(this.head - 1 + this.capacity) % this.capacity];
    },

    empty: function() {
      this.size = 0;
      this.head = 0;
    },

    // For iteration without allocation - call with callback(timestamp, min, max, mean, index)
    forEach: function(callback) {
      const start = (this.head - this.size + this.capacity) % this.capacity;
      for (let i = 0; i < this.size; i++) {
        const idx = (start + i) % this.capacity;
        callback(this.timestamps[idx], this.mins[idx], this.maxs[idx], this.means[idx], i);
      }
    },

    // Self-test: verify zero-allocation accessors match allocating ones
    _selfTest: function() {
      for (let i = 0; i < this.size; i++) {
        const obj = this.get(i);
        if (obj.timestamp !== this.timestampAt(i)) {
          throw new Error('TypedRingBuffer4: timestampAt mismatch at ' + i);
        }
        if (obj.min !== this.minAt(i)) {
          throw new Error('TypedRingBuffer4: minAt mismatch at ' + i);
        }
        if (obj.max !== this.maxAt(i)) {
          throw new Error('TypedRingBuffer4: maxAt mismatch at ' + i);
        }
        if (obj.mean !== this.meanAt(i)) {
          throw new Error('TypedRingBuffer4: meanAt mismatch at ' + i);
        }
      }
      if (this.size > 0) {
        const last = this.last();
        if (last.timestamp !== this.lastTimestamp()) {
          throw new Error('TypedRingBuffer4: lastTimestamp mismatch');
        }
        if (last.min !== this.lastMin()) {
          throw new Error('TypedRingBuffer4: lastMin mismatch');
        }
        if (last.max !== this.lastMax()) {
          throw new Error('TypedRingBuffer4: lastMax mismatch');
        }
        if (last.mean !== this.lastMean()) {
          throw new Error('TypedRingBuffer4: lastMean mismatch');
        }
      }
      return true;
    }
  };

  // Run self-tests on TypedRingBuffer implementations
  // Tests empty, partial, full, and wrapped buffer states
  const runRingBufferSelfTests = function() {
    const cap = 5;

    // Test TypedRingBuffer2
    const rb2 = new TypedRingBuffer2(cap);

    // Empty state
    if (rb2.lastTimestamp() !== 0 || rb2.lastValue() !== 0) {
      throw new Error('TypedRingBuffer2: empty state failed');
    }

    // Partial fill (3 items in capacity 5)
    rb2.push(1.0, 100);
    rb2.push(2.0, 200);
    rb2.push(3.0, 300);
    rb2._selfTest();

    // Full (5 items)
    rb2.push(4.0, 400);
    rb2.push(5.0, 500);
    rb2._selfTest();

    // Wrapped (7 items pushed into capacity 5)
    rb2.push(6.0, 600);
    rb2.push(7.0, 700);
    rb2._selfTest();

    // Test TypedRingBuffer4
    const rb4 = new TypedRingBuffer4(cap);

    // Empty state
    if (rb4.lastTimestamp() !== 0 || rb4.lastMin() !== 0 ||
        rb4.lastMax() !== 0 || rb4.lastMean() !== 0) {
      throw new Error('TypedRingBuffer4: empty state failed');
    }

    // Partial fill
    rb4.push(1.0, 10, 100, 50);
    rb4.push(2.0, 20, 200, 100);
    rb4.push(3.0, 30, 300, 150);
    rb4._selfTest();

    // Full
    rb4.push(4.0, 40, 400, 200);
    rb4.push(5.0, 50, 500, 250);
    rb4._selfTest();

    // Wrapped
    rb4.push(6.0, 60, 600, 300);
    rb4.push(7.0, 70, 700, 350);
    rb4._selfTest();

    console.log('TypedRingBuffer self-tests passed');
    return true;
  };

  // Expose for manual testing: JT.core.runRingBufferSelfTests()
  // Not run automatically to avoid creating garbage at startup
  my.core.runRingBufferSelfTests = runRingBufferSelfTests;

  /* a prototype object to encapsulate timeseries data. */
  const Series = function(name, title, ylabel, rateFormatter) {
    this.name = name;
    this.title = title;
    this.ylabel = ylabel;
    this.rateFormatter = rateFormatter;
    this.xlabel = "Time (s)";
    this.stats = {min: 99999, max:0, median:0, mean:0, maxPG:0, meanPG:0 };
    this.samples = {};
    this.pgaps = {};
    for (const ts in timeScaleTable) {
      this.samples[ts] = new TypedRingBuffer2(sampleWindowSize);
      this.pgaps[ts] = new TypedRingBuffer4(sampleWindowSize);
    }
 };


  const sBin = {};  // a container (Bin) for series.
  sBin.rxRate = new Series("rxRate",
                           "Ingress Bitrate (bps)",
                           "Bitrate",
                           byteCountToBpsRate);

  sBin.txRate = new Series("txRate",
                           "Egress Bitrate (bps)",
                           "Bitrate",
                           byteCountToBpsRate);

  sBin.txPacketRate = new Series("txPacketRate",
                                 "Egress Packet Rate",
                                 "Packet Rate",
                                 packetDeltaToPpsRate);

  sBin.rxPacketRate = new Series("rxPacketRate",
                                 "Ingress Packet Rate",
                                 "Packet Rate",
                                 packetDeltaToPpsRate);

  let selectedSeriesName = "rxRate";

  my.core.setSelectedSeriesName = function(sName) {
    selectedSeriesName = sName;
  };

  my.core.getSelectedSeries = function () {
    return sBin[selectedSeriesName];
  };

  const resizeCBuf = function(series, len) {

    if (len === sampleCount) {
      return;
    }

    // For typed ring buffers, we just create new ones (data will be lost on resize)
    // This is consistent with the original behavior for pgaps
    for (const key in timeScaleTable) {
      series.samples[key] = new TypedRingBuffer2(len);
      series.pgaps[key] = new TypedRingBuffer4(len);
    }
  };

  my.core.resizeDataBufs = function(newlen) {

    resizeCBuf(sBin.rxRate, newlen);
    resizeCBuf(sBin.txRate, newlen);

    resizeCBuf(sBin.txPacketRate, newlen);
    resizeCBuf(sBin.rxPacketRate, newlen);

  };

  const clearSeries = function (s) {

    for (const key in timeScaleTable) {
      s.samples[key].empty();
      s.pgaps[key].empty();
    }

  };

  my.core.clearAllSeries = function () {
    clearSeries(sBin.txRate);
    clearSeries(sBin.rxRate);
    clearSeries(sBin.txPacketRate);
    clearSeries(sBin.rxPacketRate);

    clearFlows();
  };

  const numSort = function(a,b) {
    return (a - b)|0;
  };

  const updateBasicStatsChartData = function (stats, chartSeries) {
    if (chartSeries[0]) {
      chartSeries[0].y = stats.min;
      chartSeries[1].y = stats.median;
      chartSeries[2].y = stats.mean;
      chartSeries[3].y = stats.max;
    } else {
      chartSeries.push({x:1, y:stats.min, label:"Min"});
      chartSeries.push({x:2, y:stats.median, label:"Median"});
      chartSeries.push({x:3, y:stats.mean, label:"Mean"});
      chartSeries.push({x:4, y:stats.max, label:"Max"});
    }
  };

  const updatePacketGapChartData = function (data, mean, minMax) {
    const len = data.size;

    // Reuse existing objects if array already has them, otherwise create new ones
    // This avoids allocating ~400 objects per update (200 mean + 200 minMax)
    for (let i = 0; i < len; i++) {
      const x = data.timestampAt(i);
      if (i < mean.length) {
        // Reuse existing object
        mean[i].x = x;
        mean[i].y = data.meanAt(i);
        minMax[i].x = x;
        minMax[i].y[0] = data.minAt(i);
        minMax[i].y[1] = data.maxAt(i);
      } else {
        // Create new object (only happens during initial fill)
        mean.push({x: x, y: data.meanAt(i)});
        minMax.push({x: x, y: [data.minAt(i), data.maxAt(i)]});
      }
    }
    // Trim arrays if data shrunk (unlikely but safe)
    mean.length = len;
    minMax.length = len;
  };

  // Pre-allocated buffer for stats calculation to avoid slice()/map() allocations
  const statsSortBuffer = new Float64Array(sampleWindowSize);

  const updateStats = function (series, timeScale) {
    const samples = series.samples[timeScale];
    const len = samples.size;

    if (len === 0) return;

    // Copy values using zero-allocation accessor
    for (let i = 0; i < len; i++) {
      statsSortBuffer[i] = samples.valueAt(i);
    }

    series.stats.cur = statsSortBuffer[len - 1];

    // Sort just the portion we're using (Float64Array.sort is in-place)
    const slice = statsSortBuffer.subarray(0, len);
    slice.sort();

    series.stats.max = slice[len - 1];
    series.stats.min = slice[0];
    series.stats.median = slice[Math.floor(len / 2)];

    let sum = 0;
    for (let i = 0; i < len; i++) {
      sum += slice[i];
    }
    series.stats.mean = sum / len;

    // Use zero-allocation accessors for pgaps
    const pgaps = series.pgaps[timeScale];
    if (pgaps.size > 0) {
      series.stats.maxPG = pgaps.lastMax();
      series.stats.meanPG = pgaps.lastMean();
    }
  };

  const updateMainChartData = function(samples, chartSeries) {
    const len = samples.size;

    // Reuse existing objects if array already has them, otherwise create new ones
    // This avoids allocating ~200 objects per update
    for (let i = 0; i < len; i++) {
      if (i < chartSeries.length) {
        // Reuse existing object
        chartSeries[i].timestamp = samples.timestampAt(i);
        chartSeries[i].value = samples.valueAt(i);
      } else {
        // Create new object (only happens during initial fill)
        chartSeries.push({
          timestamp: samples.timestampAt(i),
          value: samples.valueAt(i)
        });
      }
    }
    // Trim array if data shrunk (unlikely but safe)
    chartSeries.length = len;
  };

  const chartSamples = {};

  const updateSampleCounts = function(interval) {
      if (!chartSamples[interval]) chartSamples[interval] = 1;
      else if (chartSamples[interval] < sampleCount) chartSamples[interval]++;
  };

  /* Object pool for data points to reduce GC pressure.
   * Instead of creating ~4000 new objects per update (20 flows × 200 samples),
   * we reuse pre-allocated objects from this pool. */
  const DATA_POINT_POOL_SIZE = sampleWindowSize * 50;  // 200 samples × 50 flows max
  const dataPointPool = [];
  let dataPointPoolIndex = 0;

  // Initialize data point pool once
  for (let i = 0; i < DATA_POINT_POOL_SIZE; i++) {
    dataPointPool.push({
      ts: 0, bytes: 0, packets: 0,
      rtt_us: -1, tcp_state: -1,
      rwnd_bytes: -1, window_scale: -1,
      zero_window_cnt: 0, dup_ack_cnt: 0, retransmit_cnt: 0,
      ece_cnt: 0, recent_events: 0
    });
  }

  /* Reset a pooled data point to default values */
  const resetDataPoint = function(d, ts) {
    d.ts = ts;
    d.bytes = 0;
    d.packets = 0;
    d.rtt_us = -1;
    d.tcp_state = -1;
    d.rwnd_bytes = -1;
    d.window_scale = -1;
    d.zero_window_cnt = 0;
    d.dup_ack_cnt = 0;
    d.retransmit_cnt = 0;
    d.ece_cnt = 0;
    d.recent_events = 0;
    return d;
  };

  /* Get a pooled data point object, resetting it to defaults */
  const getPooledDataPoint = function(ts) {
    if (dataPointPoolIndex >= DATA_POINT_POOL_SIZE) {
      // Pool exhausted - create new object (fallback, shouldn't happen often)
      return resetDataPoint({}, ts);
    }
    const obj = dataPointPool[dataPointPoolIndex++];
    return resetDataPoint(obj, ts);
  };

  /* Flow object cache to reuse flow objects across updates.
   * Keyed by fkey, these objects persist and get their values array reused. */
  const flowObjectCache = new Map();
  const DEFAULT_RTT_HIST = [0,0,0,0,0,0,0,0,0,0,0,0,0,0];
  const DEFAULT_IPG_HIST = [0,0,0,0,0,0,0,0,0,0,0,0];

  /* Get or create a flow object from the cache */
  const getOrCreateFlow = function(fkey) {
    let flow = flowObjectCache.get(fkey);
    if (!flow) {
      flow = {
        fkey: fkey,
        values: [],
        tbytes: 0, tpackets: 0,
        rtt_us: -1, tcp_state: -1, saw_syn: 0,
        video_type: 0, video_codec: 0, video_jitter_us: 0,
        video_seq_loss: 0, video_cc_errors: 0, video_ssrc: 0,
        video_codec_source: 0, video_width: 0, video_height: 0,
        video_profile: 0, video_level: 0, video_fps_x100: 0,
        video_bitrate_kbps: 0, video_gop_frames: 0, video_keyframes: 0, video_frames: 0,
        audio_type: 0, audio_codec: 0, audio_jitter_us: 0,
        audio_seq_loss: 0, audio_ssrc: 0, audio_bitrate_kbps: 0,
        health_rtt_hist: DEFAULT_RTT_HIST.slice(),
        health_rtt_samples: 0, health_status: 0, health_flags: 0,
        ipg_hist: DEFAULT_IPG_HIST.slice(),
        ipg_samples: 0, ipg_mean_us: 0,
        frame_size_hist: null, frame_size_samples: 0, frame_size_mean: 0,
        frame_size_variance: 0, frame_size_min: 0, frame_size_max: 0,
        pps_hist: null, pps_samples: 0, pps_mean: 0, pps_variance: 0
      };
      flowObjectCache.set(fkey, flow);
    }
    // Clear the values array for reuse (much faster than creating new array)
    flow.values.length = 0;
    return flow;
  };

  /* Reset the data point pool index at start of each update cycle */
  const resetDataPointPool = function() {
    dataPointPoolIndex = 0;
  };

  /* Extract flow identity from a key (everything after the interval prefix).
   * Used for frontier tracking (same flow has different keys at different intervals).
   * Cached to avoid repeated substring allocations (reduces GC pressure). */
  const flowIdentityCache = new Map();
  const getFlowIdentity = function(fkey) {
    let identity = flowIdentityCache.get(fkey);
    if (identity === undefined) {
      const firstSlash = fkey.indexOf('/');
      identity = fkey.substring(firstSlash + 1);
      flowIdentityCache.set(fkey, identity);
    }
    return identity;
  };

  /* Translate a flow key from one interval to another.
   * fkey format: interval/src/sport/dst/dport/proto/tclass */
  const translateFlowKey = function(fkey, toInterval) {
    const firstSlash = fkey.indexOf('/');
    return toInterval + fkey.substring(firstSlash);
  };

  const updateTopFlowChartData = function(interval) {
    const chartPeriod = my.charts.getChartPeriod();
    const chartSeries = JT.charts.getTopFlowsRef();
    const intervalMs = interval / 1E6;

    updateSampleCounts(interval);

    console.assert(Number(chartPeriod) > 0);
    console.assert(Number(intervalMs) > 0);

    // Determine if this interval is authoritative (matches chartPeriod) or
    // refinement (faster interval for smooth animation)
    const isAuthoritative = (Number(chartPeriod) === Number(intervalMs));
    const expectedRefinement = intervalRefinement[Number(chartPeriod)];
    const isRefinement = (expectedRefinement === Number(intervalMs));

    if (!isAuthoritative && !isRefinement) {
      // Neither authoritative nor refinement for current view - ignore
      return;
    }

    // Determine the authoritative interval (for flow ranking and totals)
    const authIntervalNs = Number(chartPeriod) * 1E6;
    const refinementIntervalNs = expectedRefinement ? expectedRefinement * 1E6 : null;

    // Must have authoritative data to proceed (need flowRank)
    if (!flowRank[authIntervalNs] || flowRank[authIntervalNs].length === 0) {
      return;
    }

    // Update frontier when authoritative data arrives
    // Only advance forward (monotonic) to prevent jitter from out-of-order messages
    if (isAuthoritative && flowsTS[authIntervalNs] && flowsTS[authIntervalNs].size > 0) {
      const latestSlice = flowsTS[authIntervalNs].get(flowsTS[authIntervalNs].size - 1);
      const latestTs = latestSlice.ts;
      if (latestTs > currentChartTime) {
        currentChartTime = latestTs;
      }
      // Update frontier for all flows in the latest slice
      for (const key in latestSlice) {
        if (key === 'ts') continue;
        const flowId = getFlowIdentity(key);
        authoritativeFrontier.set(flowId, latestTs);
      }
    }

    // Update current time from refinement data for smooth X-axis scrolling
    // Only advance forward (monotonic) to prevent jitter from out-of-order messages
    if (isRefinement && refinementIntervalNs && flowsTS[refinementIntervalNs] &&
        flowsTS[refinementIntervalNs].size > 0) {
      const latestRefSlice = flowsTS[refinementIntervalNs].get(flowsTS[refinementIntervalNs].size - 1);
      if (latestRefSlice.ts > currentChartTime) {
        currentChartTime = latestRefSlice.ts;
      }
    }

    const fcount = flowRank[authIntervalNs].length;
    chartSeries.length = 0;
    resetDataPointPool();  // Reset pool index at start of each update cycle

    const authSlices = flowsTS[authIntervalNs] ? flowsTS[authIntervalNs].size : 0;

    /* get the top flows from the authoritative ranking... */
    for (let j = 0; j < fcount; j++) {
      const fkey = flowRank[authIntervalNs][j];
      const flowId = getFlowIdentity(fkey);  // For frontier lookup and refinement key translation
      const frontier = authoritativeFrontier.get(flowId) || 0;
      const flow = getOrCreateFlow(fkey);  // Reuse cached flow object
      let lastRtt = -1;  /* Track latest RTT for this flow */
      let lastTcpState = -1;  /* Track latest TCP state for this flow */
      let sawSyn = 0;  /* Track if SYN was ever observed for this flow */
      /* Video stream tracking */
      let lastVideoType = 0;
      let lastVideoCodec = 0;
      let lastVideoJitterUs = 0;
      let lastVideoSeqLoss = 0;
      let lastVideoCcErrors = 0;
      let lastVideoSsrc = 0;
      /* Extended video telemetry tracking */
      let lastVideoCodecSource = 0;
      let lastVideoWidth = 0;
      let lastVideoHeight = 0;
      let lastVideoProfile = 0;
      let lastVideoLevel = 0;
      let lastVideoFpsX100 = 0;
      let lastVideoBitrateKbps = 0;
      let lastVideoGopFrames = 0;
      let lastVideoKeyframes = 0;
      let lastVideoFrames = 0;
      /* Audio stream tracking */
      let lastAudioType = 0;
      let lastAudioCodec = 0;
      let lastAudioJitterUs = 0;
      let lastAudioSeqLoss = 0;
      let lastAudioSsrc = 0;
      let lastAudioBitrateKbps = 0;
      /* TCP health tracking - use pre-defined constant to avoid allocations */
      let lastHealthRttHist = DEFAULT_RTT_HIST;
      let lastHealthRttSamples = 0;
      let lastHealthStatus = 0;
      let lastHealthFlags = 0;
      /* IPG histogram tracking (all flows) - use pre-defined constant to avoid allocations */
      let lastIpgHist = DEFAULT_IPG_HIST;
      let lastIpgSamples = 0;
      let lastIpgMeanUs = 0;
      /* Frame size histogram tracking (all flows) */
      let lastFrameSizeHist = null;
      let lastFrameSizeSamples = 0;
      let lastFrameSizeMean = 0;
      let lastFrameSizeVariance = 0;
      let lastFrameSizeMin = 0;
      let lastFrameSizeMax = 0;
      /* PPS histogram tracking (all flows) */
      let lastPpsHist = null;
      let lastPpsSamples = 0;
      let lastPpsMean = 0;
      let lastPpsVariance = 0;
      // Process authoritative data points (all of them)
      for (let i = 0; i < authSlices; i++) {
        const slice = flowsTS[authIntervalNs].get(i);
        /* the data point must exist to keep the series alignment intact */
        const d = getPooledDataPoint(slice.ts);  // Reuse pooled object
        if (slice[fkey]) {
          d.bytes = slice[fkey].bytes;
          d.packets = slice[fkey].packets;
          d.rtt_us = slice[fkey].rtt_us;
          d.tcp_state = slice[fkey].tcp_state;
          d.rwnd_bytes = slice[fkey].rwnd_bytes;
          d.window_scale = slice[fkey].window_scale;
          d.zero_window_cnt = slice[fkey].zero_window_cnt;
          d.dup_ack_cnt = slice[fkey].dup_ack_cnt;
          d.retransmit_cnt = slice[fkey].retransmit_cnt;
          d.ece_cnt = slice[fkey].ece_cnt;
          d.recent_events = slice[fkey].recent_events;
          if (d.rtt_us >= 0) {
            lastRtt = d.rtt_us;
          }
          if (d.tcp_state >= 0) {
            lastTcpState = d.tcp_state;
          }
          if (slice[fkey].saw_syn) {
            sawSyn = 1;
          }
          /* Track video stream info - update if video detected */
          if (slice[fkey].video_type > 0) {
            lastVideoType = slice[fkey].video_type;
            lastVideoCodec = slice[fkey].video_codec;
            lastVideoJitterUs = slice[fkey].video_jitter_us;
            lastVideoSeqLoss = slice[fkey].video_seq_loss;
            lastVideoCcErrors = slice[fkey].video_cc_errors;
            lastVideoSsrc = slice[fkey].video_ssrc;
            /* Extended video telemetry */
            lastVideoCodecSource = slice[fkey].video_codec_source;
            lastVideoWidth = slice[fkey].video_width;
            lastVideoHeight = slice[fkey].video_height;
            lastVideoProfile = slice[fkey].video_profile;
            lastVideoLevel = slice[fkey].video_level;
            lastVideoFpsX100 = slice[fkey].video_fps_x100;
            lastVideoBitrateKbps = slice[fkey].video_bitrate_kbps;
            lastVideoGopFrames = slice[fkey].video_gop_frames;
            lastVideoKeyframes = slice[fkey].video_keyframes;
            lastVideoFrames = slice[fkey].video_frames;
          }
          /* Track audio stream info - update if audio detected */
          if (slice[fkey].audio_type > 0) {
            lastAudioType = slice[fkey].audio_type;
            lastAudioCodec = slice[fkey].audio_codec;
            lastAudioJitterUs = slice[fkey].audio_jitter_us;
            lastAudioSeqLoss = slice[fkey].audio_seq_loss;
            lastAudioSsrc = slice[fkey].audio_ssrc;
            lastAudioBitrateKbps = slice[fkey].audio_bitrate_kbps;
          }
          /* Track TCP health info - update if samples present */
          if (slice[fkey].health_rtt_samples > 0) {
            lastHealthRttHist = slice[fkey].health_rtt_hist || DEFAULT_RTT_HIST;
            lastHealthRttSamples = slice[fkey].health_rtt_samples;
            lastHealthStatus = slice[fkey].health_status;
            lastHealthFlags = slice[fkey].health_flags;
          }
          /* Track IPG histogram info - update if samples present */
          if (slice[fkey].ipg_samples > 0) {
            lastIpgHist = slice[fkey].ipg_hist || DEFAULT_IPG_HIST;
            lastIpgSamples = slice[fkey].ipg_samples;
            lastIpgMeanUs = slice[fkey].ipg_mean_us;
          }
          /* Track frame size histogram info - update if samples present */
          if (slice[fkey].frame_size_samples > 0) {
            lastFrameSizeHist = slice[fkey].frame_size_hist;
            lastFrameSizeSamples = slice[fkey].frame_size_samples;
            lastFrameSizeMean = slice[fkey].frame_size_mean;
            lastFrameSizeVariance = slice[fkey].frame_size_variance;
            lastFrameSizeMin = slice[fkey].frame_size_min;
            lastFrameSizeMax = slice[fkey].frame_size_max;
          }
          /* Track PPS histogram info - update if samples present */
          if (slice[fkey].pps_samples > 0) {
            lastPpsHist = slice[fkey].pps_hist;
            lastPpsSamples = slice[fkey].pps_samples;
            lastPpsMean = slice[fkey].pps_mean;
            lastPpsVariance = slice[fkey].pps_variance;
          }
        }
        console.assert(d.bytes >= 0);
        console.assert(d.packets >= 0);
        flow.values.push(d);
      }

      // Note: Refinement data is NOT added as separate data points.
      // Adding mixed-resolution points (100ms + 20ms) creates visual discontinuity.
      // Instead, refinement data triggers more frequent redraws (via setDirty),
      // which provides smoother animation without adding artifacts.
      // The X-axis domain is updated based on refinement timestamps in the chart code.

      flow.tbytes = flowsTotals[authIntervalNs][fkey].tbytes;
      flow.tpackets = flowsTotals[authIntervalNs][fkey].tpackets;
      flow.rtt_us = lastRtt;  /* Latest RTT value for legend display */
      flow.tcp_state = lastTcpState;  /* Latest TCP state */
      flow.saw_syn = sawSyn;  /* True if SYN was ever observed */
      /* Video stream info for legend display */
      flow.video_type = lastVideoType;
      flow.video_codec = lastVideoCodec;
      flow.video_jitter_us = lastVideoJitterUs;
      flow.video_seq_loss = lastVideoSeqLoss;
      flow.video_cc_errors = lastVideoCcErrors;
      flow.video_ssrc = lastVideoSsrc;
      /* Extended video telemetry for legend display */
      flow.video_codec_source = lastVideoCodecSource;
      flow.video_width = lastVideoWidth;
      flow.video_height = lastVideoHeight;
      flow.video_profile = lastVideoProfile;
      flow.video_level = lastVideoLevel;
      flow.video_fps_x100 = lastVideoFpsX100;
      flow.video_bitrate_kbps = lastVideoBitrateKbps;
      flow.video_gop_frames = lastVideoGopFrames;
      flow.video_keyframes = lastVideoKeyframes;
      flow.video_frames = lastVideoFrames;
      /* Audio stream info for legend display */
      flow.audio_type = lastAudioType;
      flow.audio_codec = lastAudioCodec;
      flow.audio_jitter_us = lastAudioJitterUs;
      flow.audio_seq_loss = lastAudioSeqLoss;
      flow.audio_ssrc = lastAudioSsrc;
      flow.audio_bitrate_kbps = lastAudioBitrateKbps;
      /* TCP health info for legend display */
      flow.health_rtt_hist = lastHealthRttHist;
      flow.health_rtt_samples = lastHealthRttSamples;
      flow.health_status = lastHealthStatus;
      flow.health_flags = lastHealthFlags;
      /* IPG histogram info for legend display (all flows) */
      flow.ipg_hist = lastIpgHist;
      flow.ipg_samples = lastIpgSamples;
      flow.ipg_mean_us = lastIpgMeanUs;
      /* Frame size histogram info for legend display (all flows) */
      flow.frame_size_hist = lastFrameSizeHist;
      flow.frame_size_samples = lastFrameSizeSamples;
      flow.frame_size_mean = lastFrameSizeMean;
      flow.frame_size_variance = lastFrameSizeVariance;
      flow.frame_size_min = lastFrameSizeMin;
      flow.frame_size_max = lastFrameSizeMax;
      /* PPS histogram info for legend display (all flows) */
      flow.pps_hist = lastPpsHist;
      flow.pps_samples = lastPpsSamples;
      flow.pps_mean = lastPpsMean;
      flow.pps_variance = lastPpsVariance;
      chartSeries.push(flow);
    }

  };

  const updateSeries = function (series, yVal, selectedSeries, timeScale, timestamp) {
    const periodMs = timeScaleTable[timeScale];
    // Store timestamp (in seconds) with each sample for absolute time positioning
    // TypedRingBuffer2.push takes (timestamp, value) directly - no object allocation
    series.samples[timeScale].push(timestamp, series.rateFormatter(yVal));

    // Only process when interval matches the selected chart period
    if (my.charts.getChartPeriod() == periodMs) {
      updateStats(series, timeScale);
      JT.measurementsModule.updateSeries(series.name, series.stats);
      JT.trapModule.checkTriggers(series.name, series.stats);

      // Update chart data for the selected series
      // Smooth scrolling comes from currentChartTime being updated by flow refinement data
      if (series.name === selectedSeries.name) {
        updateMainChartData(series.samples[timeScale], JT.charts.getMainChartRef());
        updatePacketGapChartData(series.pgaps[timeScale],
                                 JT.charts.getPacketGapMeanRef(),
                                 JT.charts.getPacketGapMinMaxRef());
      }
    }
  };

  const updateData = function (d, sSeries, timeScale, timestamp) {
    // TypedRingBuffer4.push takes (timestamp, min, max, mean) directly - no object allocation
    sBin.rxRate.pgaps[timeScale].push(timestamp, d.min_rx_pgap, d.max_rx_pgap, d.mean_rx_pgap / 1000.0);
    sBin.txRate.pgaps[timeScale].push(timestamp, d.min_tx_pgap, d.max_tx_pgap, d.mean_tx_pgap / 1000.0);
    sBin.rxPacketRate.pgaps[timeScale].push(timestamp, d.min_rx_pgap, d.max_rx_pgap, d.mean_rx_pgap / 1000.0);
    sBin.txPacketRate.pgaps[timeScale].push(timestamp, d.min_tx_pgap, d.max_tx_pgap, d.mean_tx_pgap / 1000.0);

    updateSeries(sBin.txRate, d.tx, sSeries, timeScale, timestamp);
    updateSeries(sBin.rxRate, d.rx, sSeries, timeScale, timestamp);
    updateSeries(sBin.txPacketRate, d.txP, sSeries, timeScale, timestamp);
    updateSeries(sBin.rxPacketRate, d.rxP, sSeries, timeScale, timestamp);
  };

  my.core.processDataMsg = function (stats, interval, timestamp) {
    const selectedSeries = sBin[selectedSeriesName];
    const intervalMs = interval / 1E6;
    const chartPeriod = my.charts.getChartPeriod();

    // Update currentChartTime from stats data for smooth scrolling
    // Use any interval <= chartPeriod for smoother updates (not just the specific refinement interval)
    // This helps when refinement data (e.g. 20ms) arrives sporadically due to tier/network issues
    // Only advance forward (monotonic) to prevent jitter from out-of-order messages
    if (intervalMs <= chartPeriod && timestamp > currentChartTime) {
      currentChartTime = timestamp;
    }

    switch (interval) {
      case 5000000:
           updateData(stats, selectedSeries, '5ms', timestamp);
           break;
      case 10000000:
           updateData(stats, selectedSeries, '10ms', timestamp);
           break;
      case 20000000:
           updateData(stats, selectedSeries, '20ms', timestamp);
           break;
      case 50000000:
           updateData(stats, selectedSeries, '50ms', timestamp);
           break;
      case 100000000:
           updateData(stats, selectedSeries, '100ms', timestamp);
           break;
      case 200000000:
           updateData(stats, selectedSeries, '200ms', timestamp);
           break;
      case 500000000:
           updateData(stats, selectedSeries, '500ms', timestamp);
           break;
      case 1000000000:
           updateData(stats, selectedSeries, '1000ms', timestamp);
           break;
      default:
           console.log("unknown interval: " + interval);
    }
  };

  /***** Top Flows follows *****/

  let flows = {};
  let flowRank = {}; /* a sortable list of flow keys for each interval */

  let flowsTS = {};
  let flowsTotals = {};

  /* Track last authoritative timestamp per flow for progressive refinement.
   * Key: flowKey (without interval prefix), Value: timestamp of last authoritative data point.
   * Used to determine boundary between authoritative and provisional data. */
  const authoritativeFrontier = new Map();

  /* Track the "current time" for smooth X-axis scrolling.
   * Updated from refinement data to provide smooth animation between authoritative updates. */
  let currentChartTime = 0;

  /* discard all previous flow data, like when changing capture interface */
  const clearFlows = function () {
    flows = {};
    flowRank = {};
    flowsTS = {};
    flowsTotals = {};
    authoritativeFrontier.clear();
    flowIdentityCache.clear();
    flowObjectCache.clear();
    currentChartTime = 0;
  };

  /* Get current chart time for smooth X-axis scrolling */
  my.core.getCurrentChartTime = function() {
    return currentChartTime;
  };

  /* Get the refinement interval for smooth animation.
   * Returns the interval in seconds, or 0 if no refinement available.
   * Used by charts to calculate display lag for smooth scrolling. */
  my.core.getRefinementIntervalSec = function() {
    const chartPeriod = my.charts.getChartPeriod();
    const refinementMs = intervalRefinement[chartPeriod];
    return refinementMs ? refinementMs / 1000 : 0;
  };

  /* Get the display lag for smooth scrolling (right edge buffer).
   * Returns one authoritative interval to keep the newest data point off-screen.
   * This prevents "popping" caused by D3's curveBasis interpolation being
   * affected by new data points near the visible edge.
   * Returns 0 if no refinement available. */
  my.core.getDisplayLagSec = function() {
    const chartPeriod = my.charts.getChartPeriod();
    const refinementMs = intervalRefinement[chartPeriod];
    // Use one authoritative interval as display lag
    return refinementMs ? chartPeriod / 1000 : 0;
  };

  /* Get the left margin for smooth scrolling.
   * Returns one authoritative interval - this SHRINKS the visible window
   * so the oldest data is off-screen (to the left) when it ages out.
   * Returns 0 if no refinement available (no smooth scrolling needed). */
  my.core.getLeftMarginSec = function() {
    const chartPeriod = my.charts.getChartPeriod();
    const refinementMs = intervalRefinement[chartPeriod];
    // Only apply margin if refinement is available (smooth scrolling active)
    return refinementMs ? chartPeriod / 1000 : 0;
  };

  /* Reusable object for getChartDomain to avoid per-call allocations */
  const chartDomainResult = {
    currentTime: 0,
    xMin: 0,
    xMax: 0,
    windowWidthSec: 0,
    displayLagSec: 0,
    leftMarginSec: 0,
    visibleWidthSec: 0,
    isValid: false
  };

  /* Get chart domain parameters for smooth scrolling.
   * Returns an object with all values needed to set X-axis domain and ticks.
   * WARNING: Returns a shared object that is reused across calls.
   * Callers must consume values immediately and not store the reference. */
  my.core.getChartDomain = function() {
    const currentTime = currentChartTime;
    const chartPeriodMs = my.charts.getChartPeriod();
    const samples = sampleCount;
    const windowWidthSec = samples * (chartPeriodMs / 1000);
    const displayLagSec = my.core.getDisplayLagSec();
    const leftMarginSec = my.core.getLeftMarginSec();

    chartDomainResult.currentTime = currentTime;
    chartDomainResult.windowWidthSec = windowWidthSec;
    chartDomainResult.displayLagSec = displayLagSec;
    chartDomainResult.leftMarginSec = leftMarginSec;

    if (currentTime > 0) {
      chartDomainResult.xMin = currentTime - windowWidthSec + leftMarginSec;
      chartDomainResult.xMax = currentTime - displayLagSec;
      chartDomainResult.visibleWidthSec = windowWidthSec - displayLagSec - leftMarginSec;
      chartDomainResult.isValid = true;
    } else {
      chartDomainResult.xMin = 0;
      chartDomainResult.xMax = 0;
      chartDomainResult.visibleWidthSec = 0;
      chartDomainResult.isValid = false;
    }

    return chartDomainResult;
  };

  const getFlowKey = function (interval, flow) {
    return interval + '/' + flow.src + '/' + flow.sport + '/' + flow.dst +
           '/' + flow.dport + '/' + flow.proto + '/' + flow.tclass;
  };

  const msgToFlows = function (msg, timestamp) {
    const interval = msg.interval_ns;
    const fcnt = msg.flows.length;

    /* we haven't seen this interval before, initialise it. */
    if (!flowsTS[interval]) {
      flowsTS[interval] = new CBuffer(sampleWindowSize);
      flowsTotals[interval] = {};
      flowRank[interval] = []; /* sortable! */
    }

    const sample_slice = {};
    sample_slice.ts = timestamp;
    flowsTS[interval].push(sample_slice);

    for (let i = 0; i < fcnt; i++) {
      const fkey = getFlowKey(interval, msg.flows[i]);

      /* create new flow entry if we haven't seen it before */
      if (!flowsTotals[interval][fkey]) {
        flowsTotals[interval][fkey] = {
          'ttl': sampleWindowSize,
          'tbytes': 0,
          'tpackets': 0
        };
        flowRank[interval].push(fkey);
      }

      /* Reuse the JSON-parsed flow object directly instead of copying all properties.
       * This eliminates ~20 object allocations per message.
       * Just set defaults for optional fields that might be undefined. */
      const flow = msg.flows[i];

      /* Set defaults for optional video fields */
      if (flow.video_type === undefined) flow.video_type = 0;
      if (flow.video_codec === undefined) flow.video_codec = 0;
      if (flow.video_jitter_us === undefined) flow.video_jitter_us = 0;
      if (flow.video_seq_loss === undefined) flow.video_seq_loss = 0;
      if (flow.video_cc_errors === undefined) flow.video_cc_errors = 0;
      if (flow.video_ssrc === undefined) flow.video_ssrc = 0;
      if (flow.video_codec_source === undefined) flow.video_codec_source = 0;
      if (flow.video_width === undefined) flow.video_width = 0;
      if (flow.video_height === undefined) flow.video_height = 0;
      if (flow.video_profile === undefined) flow.video_profile = 0;
      if (flow.video_level === undefined) flow.video_level = 0;
      if (flow.video_fps_x100 === undefined) flow.video_fps_x100 = 0;
      if (flow.video_bitrate_kbps === undefined) flow.video_bitrate_kbps = 0;
      if (flow.video_gop_frames === undefined) flow.video_gop_frames = 0;
      if (flow.video_keyframes === undefined) flow.video_keyframes = 0;
      if (flow.video_frames === undefined) flow.video_frames = 0;

      /* Set defaults for optional audio fields */
      if (flow.audio_type === undefined) flow.audio_type = 0;
      if (flow.audio_codec === undefined) flow.audio_codec = 0;
      if (flow.audio_sample_rate === undefined) flow.audio_sample_rate = 0;
      if (flow.audio_jitter_us === undefined) flow.audio_jitter_us = 0;
      if (flow.audio_seq_loss === undefined) flow.audio_seq_loss = 0;
      if (flow.audio_ssrc === undefined) flow.audio_ssrc = 0;
      if (flow.audio_bitrate_kbps === undefined) flow.audio_bitrate_kbps = 0;

      /* Set defaults for optional health fields
       * Use shared constants for read-only histograms (reduces GC pressure) */
      if (flow.health_rtt_hist === undefined) flow.health_rtt_hist = DEFAULT_RTT_HIST;
      if (flow.health_rtt_samples === undefined) flow.health_rtt_samples = 0;
      if (flow.health_status === undefined) flow.health_status = 0;
      if (flow.health_flags === undefined) flow.health_flags = 0;

      /* Set defaults for optional histogram fields */
      if (flow.ipg_hist === undefined) flow.ipg_hist = DEFAULT_IPG_HIST;
      if (flow.ipg_samples === undefined) flow.ipg_samples = 0;
      if (flow.ipg_mean_us === undefined) flow.ipg_mean_us = 0;
      if (flow.frame_size_hist === undefined) flow.frame_size_hist = null;
      if (flow.frame_size_samples === undefined) flow.frame_size_samples = 0;
      if (flow.frame_size_mean === undefined) flow.frame_size_mean = 0;
      if (flow.frame_size_variance === undefined) flow.frame_size_variance = 0;
      if (flow.frame_size_min === undefined) flow.frame_size_min = 0;
      if (flow.frame_size_max === undefined) flow.frame_size_max = 0;
      if (flow.pps_hist === undefined) flow.pps_hist = null;
      if (flow.pps_samples === undefined) flow.pps_samples = 0;
      if (flow.pps_mean === undefined) flow.pps_mean = 0;
      if (flow.pps_variance === undefined) flow.pps_variance = 0;

      sample_slice[fkey] = flow;


      /* reset the time-to-live to the chart window length (in samples),
       * so that it can be removed when it ages beyond the window. */
      flowsTotals[interval][fkey].ttl = sampleWindowSize;
      /* update totals for the flow */
      flowsTotals[interval][fkey].tbytes += flow.bytes;
      flowsTotals[interval][fkey].tpackets += flow.packets;

      console.assert(
        ((flowsTotals[interval][fkey].tbytes === 0)
         && (flowsTotals[interval][fkey].tpackets === 0))
        ||
        ((flowsTotals[interval][fkey].tbytes != 0)
         && (flowsTotals[interval][fkey].tpackets != 0)
        )
      );
    }

    /* Update flow ranks table ONCE after all flows processed (was inside loop - very inefficient!)
     * Sort descending by total bytes. Use closure to capture current totals. */
    const totals = flowsTotals[interval];
    flowRank[interval].sort((a, b) => totals[b].tbytes - totals[a].tbytes);
  };


  /* reduce the time-to-live for the flow and expire it when no samples are
   * within the visible chart window */
  /* Reusable set for collecting expired keys - avoids allocations */
  const expiredKeys = new Set();

  const expireOldFlowsAndUpdateRank = function (interval) {
    const ft = flowsTotals[interval];
    const rank = flowRank[interval];
    expiredKeys.clear();

    /* First pass: decrement TTL and collect expired keys */
    for (const fkey of Object.keys(ft)) {
      ft[fkey].ttl -= 1;
      if (ft[fkey].ttl <= 0) {
        expiredKeys.add(fkey);
      }
    }

    /* Second pass: remove expired keys in batch (avoid repeated filter calls) */
    if (expiredKeys.size > 0) {
      /* Remove from flowsTotals and identity cache */
      for (const fkey of expiredKeys) {
        delete ft[fkey];
        flowIdentityCache.delete(fkey);  // Clean up cached identity
      }
      /* Filter flowRank once for all expired keys */
      flowRank[interval] = rank.filter(o => !expiredKeys.has(o));
    }

    /* We must have the same number of flow keys in the flowsTotals and
     * flowRank accounting tables... */
    console.assert(flowRank[interval].length ==
                   Object.keys(flowsTotals[interval]).length);

    /* Remember: each TCP flow has a return flow, but UDP may or may not! */
  };

  my.core.processTopTalkMsg = function (msg) {
    let interval = msg.interval_ns;
    let tstamp = msg.timestamp.tv_sec + msg.timestamp.tv_nsec / 1E9;

    console.assert(!(Number.isNaN(tstamp)));

    msgToFlows(msg, tstamp);
    expireOldFlowsAndUpdateRank(interval);
    updateTopFlowChartData(interval);

    return;
    switch (interval) {
      case 5000000:
      case 10000000:
      case 20000000:
      case 50000000:
      case 100000000:
      case 200000000:
      case 500000000:
           break;
      case 1000000000:
           /* insert debug logging here */
           console.log("[processTopTalkMsg] interval === " + interval +
                       " msg.timestamp:" + msg.timestamp.tv_sec + "." +
                         + msg.timestamp.tv_nsec);
           console.log("flowsTotals["+interval+"]: " +
                       JSON.stringify(flowsTotals[interval]));
           break;
      default:
           console.log("unknown interval: " + interval);
           return;
    }
  };

})(JT);
/* End of jittertrap-core.js */
