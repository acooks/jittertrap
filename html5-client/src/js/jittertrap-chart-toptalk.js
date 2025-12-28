/* jittertrap-chart-toptalk.js */

/* global d3 */
/* global JT:true */

((my) => {
  'use strict';

  my.charts.toptalk = {};

  const chartData = [];

  /* Reusable data structures to reduce GC pressure.
   * Instead of creating new Maps and arrays every frame, we clear and reuse these. */
  const reusableBinsMap = new Map();
  const reusableFormattedData = [];
  const reusableOtherValuesMap = new Map();
  const reusableTopNResult = [];
  const reusableBinObjects = [];  // Pool of bin objects { ts: ... }
  let binObjectPoolIndex = 0;
  const reusableFkeys = [];  // For fkeys array in redraw
  const reusableBarData = [];  // For barData array in redraw
  const reusableFlowDataMap = new Map();  // For flowDataMap in redraw
  const reusableFlowDataObjects = [];  // Pool of flow data objects
  const formatResult = { formattedData: null, maxSlice: 0 };  // Reusable return object

  /* Pre-defined default histogram arrays - avoids allocating new arrays in hot path */
  const DEFAULT_VIDEO_JITTER_HIST = [0,0,0,0,0,0,0,0,0,0,0,0];
  const DEFAULT_HEALTH_RTT_HIST = [0,0,0,0,0,0,0,0,0,0,0,0,0,0];
  const DEFAULT_IPG_HIST = [0,0,0,0,0,0,0,0,0,0,0,0];

  /* Pre-defined comparators to avoid creating closures in hot path */
  const timestampComparator = (a, b) => a.ts - b.ts;

  /* Throttle legend updates - they don't need 60Hz refresh.
   * 100ms (10Hz) is sufficient for human perception of value changes. */
  let lastLegendUpdateTime = 0;
  const LEGEND_UPDATE_INTERVAL_MS = 100;

  /* Custom stack implementation that reuses arrays instead of allocating.
   * d3.stack() creates new arrays every call, causing massive GC pressure.
   * This version pre-allocates and updates in place. */
  const cachedStackResult = [];  // Outer array of flow layers
  const cachedStackKeys = [];    // Track which keys we've allocated for
  let cachedStackDataLength = 0; // Track data length for reallocation

  const customStack = function(data, keys) {
    const dataLen = data.length;
    const keysLen = keys.length;

    // Check if we need to reallocate (keys changed or data grew)
    let needsRealloc = cachedStackResult.length !== keysLen ||
                       cachedStackDataLength < dataLen;

    // Also check if keys changed
    if (!needsRealloc) {
      for (let k = 0; k < keysLen; k++) {
        if (cachedStackKeys[k] !== keys[k]) {
          needsRealloc = true;
          break;
        }
      }
    }

    if (needsRealloc) {
      // Reallocate the structure
      cachedStackResult.length = keysLen;
      cachedStackKeys.length = keysLen;

      for (let k = 0; k < keysLen; k++) {
        cachedStackKeys[k] = keys[k];

        if (!cachedStackResult[k]) {
          cachedStackResult[k] = [];
        }
        cachedStackResult[k].key = keys[k];

        // Ensure inner array has enough slots
        const layer = cachedStackResult[k];
        if (layer.length < dataLen) {
          for (let i = layer.length; i < dataLen; i++) {
            // Each point is [y0, y1] with a .data property
            layer[i] = [0, 0];
            layer[i].data = null;
          }
        }
        layer.length = dataLen;  // Trim if needed
      }
      cachedStackDataLength = dataLen;
    }

    // Always trim layers to current dataLen (handles shrinking data)
    // This is outside the needsRealloc block to handle data shrinking
    for (let k = 0; k < keysLen; k++) {
      cachedStackResult[k].length = dataLen;
    }
    cachedStackDataLength = dataLen;

    // Now update values in place (this is the hot path)
    // d3.stack stacks from LAST key to FIRST:
    // - keys[n-1] is at bottom (y0=0)
    // - keys[0] is at top (highest y values)
    // So we iterate in reverse, accumulating y0 from last key to first
    for (let i = 0; i < dataLen; i++) {
      const d = data[i];
      let y0 = 0;

      // Reverse iteration: last key gets y0=0 (bottom), first key on top
      for (let k = keysLen - 1; k >= 0; k--) {
        const key = keys[k];
        const value = d[key] || 0;
        const y1 = y0 + value;

        const point = cachedStackResult[k][i];
        point[0] = y0;
        point[1] = y1;
        point.data = d;

        y0 = y1;
      }
    }

    return cachedStackResult;
  };

  /* Track current flow keys to avoid for...in iteration */
  const currentFlowKeys = new Set();
  const currentFlowKeysArray = [];  // Array form for indexed iteration (avoids iterator)

  /* Get or create a bin object from the pool.
   * Note: We don't delete old properties (expensive) - we just set new ones.
   * Stale properties are ignored because customStack/maxSlice only access currentFlowKeys. */
  const getPooledBinObject = function(ts) {
    if (binObjectPoolIndex < reusableBinObjects.length) {
      const obj = reusableBinObjects[binObjectPoolIndex++];
      obj.ts = ts;
      return obj;
    }
    // Pool exhausted - grow it
    const obj = { ts: ts };
    reusableBinObjects.push(obj);
    binObjectPoolIndex++;
    return obj;
  };

  const clearChartData = function () {
    chartData.length = 0;
  };

  /* must return a reference to an array of {x:x, y:y} */
  my.charts.toptalk.getDataRef = function () {
    return chartData;
  };

  /* Cached "other" flow object to avoid allocation each frame.
   * WARNING: This object is mutated and reused across frames.
   * Callers must not store references to this object across frames. */
  const cachedOtherFlow = {
    fkey: 'other',
    tbytes: 0,
    values: []
  };
  const cachedOtherValues = [];  // Pool for {ts, bytes} objects

  /* Aggregate chart data, collapsing flows beyond LEGEND_DISPLAY_LIMIT into "other".
   * WARNING: Returns a reference to reusableTopNResult which is mutated each frame.
   * Callers must consume the result immediately and not store references. */
  const processAndAggregateChartData = function(incomingData) {
    const LEGEND_DISPLAY_LIMIT = 20;

    if (incomingData.length <= LEGEND_DISPLAY_LIMIT) {
      return incomingData;
    }

    // Reuse result array instead of slice().concat()
    reusableTopNResult.length = 0;
    const topNCount = Math.min(incomingData.length, LEGEND_DISPLAY_LIMIT);
    for (let i = 0; i < topNCount; i++) {
      reusableTopNResult.push(incomingData[i]);
    }

    // Reset cached other flow
    cachedOtherFlow.tbytes = 0;
    cachedOtherFlow.values.length = 0;

    // Reuse Map for aggregation
    reusableOtherValuesMap.clear();

    // Process remaining flows (index-based instead of slice)
    for (let i = LEGEND_DISPLAY_LIMIT; i < incomingData.length; i++) {
      const flow = incomingData[i];
      cachedOtherFlow.tbytes += flow.tbytes;
      for (let j = 0; j < flow.values.length; j++) {
        const dataPoint = flow.values[j];
        const currentBytes = reusableOtherValuesMap.get(dataPoint.ts) || 0;
        reusableOtherValuesMap.set(dataPoint.ts, currentBytes + dataPoint.bytes);
      }
    }

    // Convert Map to values array, reusing pooled objects
    // Use for...of instead of forEach to avoid closure allocation
    let valueIndex = 0;
    for (const entry of reusableOtherValuesMap) {
      const ts = entry[0];
      const bytes = entry[1];
      if (!cachedOtherValues[valueIndex]) {
        cachedOtherValues[valueIndex] = { ts: 0, bytes: 0 };
      }
      cachedOtherValues[valueIndex].ts = ts;
      cachedOtherValues[valueIndex].bytes = bytes;
      cachedOtherFlow.values.push(cachedOtherValues[valueIndex]);
      valueIndex++;
    }

    // Ensure the values are sorted by timestamp, as d3 expects
    cachedOtherFlow.values.sort(timestampComparator);

    // Add other flow to result (instead of concat)
    reusableTopNResult.push(cachedOtherFlow);
    return reusableTopNResult;
  };

  my.charts.toptalk.toptalkChart = (function (m) {
    // Use smaller margins on mobile devices
    const isMobile = window.innerWidth <= 768;
    const margin = {
      top: isMobile ? 15 : 20,
      right: isMobile ? 10 : 20,
      bottom: isMobile ? 80 : 100,
      left: isMobile ? 50 : 75
    };

    const size = { width: 960, height: 400 };
    let xScale = d3.scaleLinear();
    let yScale = d3.scaleLinear();
    // Use Spectral interpolator for better distinctness with 20+ flows
    const colorScale = d3.scaleOrdinal(d3.quantize(d3.interpolateSpectral, 21).reverse());
    // Cache domain as Set for O(1) lookup in getFlowColor (updated when domain changes)
    let colorDomainSet = new Set();

    const formatBitrate = function(d) {
        // d is in bps. d3.format('.2s') auto-scales and adds SI prefix.
        return d3.format(".2s")(d) + "bps";
    };
    
    let xAxis = d3.axisBottom();
    let yAxis = d3.axisLeft().tickFormat(formatBitrate);
    let xGrid = d3.axisBottom();
    let yGrid = d3.axisLeft();
    let area = d3.area();

    const stack = d3.stack()
                .order(d3.stackOrderReverse)
                .offset(d3.stackOffsetNone);

    // Bisector to find the closest timestamp index
    const bisectDate = d3.bisector(d => d.data.ts).left;

    let svg = {};
    let context = {};
    let canvas = {};
    let currentStackedData = []; // Store for hit-testing

    // Cached D3 selections to avoid repeated select() calls which create
    // new selection objects that form cycles with DOM nodes, increasing
    // Cycle Collector (CC) pressure in Firefox
    let cachedSelections = {
      xAxis: null,
      yAxis: null,
      xGrid: null,
      yGrid: null,
      barsbox: null
    };
    let currentFlowDataMap = new Map(); // Store flow data for legend
    let resizeTimer; // Timer for debounced resize handling
    let cachedFkeys = []; // Cache for legend optimization
    let currentBarData = []; // Current bar data for click handling
    let currentBarScale = null; // Current x scale for bar click handling

    // Cached values for tick formatter to avoid closure allocation
    let cachedMaxTimestamp = 0;
    const xTickFormatter = function(seconds) {
      const relativeSeconds = seconds - cachedMaxTimestamp;
      return relativeSeconds % 1 === 0 ? relativeSeconds.toString() : relativeSeconds.toFixed(1);
    };

    // Reusable tick values array
    const tickValuesCache = [];

    /* Reset and redraw the things that don't change for every redraw() */
    m.reset = function() {
      // Clear cached selections before removing DOM (breaks cycles)
      cachedSelections.xAxis = null;
      cachedSelections.yAxis = null;
      cachedSelections.xGrid = null;
      cachedSelections.yGrid = null;
      cachedSelections.barsbox = null;

      d3.select("#chartToptalk").selectAll("svg").remove();
      d3.select("#chartToptalk").selectAll("canvas").remove();


      canvas = d3.select("#chartToptalk")
            .append("canvas")
            .style("position", "absolute")
            .style("pointer-events", "none");

      my.charts.resizeChart("#chartToptalk", size)();
      context = canvas.node().getContext("2d");

      svg = d3.select("#chartToptalk")
            .append("svg")
            .style("position", "relative");

      const width = size.width - margin.left - margin.right;
      const height = size.height - margin.top - margin.bottom;

      xScale = d3.scaleLinear().range([0, width]);
      yScale = d3.scaleLinear().range([height, 0]);

      xAxis = d3.axisBottom()
              .scale(xScale)
              .ticks(10);

      yAxis = d3.axisLeft()
              .scale(yScale)
              .ticks(5)
              .tickFormat(formatBitrate);

      xGrid = d3.axisBottom()
          .scale(xScale)
           .tickSize(-height)
           .ticks(10)
           .tickFormat("");

      yGrid = d3.axisLeft()
          .scale(yScale)
           .tickSize(-width)
           .ticks(5)
           .tickFormat("");

      svg.attr("width", width + margin.left + margin.right)
         .attr("height", height + margin.top + margin.bottom);

      canvas.attr("width", width)
         .attr("height", height)
         .style("transform", "translate(" + margin.left + "px," + margin.top + "px)");
      const graph = svg.append("g")
         .attr("transform", "translate(" + margin.left + "," + margin.top + ")");

      graph.append("text")
         .attr("class", "title")
         .attr("text-anchor", "middle")
         .attr("x", width/2)
         .attr("y", 0 - margin.top / 2)
         .attr("dominant-baseline", "middle")
         .text("Top flows");

      // Cache selections as they're created to avoid repeated select() calls in redraw
      cachedSelections.xAxis = graph.append("g")
         .attr("class", "x axis")
         .attr("transform", "translate(0," + height + ")")
         .call(xAxis);

      graph.append("text")
           .attr("class", "axis-label")
           .attr("text-anchor", "middle")
           .attr("x", width/2)
           .attr("y", height + 35)
           .text("Time (s)");

      cachedSelections.yAxis = graph.append("g")
         .attr("class", "y axis")
         .call(yAxis);

      graph.append("text")
         .attr("class", "axis-label")
         .attr("transform", "rotate(-90)")
         .attr("y", 0 - margin.left)
         .attr("x", 0 - (height / 2))
         .attr("dy", "1em")
         .style("text-anchor", "middle")
         .text("Bitrate");

      cachedSelections.xGrid = graph.append("g")
        .attr("class", "xGrid")
        .attr("transform", "translate(0," + height + ")")
        .call(xGrid);

      cachedSelections.yGrid = graph.append("g")
        .attr("class", "yGrid")
        .call(yGrid);

      context.clearRect(0, 0, width, height);

      // Initialize area generator (reused across redraws)
      area = d3.area()
               .curve(d3.curveMonotoneX)
               .context(context)
               .x(d => xScale(d.data.ts))
               .y0(d => yScale(d[0] || 0))
               .y1(d => yScale(d[1] || 0));

      cachedSelections.barsbox = svg.append("g")
         .attr("class", "barsbox")
         .attr("id", "barsbox");

      cachedSelections.barsbox.append("text")
           .attr("x", 0)
           .attr("y", 35)
           .style("font-size", "12px")
           .text("Byte Distribution");

      // Mousedown handler for flow selection (mousedown fires immediately,
      // unlike click which requires mouseup on the same element - problematic
      // when elements are recreated at 60 FPS)
      cachedSelections.barsbox.on("mousedown", function(event) {
        // Only handle left mouse button
        if (event.button !== 0) return;

        // Get position relative to this group
        const [clickX, clickY] = d3.pointer(event, this);

        // Check if click is within the bar area (y=9 to y=21)
        if (clickY < 9 || clickY > 21) return;

        // Find which bar was clicked using stored data
        if (!currentBarData || currentBarData.length === 0 || !currentBarScale) return;

        for (let i = 0; i < currentBarData.length; i++) {
          const bar = currentBarData[i];
          const x0 = currentBarScale(bar.x0);
          const x1 = currentBarScale(bar.x1);
          if (clickX >= x0 && clickX <= x1) {
            event.stopPropagation();
            event.preventDefault();

            const currentSelection = my.charts.getSelectedFlow();
            if (currentSelection === bar.k) {
              my.charts.clearSelectedFlow();
            } else {
              my.charts.setSelectedFlow(bar.k);
            }

            // Update legend row visual state immediately
            updateLegendSelection();
            return;
          }
        }
      });

      // Initialize the HTML legend header
      const legendContainer = d3.select("#toptalkLegendContainer");
      legendContainer.selectAll("*").remove(); // Clear any existing content

      // Create a table-like structure for the legend
      // (Header is now static in HTML)

      // Invalidate cached fkeys to force legend rebuild on next redraw
      cachedFkeys = [];

      // Clear expanded state when chart resets
      expandedVideoRows.clear();
      expandedAudioRows.clear();
      expandedHealthRows.clear();
    };

    /* Reformat chartData to work with the new d3 v7 API
     * Ref: https://github.com/d3/d3-shape/blob/master/README.md#stack
     *
     * OPTIMIZED: Profile showed this function at 7.6% self time.
     * Key optimizations:
     * 1. Collect flow keys into array for indexed iteration (avoids iterator allocation)
     * 2. Track bin sums incrementally to calculate maxSlice during bin building
     * 3. Build formattedData array during bin creation (avoids Map.values() iterator)
     * 4. Only sort if data is actually out of order (server sends sorted data)
     */
    const formatDataAndGetMaxSlice = function(chartData) {
      // Reuse Map and array to reduce GC pressure (instead of new Map() every frame)
      reusableBinsMap.clear();
      currentFlowKeys.clear();
      binObjectPoolIndex = 0;  // Reset bin object pool
      let maxSlice = 0;

      // First pass: collect current flow keys into both Set and array
      // Array allows indexed iteration without iterator allocation
      currentFlowKeysArray.length = 0;
      for (let i = 0; i < chartData.length; i++) {
        const fkey = chartData[i].fkey;
        if (!currentFlowKeys.has(fkey)) {
          currentFlowKeys.add(fkey);
          currentFlowKeysArray.push(fkey);
        }
      }

      // Build formattedData array directly during bin creation (avoids Map.values() iterator)
      reusableFormattedData.length = 0;

      // Second pass: build bins and track sums
      // Track last timestamp to detect if sorting is needed
      let lastTs = -Infinity;
      let needsSort = false;

      for (let i = 0; i < chartData.length; i++) {
        const row = chartData[i];
        const fkey = row.fkey;
        const values = row.values;

        for (let j = 0; j < values.length; j++) {
          const o = values[j];
          const ts = o.data ? o.data.ts : o.ts; // Handle potential pre-wrapped data
          const bytes = o.bytes; // bytes is Bytes/sec (rate) from the server
          // Calculate bps: bytes * 8 bits/byte
          const bps = bytes * 8;

          // Check if we have seen this timestamp before.
          let bin = reusableBinsMap.get(ts);
          if (bin === undefined) {
            // If not, get a pooled bin object for this timestamp
            bin = getPooledBinObject(ts);
            // Initialize all current flow keys to 0 using indexed loop
            for (let k = 0; k < currentFlowKeysArray.length; k++) {
              bin[currentFlowKeysArray[k]] = 0;
            }
            bin._sum = 0;  // Track sum directly on bin object
            reusableBinsMap.set(ts, bin);
            reusableFormattedData.push(bin);  // Add to array immediately

            // Check if out of order
            if (ts < lastTs) {
              needsSort = true;
            }
            lastTs = ts;
          }

          // Add to bin and update running sum
          bin[fkey] += bps;
          bin._sum += bps;
          if (bin._sum > maxSlice) {
            maxSlice = bin._sum;
          }
        }
      }

      // Only sort if data was out of order
      if (needsSort) {
        reusableFormattedData.sort(timestampComparator);
      }

      // Return reusable result object - caller must not store reference across frames
      formatResult.formattedData = reusableFormattedData;
      formatResult.maxSlice = maxSlice;
      return formatResult;
    }

    const getFlowColor = (key) => {
      if (key === 'other') {
        return '#cccccc'; // a neutral grey
      }
      /* Check if key is in the domain to prevent D3's auto-extension behavior.
       * D3 ordinal scales auto-add unknown keys to the domain when scale(key) is called,
       * which corrupts the color assignments for existing keys.
       * Use cached Set for O(1) lookup instead of O(n) array search. */
      if (!colorDomainSet.has(key)) {
        return '#888888'; // Grey for unknown/expired flows
      }
      return colorScale(key);
    };

    /* Parse flow key into component parts */
    const parseFlowKey = (fkey) => {
      const parts = fkey.split('/');
      return {
        sourceIP: parts[1],
        sourcePort: parts[2],
        destIP: parts[3],
        destPort: parts[4],
        proto: parts[5],
        tclass: parts[6]
      };
    };

    /* Check if two arrays have the same elements (order-independent)
     * Uses a reusable Set to avoid per-call allocations (reduces GC pressure) */
    const reusableCompareSet = new Set();
    const arraysEqualUnordered = (a, b) => {
      if (a.length !== b.length) return false;
      reusableCompareSet.clear();
      for (let i = 0; i < a.length; i++) {
        reusableCompareSet.add(a[i]);
      }
      for (let i = 0; i < b.length; i++) {
        if (!reusableCompareSet.has(b[i])) return false;
      }
      return true;
    };

    /* Format RTT value for display */
    const formatRtt = (rtt_us) => {
      if (rtt_us < 0 || rtt_us === undefined) return "-";
      if (rtt_us < 1000) return rtt_us.toFixed(0) + " us";
      if (rtt_us < 1000000) return (rtt_us / 1000).toFixed(2) + " ms";
      return (rtt_us / 1000000).toFixed(2) + " s";
    };

    /* Video stream type names */
    const VIDEO_TYPES = {
      0: 'None',
      1: 'RTP',
      2: 'MPEG-TS'
    };

    /* Video codec names */
    const VIDEO_CODECS = {
      0: 'Unknown',
      1: 'H.264',
      2: 'H.265',
      3: 'VP8',
      4: 'VP9',
      5: 'AV1'
    };

    /* Audio codec names */
    const AUDIO_CODECS = {
      0: 'Unknown',
      1: 'PCMU',
      2: 'PCMA',
      3: 'G.729',
      4: 'Opus',
      5: 'AAC'
    };

    /* Codec source names */
    const CODEC_SOURCES = {
      0: 'Unknown',
      1: 'In-band',
      2: 'SDP'
    };

    /* TCP health status values (must match server) */
    const TCP_HEALTH = {
      UNKNOWN: 0,
      GOOD: 1,
      WARNING: 2,
      PROBLEM: 3
    };

    /* TCP health flag bits (must match server) */
    const TCP_HEALTH_FLAGS = {
      HIGH_TAIL_LATENCY: 0x01,
      ELEVATED_LOSS: 0x02,
      HIGH_LOSS: 0x04,
      WINDOW_STARVATION: 0x08,
      RTO_STALLS: 0x10
    };

    /* Health icon lookup table - indexed by TCP_HEALTH status value */
    const HEALTH_ICONS = [
      { icon: '-', color: '#6c757d', title: 'Unknown' },      /* 0: UNKNOWN */
      { icon: '\u2713', color: '#28a745', title: 'Healthy' }, /* 1: GOOD - check mark, green */
      { icon: '\u26A0', color: '#ffc107', title: 'Warning' }, /* 2: WARNING - warning sign, yellow */
      { icon: '\u2717', color: '#dc3545', title: 'Problem' }  /* 3: PROBLEM - X mark, red */
    ];

    /* Click action titles */
    const CLICK_TITLES = {
      expandHealth: "Click to expand health details",
      collapseHealth: "Click to collapse health details",
      expandTiming: "Click to expand timing details",
      collapseTiming: "Click to collapse timing details",
      expandVideo: "Click to expand video details",
      collapseVideo: "Click to collapse video details",
      expandAudio: "Click to expand audio details",
      collapseAudio: "Click to collapse audio details"
    };

    /* Get health icon and color based on status */
    const getHealthIcon = (health_status) => {
      return HEALTH_ICONS[health_status] || HEALTH_ICONS[0];
    };

    /* Get the appropriate health/timing icon for a flow
     * Unified logic for both initial creation and updates
     */
    const getHealthIconForFlow = (flowData, isTcp, isExpanded) => {
      if (isExpanded) {
        return { icon: '\u25BC', color: '#333', cursor: 'pointer', clickable: true };
      }

      const hasTcpHealth = flowData && flowData.health_rtt_samples >= 10;
      const hasIpgData = flowData && flowData.ipg_samples > 0;
      const hasFrameSizeData = flowData && flowData.frame_size_samples > 0;
      const hasPpsData = flowData && flowData.pps_samples > 0;
      const hasAnyTimingData = hasIpgData || hasFrameSizeData || hasPpsData;

      if (isTcp && hasTcpHealth) {
        const result = getHealthIcon(flowData.health_status || 0);
        return { icon: result.icon, color: result.color, cursor: 'pointer', clickable: true };
      } else if (hasAnyTimingData) {
        return { icon: '\u2261', color: '#6f42c1', cursor: 'pointer', clickable: true };
      } else {
        return { icon: '-', color: '#6c757d', cursor: 'default', clickable: false };
      }
    };

    /* RTT histogram bucket labels - short versions for display */
    const RTT_BUCKET_LABELS_SHORT = [
      '<100µ', '100µ', '200µ', '500µ',
      '1ms', '2ms', '5ms', '10ms',
      '20ms', '50ms', '100ms', '200ms',
      '500ms', '>1s'
    ];

    /* RTT histogram bucket labels - full versions for tooltips */
    const RTT_BUCKET_LABELS_FULL = [
      '<100µs', '100-200µs', '200-500µs', '0.5-1ms',
      '1-2ms', '2-5ms', '5-10ms', '10-20ms',
      '20-50ms', '50-100ms', '100-200ms', '200-500ms',
      '0.5-1s', '>1s'
    ];

    /* Jitter histogram bucket labels - short versions for display (12 buckets) */
    const JITTER_BUCKET_LABELS_SHORT = [
      '<10µ', '10µ', '50µ', '100µ',
      '0.5ms', '1ms', '2ms', '5ms',
      '10ms', '20ms', '50ms', '>100ms'
    ];

    /* Jitter histogram bucket labels - full versions for tooltips */
    const JITTER_BUCKET_LABELS_FULL = [
      '<10µs', '10-50µs', '50-100µs', '100-500µs',
      '0.5-1ms', '1-2ms', '2-5ms', '5-10ms',
      '10-20ms', '20-50ms', '50-100ms', '>100ms'
    ];

    /* Frame size histogram bucket labels - 20 buckets
     * Covers VoIP, MPEG-TS, tunnel MTUs, and standard Ethernet
     */
    const FRAME_SIZE_BUCKET_LABELS_SHORT = [
      '<64',    /* 0: undersized */
      '64',     /* 1: 64-100 (ACKs) */
      '100',    /* 2: 100-160 (G.729) */
      '160',    /* 3: 160-220 (G.711 20ms) */
      '220',    /* 4: 220-300 (G.711 30ms) */
      '300',    /* 5: 300-400 (TS 2×) */
      '400',    /* 6: 400-576 (TS 3×) */
      '576',    /* 7: 576-760 (TS 4×) */
      '760',    /* 8: 760-950 (TS 5×) */
      '950',    /* 9: 950-1140 (TS 6×) */
      '1140',   /* 10: 1140-1320 (TS 7×) */
      '1320',   /* 11: 1320-1400 */
      '1400',   /* 12: 1400-1430 (WG) */
      '1430',   /* 13: 1430-1460 (VXLAN) */
      '1460',   /* 14: 1460-1480 (GRE) */
      '1480',   /* 15: 1480-1492 (MPLS) */
      '1492',   /* 16: 1492-1500 */
      '1500',   /* 17: 1500-1518 (MTU) */
      '1518',   /* 18: 1518-2000 (VLAN) */
      '≥2K'     /* 19: >=2000 (jumbo) */
    ];
    const FRAME_SIZE_BUCKET_LABELS_FULL = [
      '<64B', '64-100B', '100-160B', '160-220B', '220-300B',
      '300-400B', '400-576B', '576-760B', '760-950B', '950-1140B', '1140-1320B',
      '1320-1400B', '1400-1430B', '1430-1460B', '1460-1480B', '1480-1492B',
      '1492-1500B', '1500-1518B', '1518-2000B', '≥2000B'
    ];

    /* PPS histogram bucket labels - 12 buckets */
    const PPS_BUCKET_LABELS_SHORT = [
      '<10', '10', '50', '100', '500', '1K', '2K', '5K', '10K', '20K', '50K', '>100K'
    ];
    const PPS_BUCKET_LABELS_FULL = [
      '<10 pps', '10-50', '50-100', '100-500', '500-1K', '1K-2K',
      '2K-5K', '5K-10K', '10K-20K', '20K-50K', '50K-100K', '>100K pps'
    ];

    /* Create a visual RTT histogram with log scale for better visualization */
    const createRttHistogram = (hist, samples) => {
      if (!hist || samples === 0) return '';

      /* Use log scale: log(count+1) to handle zeros and make small values visible */
      const logCounts = hist.map(c => Math.log10((c || 0) + 1));
      const maxLog = Math.max(...logCounts, 0.1);

      let histHtml = `<div class="rtt-histogram">
        <div class="rtt-histogram-title">RTT Distribution</div>
        <div class="rtt-histogram-bars">`;

      for (let i = 0; i < 14; i++) {
        const count = hist[i] || 0;
        const pct = (count / samples * 100).toFixed(1);
        /* Log scale height, minimum 0% for empty buckets */
        const heightPct = count > 0 ? Math.max((logCounts[i] / maxLog * 100), 5).toFixed(0) : 0;
        const barClass = count > 0 ? 'rtt-bar' : 'rtt-bar rtt-bar-empty';

        histHtml += `<div class="rtt-bar-container" title="${RTT_BUCKET_LABELS_FULL[i]}: ${count} (${pct}%)">
            <div class="${barClass}" style="height: ${heightPct}%;"></div>
          </div>`;
      }

      histHtml += `</div><div class="rtt-histogram-labels">`;
      for (let i = 0; i < 14; i++) {
        histHtml += `<span>${RTT_BUCKET_LABELS_SHORT[i]}</span>`;
      }
      histHtml += `</div></div>`;
      return histHtml;
    };

    /* Create a visual jitter histogram with log scale (12 buckets) */
    const createJitterHistogram = (hist) => {
      if (!hist) return '';

      /* Calculate total samples from histogram */
      const samples = hist.reduce((sum, c) => sum + (c || 0), 0);
      if (samples === 0) return '';

      /* Use log scale: log(count+1) to handle zeros and make small values visible */
      const logCounts = hist.map(c => Math.log10((c || 0) + 1));
      const maxLog = Math.max(...logCounts, 0.1);

      let histHtml = `<div class="jitter-histogram">
        <div class="jitter-histogram-title">Jitter Distribution (${samples} samples)</div>
        <div class="jitter-histogram-bars">`;

      for (let i = 0; i < 12; i++) {
        const count = hist[i] || 0;
        const pct = (count / samples * 100).toFixed(1);
        /* Log scale height, minimum 0% for empty buckets */
        const heightPct = count > 0 ? Math.max((logCounts[i] / maxLog * 100), 5).toFixed(0) : 0;
        const barClass = count > 0 ? 'jitter-bar' : 'jitter-bar jitter-bar-empty';

        histHtml += `<div class="jitter-bar-container" title="${JITTER_BUCKET_LABELS_FULL[i]}: ${count} (${pct}%)">
            <div class="${barClass}" style="height: ${heightPct}%;"></div>
          </div>`;
      }

      histHtml += `</div><div class="jitter-histogram-labels">`;
      for (let i = 0; i < 12; i++) {
        histHtml += `<span>${JITTER_BUCKET_LABELS_SHORT[i]}</span>`;
      }
      histHtml += `</div></div>`;
      return histHtml;
    };

    /* Create a visual IPG histogram with log scale (12 buckets) - for all flows */
    const createIpgHistogram = (hist, samples, meanUs) => {
      if (!hist || samples === 0) return '';

      /* Use log scale: log(count+1) to handle zeros and make small values visible */
      const logCounts = hist.map(c => Math.log10((c || 0) + 1));
      const maxLog = Math.max(...logCounts, 0.1);

      /* Format mean IPG for display */
      const meanStr = meanUs > 0 ? formatVideoJitter(meanUs) : '-';

      let histHtml = `<div class="ipg-histogram">
        <div class="ipg-histogram-title">Inter-Packet Gap (${samples} samples, mean: ${meanStr})</div>
        <div class="ipg-histogram-bars">`;

      for (let i = 0; i < 12; i++) {
        const count = hist[i] || 0;
        const pct = (count / samples * 100).toFixed(1);
        /* Log scale height, minimum 0% for empty buckets */
        const heightPct = count > 0 ? Math.max((logCounts[i] / maxLog * 100), 5).toFixed(0) : 0;
        const barClass = count > 0 ? 'ipg-bar' : 'ipg-bar ipg-bar-empty';

        histHtml += `<div class="ipg-bar-container" title="${JITTER_BUCKET_LABELS_FULL[i]}: ${count} (${pct}%)">
            <div class="${barClass}" style="height: ${heightPct}%;"></div>
          </div>`;
      }

      histHtml += `</div><div class="ipg-histogram-labels">`;
      for (let i = 0; i < 12; i++) {
        histHtml += `<span>${JITTER_BUCKET_LABELS_SHORT[i]}</span>`;
      }
      histHtml += `</div></div>`;
      return histHtml;
    };

    /* Format bytes for display */
    const formatBytes = (bytes) => {
      if (bytes < 1000) return bytes + ' B';
      if (bytes < 1000000) return (bytes / 1000).toFixed(1) + ' KB';
      return (bytes / 1000000).toFixed(2) + ' MB';
    };

    /* Create a visual frame size histogram (20 buckets) */
    const createFrameSizeHistogram = (hist, samples, mean, variance, min, max) => {
      if (!hist || samples === 0) return '';

      /* Use log scale: log(count+1) to handle zeros and make small values visible */
      const logCounts = hist.map(c => Math.log10((c || 0) + 1));
      const maxLog = Math.max(...logCounts, 0.1);

      /* Calculate standard deviation from variance */
      const stdDev = Math.sqrt(variance || 0);

      let histHtml = `<div class="frame-size-histogram">
        <div class="frame-size-histogram-title">Frame Size (${samples} samples, mean: ${mean}B, min: ${min}B, max: ${max}B)</div>
        <div class="frame-size-histogram-bars">`;

      for (let i = 0; i < 20; i++) {
        const count = hist[i] || 0;
        const pct = (count / samples * 100).toFixed(1);
        /* Log scale height, minimum 0% for empty buckets */
        const heightPct = count > 0 ? Math.max((logCounts[i] / maxLog * 100), 5).toFixed(0) : 0;
        const barClass = count > 0 ? 'frame-size-bar' : 'frame-size-bar frame-size-bar-empty';

        histHtml += `<div class="frame-size-bar-container" title="${FRAME_SIZE_BUCKET_LABELS_FULL[i]}: ${count} (${pct}%)">
            <div class="${barClass}" style="height: ${heightPct}%;"></div>
          </div>`;
      }

      histHtml += `</div><div class="frame-size-histogram-labels">`;
      for (let i = 0; i < 20; i++) {
        histHtml += `<span>${FRAME_SIZE_BUCKET_LABELS_SHORT[i]}</span>`;
      }
      histHtml += `</div></div>`;
      return histHtml;
    };

    /* Create a visual PPS histogram (12 buckets) */
    const createPpsHistogram = (hist, samples, mean, variance) => {
      if (!hist || samples === 0) return '';

      /* Use log scale: log(count+1) to handle zeros and make small values visible */
      const logCounts = hist.map(c => Math.log10((c || 0) + 1));
      const maxLog = Math.max(...logCounts, 0.1);

      /* Calculate standard deviation from variance */
      const stdDev = Math.sqrt(variance || 0);

      let histHtml = `<div class="pps-histogram">
        <div class="pps-histogram-title">Packets/Second (${samples} intervals, mean: ${mean} pps)</div>
        <div class="pps-histogram-bars">`;

      for (let i = 0; i < 12; i++) {
        const count = hist[i] || 0;
        const pct = (count / samples * 100).toFixed(1);
        /* Log scale height, minimum 0% for empty buckets */
        const heightPct = count > 0 ? Math.max((logCounts[i] / maxLog * 100), 5).toFixed(0) : 0;
        const barClass = count > 0 ? 'pps-bar' : 'pps-bar pps-bar-empty';

        histHtml += `<div class="pps-bar-container" title="${PPS_BUCKET_LABELS_FULL[i]}: ${count} (${pct}%)">
            <div class="${barClass}" style="height: ${heightPct}%;"></div>
          </div>`;
      }

      histHtml += `</div><div class="pps-histogram-labels">`;
      for (let i = 0; i < 12; i++) {
        histHtml += `<span>${PPS_BUCKET_LABELS_SHORT[i]}</span>`;
      }
      histHtml += `</div></div>`;
      return histHtml;
    };

    /* Create expandable health details row */
    const createHealthDetailsRow = (fkey, flowData) => {
      const hasRttData = flowData && flowData.health_rtt_samples >= 10;
      const hasIpgData = flowData && flowData.ipg_samples > 0;
      const hasFrameSizeData = flowData && flowData.frame_size_samples > 0;
      const hasPpsData = flowData && flowData.pps_samples > 0;

      if (!hasRttData && !hasIpgData && !hasFrameSizeData && !hasPpsData) {
        return `
          <div class="health-details-content">
            <span class="health-value">Collecting data...</span>
          </div>`;
      }

      let detailsHtml = `<div class="health-details-content">`;

      /* TCP Health section (if we have RTT data) */
      if (hasRttData) {
        const { icon, color } = getHealthIcon(flowData.health_status);
        const flags = flowData.health_flags || 0;

        /* Build issues list or show healthy status */
        if (flags) {
          const issues = [];
          if (flags & TCP_HEALTH_FLAGS.HIGH_TAIL_LATENCY) {
            issues.push('Inconsistent latency (spikes detected)');
          }
          if (flags & TCP_HEALTH_FLAGS.ELEVATED_LOSS) {
            issues.push('Elevated packet loss (>0.5%)');
          }
          if (flags & TCP_HEALTH_FLAGS.HIGH_LOSS) {
            issues.push('High packet loss (>2%)');
          }
          if (flags & TCP_HEALTH_FLAGS.WINDOW_STARVATION) {
            issues.push('Zero window (receiver buffer full)');
          }
          if (flags & TCP_HEALTH_FLAGS.RTO_STALLS) {
            issues.push('RTO stalls detected');
          }

          detailsHtml += `<div class="health-issues-list" style="color: ${color};">`;
          for (const issue of issues) {
            detailsHtml += `<span class="health-issue-item">${icon} ${issue}</span>`;
          }
          detailsHtml += `</div>`;
        } else {
          detailsHtml += `<div style="color: ${color};">${icon} Healthy (${flowData.health_rtt_samples} samples)</div>`;
        }

        /* Add RTT histogram visualization */
        detailsHtml += createRttHistogram(flowData.health_rtt_hist, flowData.health_rtt_samples);
      }

      /* IPG histogram section (for all flows) - time-based metric */
      if (hasIpgData) {
        detailsHtml += createIpgHistogram(flowData.ipg_hist, flowData.ipg_samples, flowData.ipg_mean_us);
      }

      /* PPS histogram section (for all flows) - time-based metric, pairs with IPG */
      if (hasPpsData) {
        detailsHtml += createPpsHistogram(
          flowData.pps_hist,
          flowData.pps_samples,
          flowData.pps_mean,
          flowData.pps_variance
        );
      }

      /* Frame size histogram section (for all flows) - size-based metric */
      if (hasFrameSizeData) {
        detailsHtml += createFrameSizeHistogram(
          flowData.frame_size_hist,
          flowData.frame_size_samples,
          flowData.frame_size_mean,
          flowData.frame_size_variance,
          flowData.frame_size_min,
          flowData.frame_size_max
        );
      }

      detailsHtml += `</div>`;
      return detailsHtml;
    };

    /* H.264 profile names */
    const H264_PROFILES = {
      66: 'Baseline',
      77: 'Main',
      88: 'Extended',
      100: 'High',
      110: 'High 10',
      122: 'High 4:2:2',
      244: 'High 4:4:4'
    };

    /* H.265/HEVC profile names (lower 5 bits = profile, bit 7 = High tier flag) */
    const H265_PROFILES = {
      1: 'Main',
      2: 'Main 10',
      3: 'Main Still Picture',
      4: 'Format Range Extensions',
      5: 'High Throughput',
      6: 'Multiview Main',
      7: 'Scalable Main',
      8: '3D Main',
      9: 'Screen-Extended Main',
      10: 'Scalable Format Range Extensions'
    };

    /* Format video jitter for display */
    const formatVideoJitter = (jitter_us) => {
      if (jitter_us <= 0) return "-";
      if (jitter_us < 1000) return jitter_us.toFixed(0) + " us";
      return (jitter_us / 1000).toFixed(2) + " ms";
    };

    /* Get video type icon character */
    const getVideoIcon = (video_type) => {
      if (video_type === 1) return '\u25B6';  /* RTP: play symbol */
      if (video_type === 2) return '\u25B6';  /* MPEG-TS: play symbol */
      return '';
    };

    /* Format video resolution */
    const formatResolution = (width, height) => {
      if (width > 0 && height > 0) return `${width}x${height}`;
      return '-';
    };

    /* Format video FPS */
    const formatFps = (fps_x100) => {
      if (fps_x100 > 0) return (fps_x100 / 100).toFixed(2) + ' fps';
      return '-';
    };

    /* Format video bitrate */
    const formatBitrateKbps = (kbps) => {
      if (kbps <= 0) return '-';
      if (kbps < 1000) return kbps + ' kbps';
      return (kbps / 1000).toFixed(2) + ' Mbps';
    };

    /* Format H.264 profile/level */
    const formatH264Profile = (profile, level) => {
      const profileName = H264_PROFILES[profile] || `Profile ${profile}`;
      const levelNum = level > 0 ? (level / 10).toFixed(1) : '?';
      return `${profileName}@L${levelNum}`;
    };

    /* Format H.265/HEVC profile/tier/level
     * profile byte: bits 0-4 = profile_idc, bit 7 = High tier flag
     * level byte: level_idc (divide by 30 to get level number, e.g., 120 = L4.0)
     */
    const formatH265Profile = (profile, level) => {
      const tier = (profile & 0x80) ? 'High' : 'Main';
      const profileIdc = profile & 0x1F;
      const profileName = H265_PROFILES[profileIdc] || `Profile ${profileIdc}`;
      /* H.265 level is level_idc / 30, e.g., 120 = 4.0, 153 = 5.1 */
      const levelNum = level > 0 ? (level / 30).toFixed(1) : '?';
      return `${profileName} ${tier}@L${levelNum}`;
    };

    /* Format profile/level based on codec type */
    const formatProfile = (codec, profile, level) => {
      if (profile === 0 && level === 0) return '-';
      /* If level is 0, SPS wasn't fully parsed - show just codec name or "-" */
      if (level === 0) return '-';
      if (codec === 2) return formatH265Profile(profile, level);  /* VIDEO_CODEC_H265 */
      return formatH264Profile(profile, level);  /* Default to H.264 format */
    };

    /* Track expanded video details rows */
    let expandedVideoRows = new Set();

    /* Track expanded audio details rows */
    let expandedAudioRows = new Set();

    /* Track expanded health details rows */
    let expandedHealthRows = new Set();

    /* Build video tooltip text */
    const getVideoTooltip = (flowData) => {
      if (!flowData || flowData.video_type <= 0) return '';
      const type = VIDEO_TYPES[flowData.video_type] || 'Unknown';
      const codec = VIDEO_CODECS[flowData.video_codec] || 'Unknown';
      let tip = `${type} | ${codec}`;
      if (flowData.video_type === 1) {  /* RTP */
        tip += `\nJitter: ${formatVideoJitter(flowData.video_jitter_us)}`;
        tip += `\nSeq Loss: ${flowData.video_seq_loss}`;
        if (flowData.video_ssrc) {
          tip += `\nSSRC: 0x${flowData.video_ssrc.toString(16)}`;
        }
      } else if (flowData.video_type === 2) {  /* MPEG-TS */
        tip += `\nCC Errors: ${flowData.video_cc_errors}`;
      }
      return tip;
    };

    /* Create expandable video details row */
    const createVideoDetailsRow = (fkey, flowData) => {
      const codec = VIDEO_CODECS[flowData.video_codec] || 'Unknown';
      const type = VIDEO_TYPES[flowData.video_type] || 'Unknown';
      const source = CODEC_SOURCES[flowData.video_codec_source] || 'Unknown';

      let detailsHtml = `
        <div class="video-details-content">
          <div class="video-details-row">
            <span class="video-label">Type:</span>
            <span class="video-value">${type}</span>
            <span class="video-label">Codec:</span>
            <span class="video-value">${codec}</span>
            <span class="video-label">Source:</span>
            <span class="video-value">${source}</span>
          </div>
          <div class="video-details-row">
            <span class="video-label">Resolution:</span>
            <span class="video-value">${formatResolution(flowData.video_width, flowData.video_height)}</span>
            <span class="video-label">FPS:</span>
            <span class="video-value video-fps">${formatFps(flowData.video_fps_x100)}</span>
            <span class="video-label">Bitrate:</span>
            <span class="video-value video-bitrate">${formatBitrateKbps(flowData.video_bitrate_kbps)}</span>
          </div>`;

      if (flowData.video_codec === 1 || flowData.video_codec === 2) {
        detailsHtml += `
          <div class="video-details-row">
            <span class="video-label">Profile:</span>
            <span class="video-value">${formatProfile(flowData.video_codec, flowData.video_profile, flowData.video_level)}</span>
            <span class="video-label">GOP:</span>
            <span class="video-value">${flowData.video_gop_frames > 0 ? flowData.video_gop_frames + ' frames' : '-'}</span>
            <span class="video-label">Keyframes:</span>
            <span class="video-value">${flowData.video_keyframes || 0}</span>
          </div>`;
      }

      if (flowData.video_type === 1) {  /* RTP */
        detailsHtml += `
          <div class="video-details-row">
            <span class="video-label">SSRC:</span>
            <span class="video-value">0x${(flowData.video_ssrc || 0).toString(16).toUpperCase()}</span>
            <span class="video-label">Jitter:</span>
            <span class="video-value video-jitter" data-fkey="${fkey}">${formatVideoJitter(flowData.video_jitter_us)}</span>
            <span class="video-label">Seq Loss:</span>
            <span class="video-value video-seq-loss" data-fkey="${fkey}">${flowData.video_seq_loss || 0}</span>
          </div>`;
        /* Add jitter histogram visualization */
        detailsHtml += createJitterHistogram(flowData.video_jitter_hist);
      } else if (flowData.video_type === 2) {  /* MPEG-TS */
        detailsHtml += `
          <div class="video-details-row">
            <span class="video-label">CC Errors:</span>
            <span class="video-value video-cc-errors" data-fkey="${fkey}">${flowData.video_cc_errors || 0}</span>
            <span class="video-label">Frames:</span>
            <span class="video-value">${flowData.video_frames || 0}</span>
          </div>`;
      }

      detailsHtml += `
          <div class="video-details-row video-actions">
            <button class="btn btn-sm btn-success webrtc-btn" data-fkey="${fkey}">
              <i class="fas fa-play"></i> Play Video
            </button>
          </div>
        </div>`;

      return detailsHtml;
    };

    /* Create expandable audio details row */
    /* Get sample rate based on audio codec (server may not send audio_sample_rate) */
    const getAudioSampleRate = (codec, serverRate) => {
      if (serverRate && serverRate > 0) return serverRate + ' kHz';
      /* Default sample rates for known codecs */
      switch (codec) {
        case 1:  /* PCMU */
        case 2:  /* PCMA */
        case 3:  /* G729 */
          return '8 kHz';
        case 4:  /* Opus */
          return '48 kHz';
        case 5:  /* AAC */
          return '44.1 kHz';
        default:
          return '-';
      }
    };

    /* Format audio bitrate from kbps value (server-calculated) */
    const formatAudioBitrateKbps = (kbps) => {
      if (kbps === undefined || kbps === null || isNaN(kbps) || kbps <= 0) {
        return '-';
      }
      if (kbps < 1000) return kbps.toFixed(0) + ' kbps';
      return (kbps / 1000).toFixed(2) + ' Mbps';
    };

    const createAudioDetailsRow = (fkey, flowData) => {
      const codec = AUDIO_CODECS[flowData.audio_codec] || 'Unknown';
      const sampleRate = getAudioSampleRate(flowData.audio_codec, flowData.audio_sample_rate);
      /* Audio jitter from server (if tracked) */
      const jitterDisplay = flowData.audio_jitter_us > 0 ? formatVideoJitter(flowData.audio_jitter_us) : 'N/A';
      /* Use server-calculated audio bitrate */
      const audioBitrate = formatAudioBitrateKbps(flowData.audio_bitrate_kbps);

      let detailsHtml = `
        <div class="audio-details-content">
          <div class="audio-details-row">
            <span class="audio-label">Type:</span>
            <span class="audio-value">RTP Audio</span>
            <span class="audio-label">Codec:</span>
            <span class="audio-value">${codec}</span>
            <span class="audio-label">Sample Rate:</span>
            <span class="audio-value">${sampleRate}</span>
          </div>
          <div class="audio-details-row">
            <span class="audio-label">Bitrate:</span>
            <span class="audio-value audio-bitrate" data-fkey="${fkey}">${audioBitrate}</span>
            <span class="audio-label">Jitter:</span>
            <span class="audio-value audio-jitter" data-fkey="${fkey}">${jitterDisplay}</span>
            <span class="audio-label">Seq Loss:</span>
            <span class="audio-value audio-seq-loss" data-fkey="${fkey}">${flowData.audio_seq_loss || 0}</span>
          </div>
          <div class="audio-details-row">
            <span class="audio-label">SSRC:</span>
            <span class="audio-value">0x${(flowData.audio_ssrc || 0).toString(16).toUpperCase()}</span>
          </div>
        </div>`;

      return detailsHtml;
    };

    /* Handle WebRTC video playback */
    const startWebrtcPlayback = (fkey, flowData) => {
      if (JT.webrtc && JT.webrtc.isSupported()) {
        JT.webrtc.startPlayback(fkey, flowData);
      } else {
        alert('WebRTC is not supported in your browser.');
      }
    };

    /* Update only the dynamic values in the legend without rebuilding DOM */
    const updateLegendValues = (flowDataMap) => {
      const legendContainer = d3.select("#toptalkLegendContainer");

      /* Update RTT values */
      legendContainer.selectAll(".rtt-value").each(function() {
        const fkey = d3.select(this).attr("data-fkey");
        const flowData = flowDataMap.get(fkey);
        if (flowData) {
          d3.select(this).text(formatRtt(flowData.rtt_us));
        }
      });

      /* Update bitrate values */
      legendContainer.selectAll(".bitrate-value").each(function() {
        const fkey = d3.select(this).attr("data-fkey");
        if (fkey === 'other') return; /* Skip "other" row */
        const flowData = flowDataMap.get(fkey);
        if (flowData) {
          d3.select(this).text(formatBitrate(flowData.current_bytes * 8));
        }
      });

      /* Update health icons - differentiate TCP vs UDP using unified function */
      legendContainer.selectAll(".health-icon").each(function() {
        const fkey = d3.select(this).attr("data-fkey");
        const flowData = flowDataMap.get(fkey);
        if (!flowData) return;

        const isExpanded = expandedHealthRows.has(fkey);
        const isTcp = fkey.indexOf('/TCP/') !== -1;
        const iconInfo = getHealthIconForFlow(flowData, isTcp, isExpanded);

        /* Determine title based on expanded state and data availability */
        let title = '';
        if (iconInfo.clickable) {
          if (isExpanded) {
            title = isTcp ? CLICK_TITLES.collapseHealth : CLICK_TITLES.collapseTiming;
          } else {
            title = isTcp ? CLICK_TITLES.expandHealth : CLICK_TITLES.expandTiming;
          }
        }

        d3.select(this)
          .text(iconInfo.icon)
          .style("color", iconInfo.color)
          .style("cursor", iconInfo.cursor)
          .attr("title", title);
      });

      /* Update video/audio icons - may become available after flow first appears */
      legendContainer.selectAll(".video-icon").each(function() {
        const fkey = d3.select(this).attr("data-fkey");
        const flowData = flowDataMap.get(fkey);
        if (!flowData) return;

        const videoType = flowData.video_type || 0;
        const audioType = flowData.audio_type || 0;
        const elem = d3.select(this);

        if (videoType > 0) {
          /* Video stream - show appropriate icon */
          const isExpanded = expandedVideoRows.has(fkey);
          elem
            .text(isExpanded ? '\u25BC' : '\u25B6')
            .classed("video-stream-icon", true)
            .classed("audio-stream-icon", false)
            .attr("title", isExpanded ? CLICK_TITLES.collapseVideo : CLICK_TITLES.expandVideo)
            .style("color", "#e74c3c")
            .style("cursor", "pointer");
        } else if (audioType > 0) {
          /* Audio-only stream - show musical note */
          const audioCodec = AUDIO_CODECS[flowData.audio_codec] || 'Audio';
          elem
            .text('\u266B')
            .classed("video-stream-icon", false)
            .classed("audio-stream-icon", true)
            .attr("title", `RTP Audio: ${audioCodec}`)
            .style("color", "#3498db")
            .style("cursor", "default");
        } else {
          /* No video/audio - clear icon */
          elem
            .text('-')
            .classed("video-stream-icon", false)
            .classed("audio-stream-icon", false)
            .attr("title", "")
            .style("color", "")
            .style("cursor", "default");
        }
      });

      /* Update expanded video detail rows */
      expandedVideoRows.forEach(fkey => {
        const flowData = flowDataMap.get(fkey);
        if (flowData) {
          const detailsRow = legendContainer.select(`.video-details-row-container[data-fkey="${fkey}"]`);
          if (!detailsRow.empty()) {
            /* Update dynamic video values */
            detailsRow.select(".video-jitter").text(formatVideoJitter(flowData.video_jitter_us));
            detailsRow.select(".video-seq-loss").text(flowData.video_seq_loss || 0);
            detailsRow.select(".video-cc-errors").text(flowData.video_cc_errors || 0);

            /* Update bitrate and FPS which may change over time */
            const bitrateEl = detailsRow.select(".video-bitrate");
            if (!bitrateEl.empty()) {
              bitrateEl.text(formatBitrateKbps(flowData.video_bitrate_kbps));
            }
            const fpsEl = detailsRow.select(".video-fps");
            if (!fpsEl.empty()) {
              fpsEl.text(formatFps(flowData.video_fps_x100));
            }
          }
        }
      });

      /* Update expanded audio detail rows */
      expandedAudioRows.forEach(fkey => {
        const flowData = flowDataMap.get(fkey);
        if (flowData) {
          const detailsRow = legendContainer.select(`.audio-details-row-container[data-fkey="${fkey}"]`);
          if (!detailsRow.empty()) {
            /* Update dynamic audio values */
            detailsRow.select(".audio-bitrate").text(formatAudioBitrateKbps(flowData.audio_bitrate_kbps));
            const jitterDisplay = flowData.audio_jitter_us > 0 ? formatVideoJitter(flowData.audio_jitter_us) : 'N/A';
            detailsRow.select(".audio-jitter").text(jitterDisplay);
            detailsRow.select(".audio-seq-loss").text(flowData.audio_seq_loss || 0);
          }
        }
      });

      /* Update expanded health detail rows - only if data changed */
      expandedHealthRows.forEach(fkey => {
        const flowData = flowDataMap.get(fkey);
        if (flowData) {
          const detailsRow = legendContainer.select(`.health-details-row-container[data-fkey="${fkey}"]`);
          if (!detailsRow.empty()) {
            /* Check if data has changed by comparing sample counts */
            const prevSamples = detailsRow.attr('data-samples');
            const currentSamples = `${flowData.health_rtt_samples || 0},${flowData.ipg_samples || 0},${flowData.frame_size_samples || 0},${flowData.pps_samples || 0}`;
            if (prevSamples !== currentSamples) {
              /* Rebuild the entire health details content only when data changes */
              detailsRow.html(createHealthDetailsRow(fkey, flowData));
              detailsRow.attr('data-samples', currentSamples);
            }
          }
        }
      });
    };

    /* Update legend row visual state based on selection */
    const updateLegendSelection = () => {
      const selectedKey = my.charts.getSelectedFlow();
      const legendContainer = d3.select("#toptalkLegendContainer");

      legendContainer.selectAll('.toptalk-legend-row').each(function() {
        const row = d3.select(this);
        const fkey = row.attr('data-fkey');

        row.classed('selected', selectedKey === fkey);
        row.classed('deselected', selectedKey !== null && selectedKey !== fkey);
      });
    };

    /* Update the legend DOM - only called when flows change */
    const updateLegend = (fkeys, flowDataMap) => {
      const legendContainer = d3.select("#toptalkLegendContainer");

      /*
       * Strategy: Instead of removing all rows, we check if existing rows match.
       * If the set of fkeys changed, rebuild. Otherwise just update values.
       */
      const existingFkeys = [];
      legendContainer.selectAll(".legend-row").each(function() {
        const fkey = d3.select(this).attr("data-fkey");
        if (fkey) existingFkeys.push(fkey);
      });

      /* Check if fkeys are identical (same elements AND same order).
       * Order matters because colorScale.domain(fkeys) assigns colors by position.
       * If flows reorder, we must rebuild the legend to update color boxes. */
      const fkeysMatch = fkeys.length === existingFkeys.length &&
                         fkeys.every((k, i) => k === existingFkeys[i]);

      if (fkeysMatch) {
        /* Same flows in same order - just update values in place (throttled) */
        const now = performance.now();
        if (now - lastLegendUpdateTime >= LEGEND_UPDATE_INTERVAL_MS) {
          lastLegendUpdateTime = now;
          updateLegendValues(flowDataMap);
        }
        return;
      }

      // Clean up expanded state for flows no longer in the set
      const newFkeySet = new Set(fkeys);
      for (const fkey of expandedVideoRows) {
        if (!newFkeySet.has(fkey)) expandedVideoRows.delete(fkey);
      }
      for (const fkey of expandedAudioRows) {
        if (!newFkeySet.has(fkey)) expandedAudioRows.delete(fkey);
      }
      for (const fkey of expandedHealthRows) {
        if (!newFkeySet.has(fkey)) expandedHealthRows.delete(fkey);
      }

      // Data join for rows - use D3 enter/exit pattern to minimize DOM churn
      const rows = legendContainer.selectAll(".legend-row")
        .data(fkeys, d => d);

      // EXIT: Remove rows no longer in data, along with their detail containers
      rows.exit().each(function(d) {
        const fkey = d;
        // Remove associated detail row containers before removing the legend row
        legendContainer.select(`.video-details-row-container[data-fkey="${fkey}"]`).remove();
        legendContainer.select(`.audio-details-row-container[data-fkey="${fkey}"]`).remove();
        legendContainer.select(`.health-details-row-container[data-fkey="${fkey}"]`).remove();
      }).remove();

      // ENTER: Add new rows only
      const rowsEnter = rows.enter()
        .append("div")
        .attr("class", "legend-row toptalk-legend-row d-flex align-items-center mb-1 legend-text")
        .attr("data-fkey", d => d)
        .on("click", function(event, d) {
          // Toggle flow selection on row click
          const fkey = d;
          const currentSelection = my.charts.getSelectedFlow();

          if (currentSelection === fkey) {
            // Toggle off - clicking same flow clears selection
            my.charts.clearSelectedFlow();
          } else {
            // Select this flow
            my.charts.setSelectedFlow(fkey);
          }

          // Update legend row visual state
          updateLegendSelection();
        });

      // Color box
      rowsEnter.append("div")
        .classed("legend-color-box flex-shrink-0", true)
        .style("background-color", d => getFlowColor(d));

      // Content
      rowsEnter.each(function(d) {
        const row = d3.select(this);
        if (d === 'other') {
          row.append("div").classed("col", true).style("padding-left", "10px").text("Other Flows");
          row.append("div").style("width", "4%").classed("flex-shrink-0 text-center", true).text("");  /* Video icon placeholder */
          row.append("div").style("width", "4%").classed("flex-shrink-0 text-center", true).text("-"); /* Health placeholder */
          row.append("div").style("width", "8%").classed("flex-shrink-0 text-right pr-2", true).text("-"); /* RTT placeholder */
          row.append("div").style("width", "9%").classed("flex-shrink-0 text-right pr-2 bitrate-value", true).attr("data-fkey", d).text("-"); /* Bitrate placeholder */
        } else {
          const flow = parseFlowKey(d);
          const flowData = flowDataMap.get(d);
          const rtt = flowData ? flowData.rtt_us : -1;
          const videoType = flowData ? flowData.video_type : 0;

          row.append("div").style("width", "30%").classed("text-right pr-2", true).style("white-space", "nowrap").text(flow.sourceIP + ":" + flow.sourcePort);
          row.append("div").style("width", "5%").classed("text-center flex-shrink-0", true).text("->");
          row.append("div").style("width", "30%").classed("text-left pl-2", true).style("white-space", "nowrap").text(flow.destIP + ":" + flow.destPort);
          row.append("div").style("width", "8%").classed("flex-shrink-0", true).text(flow.proto);
          row.append("div").style("width", "6%").classed("flex-shrink-0", true).text(flow.tclass);

          /* Video/Audio icon with tooltip and click handler for expand/collapse */
          const videoCell = row.append("div")
            .style("width", "4%")
            .classed("flex-shrink-0 text-center video-icon", true)
            .attr("data-fkey", d);

          const audioType = flowData ? flowData.audio_type : 0;

          if (videoType > 0) {
            const isExpanded = expandedVideoRows.has(d);
            videoCell
              .text(isExpanded ? '\u25BC' : '\u25B6')  /* Down arrow if expanded, right arrow if collapsed */
              .classed("video-stream-icon", true)
              .attr("title", "Click to " + (isExpanded ? "collapse" : "expand") + " video details")
              .style("color", "#e74c3c")
              .style("cursor", "pointer")
              .on("click", function(event) {
                event.stopPropagation();
                const fkey = d3.select(this).attr("data-fkey");
                const detailsRow = d3.select(`.video-details-row-container[data-fkey="${fkey}"]`);

                if (expandedVideoRows.has(fkey)) {
                  /* Collapse */
                  expandedVideoRows.delete(fkey);
                  detailsRow.remove();
                  d3.select(this).text('\u25B6').attr("title", CLICK_TITLES.expandVideo);
                } else {
                  /* Expand */
                  expandedVideoRows.add(fkey);
                  const fd = currentFlowDataMap.get(fkey);
                  if (fd) {
                    const parentRow = d3.select(this.parentNode);
                    const detailsDiv = legendContainer.insert("div", function() {
                      return parentRow.node().nextSibling;
                    })
                      .attr("class", "video-details-row-container")
                      .attr("data-fkey", fkey)
                      .html(createVideoDetailsRow(fkey, fd));

                    /* Attach click handler for WebRTC button */
                    detailsDiv.select(".webrtc-btn").on("click", function(event) {
                      event.stopPropagation();
                      const btnFkey = d3.select(this).attr("data-fkey");
                      const btnFlowData = currentFlowDataMap.get(btnFkey);
                      if (btnFlowData) {
                        startWebrtcPlayback(btnFkey, btnFlowData);
                      }
                    });
                  }
                  d3.select(this).text('\u25BC').attr("title", CLICK_TITLES.collapseVideo);
                }
              });

            /* If row was previously expanded, re-create details row right after this row */
            if (isExpanded) {
              const fd = flowDataMap.get(d);
              if (fd) {
                /* Insert details row immediately after the current legend row */
                const detailsDiv = legendContainer.insert("div", function() {
                  return row.node().nextSibling;
                })
                  .attr("class", "video-details-row-container")
                  .attr("data-fkey", d)
                  .html(createVideoDetailsRow(d, fd));

                /* Attach click handler for WebRTC button */
                detailsDiv.select(".webrtc-btn").on("click", function(event) {
                  event.stopPropagation();
                  const btnFkey = d3.select(this).attr("data-fkey");
                  const btnFlowData = currentFlowDataMap.get(btnFkey);
                  if (btnFlowData) {
                    startWebrtcPlayback(btnFkey, btnFlowData);
                  }
                });
              }
            }
          } else if (audioType > 0) {
            /* Audio-only stream - show musical note with click to expand */
            const audioCodec = AUDIO_CODECS[flowData.audio_codec] || 'Audio';
            const isExpanded = expandedAudioRows.has(d);
            videoCell
              .text(isExpanded ? '\u25BC' : '\u266B')  /* Down arrow if expanded, musical note if collapsed */
              .classed("audio-stream-icon", true)
              .attr("title", isExpanded ? CLICK_TITLES.collapseAudio : CLICK_TITLES.expandAudio)
              .style("color", "#3498db")
              .style("cursor", "pointer")
              .on("click", function(event) {
                event.stopPropagation();
                const fkey = d3.select(this).attr("data-fkey");
                const detailsRow = d3.select(`.audio-details-row-container[data-fkey="${fkey}"]`);

                if (expandedAudioRows.has(fkey)) {
                  /* Collapse */
                  expandedAudioRows.delete(fkey);
                  detailsRow.remove();
                  d3.select(this).text('\u266B').attr("title", CLICK_TITLES.expandAudio);
                } else {
                  /* Expand */
                  expandedAudioRows.add(fkey);
                  const fd = currentFlowDataMap.get(fkey);
                  if (fd) {
                    const parentRow = d3.select(this.parentNode);
                    legendContainer.insert("div", function() {
                      return parentRow.node().nextSibling;
                    })
                      .attr("class", "audio-details-row-container")
                      .attr("data-fkey", fkey)
                      .html(createAudioDetailsRow(fkey, fd));
                  }
                  d3.select(this).text('\u25BC').attr("title", CLICK_TITLES.collapseAudio);
                }
              });

            /* If row was previously expanded, re-create details row right after this row */
            if (isExpanded) {
              const fd = flowDataMap.get(d);
              if (fd) {
                /* Insert details row immediately after the current legend row */
                legendContainer.insert("div", function() {
                  return row.node().nextSibling;
                })
                  .attr("class", "audio-details-row-container")
                  .attr("data-fkey", d)
                  .html(createAudioDetailsRow(d, fd));
              }
            }
          }

          /* Health icon column */
          const healthCell = row.append("div")
            .style("width", "4%")
            .classed("flex-shrink-0 text-center health-icon", true)
            .attr("data-fkey", d);

          const proto = flow.proto ? flow.proto.toLowerCase() : '';
          const isTcp = proto === 'tcp';
          const isExpanded = expandedHealthRows.has(d);

          /* Use unified function to determine icon state */
          const iconInfo = getHealthIconForFlow(flowData, isTcp, isExpanded);

          healthCell
            .text(iconInfo.icon)
            .style("color", iconInfo.color)
            .style("cursor", iconInfo.cursor)
            .attr("title", iconInfo.clickable
              ? (isExpanded
                ? (isTcp ? CLICK_TITLES.collapseHealth : CLICK_TITLES.collapseTiming)
                : (isTcp ? CLICK_TITLES.expandHealth : CLICK_TITLES.expandTiming))
              : "");

          if (iconInfo.clickable) {
            healthCell
              .attr("data-has-handler", "true")
              .on("click", function(event) {
                event.stopPropagation();
                const fkey = d3.select(this).attr("data-fkey");
                const detailsRow = d3.select(`.health-details-row-container[data-fkey="${fkey}"]`);

                if (expandedHealthRows.has(fkey)) {
                  /* Collapse */
                  expandedHealthRows.delete(fkey);
                  detailsRow.remove();
                  const fd = currentFlowDataMap.get(fkey);
                  const fdIsTcp = fkey.indexOf('/TCP/') !== -1;
                  const collapsedIconInfo = getHealthIconForFlow(fd, fdIsTcp, false);
                  d3.select(this).text(collapsedIconInfo.icon).style("color", collapsedIconInfo.color).attr("title", fdIsTcp ? CLICK_TITLES.expandHealth : CLICK_TITLES.expandTiming);
                } else {
                  /* Expand */
                  expandedHealthRows.add(fkey);
                  const fd = currentFlowDataMap.get(fkey);
                  if (fd) {
                    const parentRow = d3.select(this.parentNode);
                    const samples = `${fd.health_rtt_samples || 0},${fd.ipg_samples || 0},${fd.frame_size_samples || 0},${fd.pps_samples || 0}`;
                    legendContainer.insert("div", function() {
                      return parentRow.node().nextSibling;
                    })
                      .attr("class", "health-details-row-container")
                      .attr("data-fkey", fkey)
                      .attr("data-samples", samples)
                      .html(createHealthDetailsRow(fkey, fd));
                  }
                  d3.select(this).text('\u25BC').style("color", "#333").attr("title", isTcp ? CLICK_TITLES.collapseHealth : CLICK_TITLES.collapseTiming);
                }
              });

            /* If row was previously expanded, re-create details row right after this row */
            if (isExpanded) {
              const fd = flowDataMap.get(d);
              if (fd) {
                /* Insert details row immediately after the current legend row */
                const samples = `${fd.health_rtt_samples || 0},${fd.ipg_samples || 0},${fd.frame_size_samples || 0},${fd.pps_samples || 0}`;
                legendContainer.insert("div", function() {
                  return row.node().nextSibling;
                })
                  .attr("class", "health-details-row-container")
                  .attr("data-fkey", d)
                  .attr("data-samples", samples)
                  .html(createHealthDetailsRow(d, fd));
              }
            }
          }

          row.append("div").style("width", "8%").classed("flex-shrink-0 text-right pr-2 rtt-value", true).attr("data-fkey", d).text(formatRtt(rtt));

          /* Bitrate column - updated each redraw cycle */
          const currentBytes = flowData ? flowData.current_bytes : 0;
          row.append("div").style("width", "9%").classed("flex-shrink-0 text-right pr-2 bitrate-value", true).attr("data-fkey", d).text(formatBitrate(currentBytes * 8));
        }
      });

      // UPDATE: Merge enter + existing to update color boxes on ALL rows
      // This ensures colors stay correct when flow order changes
      const allRows = rowsEnter.merge(rows);
      allRows.select(".legend-color-box")
        .style("background-color", d => getFlowColor(d));

      // Reorder DOM elements to match data order
      // D3's data join preserves object constancy but not DOM order
      allRows.order();

      // Apply selection styling after legend rebuild
      updateLegendSelection();
    };


    /* Update the chart (try to avoid memory allocations here!) */
    m.redraw = function() {

      // Process the raw chartData to aggregate "other" flows before drawing
      const processedChartData = processAndAggregateChartData(chartData);

      const width = size.width - margin.left - margin.right;
      const height = size.height - margin.top - margin.bottom;

      // Update xScale range instead of recreating
      xScale.range([0, width]);

      const { formattedData, maxSlice } = formatDataAndGetMaxSlice(processedChartData);

      /* Get domain parameters from shared utility for smooth scrolling */
      const domain = JT.core.getChartDomain();

      if (processedChartData && processedChartData[0]) {
        if (domain.isValid) {
          xScale.domain([domain.xMin, domain.xMax]);
        } else {
          // Fallback: use data extent during initial load
          xScale.domain(d3.extent(processedChartData[0].values, d => d.ts));
        }
      }

      // Update time formatter: 0 = now (currentTime), negative = past (oscilloscope style)
      if (domain.isValid) {
        // Generate tick positions at ROUND intervals for clean axis labels
        const tickInterval = Math.max(1, Math.ceil(domain.visibleWidthSec / 10)); // ~10 ticks

        // Generate ticks at round relative-time positions (0, -20, -40, etc.)
        tickValuesCache.length = 0;  // Reuse array
        for (let relativeTime = 0; relativeTime >= -domain.windowWidthSec && tickValuesCache.length < 15; relativeTime -= tickInterval) {
          const absoluteTime = domain.currentTime + relativeTime;
          if (absoluteTime >= domain.xMin && absoluteTime <= domain.xMax) {
            tickValuesCache.unshift(absoluteTime);
          }
        }

        // Set explicit tick values to prevent scrolling
        xAxis.tickValues(tickValuesCache);
        xGrid.tickValues(tickValuesCache);

        // Use pre-defined formatter (update cached value for use in formatter)
        cachedMaxTimestamp = domain.currentTime;
        xAxis.tickFormat(xTickFormatter);
      }

      const yPow = d3.select('input[name="y-axis-is-log"]:checked').node().value;

      // Check if we need to switch scale types
      const needsPowerScale = (yPow == 1);
      const isPowerScale = yScale.exponent !== undefined;

      if (needsPowerScale !== isPowerScale) {
        // Only recreate if switching between linear and power
        if (needsPowerScale) {
          yScale = d3.scalePow().exponent(0.5).clamp(true);
        } else {
          yScale = d3.scaleLinear().clamp(true);
        }
      }

      // Update range and domain
      yScale.range([height, 0]).domain([0, maxSlice]);

      xAxis.scale(xScale);
      yAxis.scale(yScale);

      xGrid.scale(xScale);
      yGrid.scale(yScale);

      svg = d3.select("#chartToptalk");

      // Use cached selections (avoids cycle collector pressure)
      cachedSelections.xAxis.call(xAxis);
      cachedSelections.yAxis.call(yAxis);
      cachedSelections.xGrid.call(xGrid);
      cachedSelections.yGrid.call(yGrid);

      // Reuse fkeys array instead of creating new one with map()
      reusableFkeys.length = 0;
      for (let i = 0; i < processedChartData.length; i++) {
        reusableFkeys.push(processedChartData[i].fkey);
      }
      const fkeys = reusableFkeys;  // Local alias for compatibility with rest of function
      colorScale.domain(fkeys); // Set the domain for the ordinal scale
      // Update cached domain Set for O(1) lookup in getFlowColor
      colorDomainSet.clear();
      for (let i = 0; i < fkeys.length; i++) {
        colorDomainSet.add(fkeys[i]);
      }

      // Use custom stack that reuses arrays instead of d3.stack (which allocates)
      const stackedChartData = customStack(formattedData, fkeys);

      currentStackedData = stackedChartData; // Expose for hit-testing

      // Area generator is initialized in reset(), just clear and draw
      context.clearRect(0, 0, width, height);

      // Get selected flow for highlighting
      const selectedKey = my.charts.getSelectedFlow();

      // Use for loop instead of forEach to avoid closure allocation
      for (let layerIdx = 0; layerIdx < stackedChartData.length; layerIdx++) {
        const layer = stackedChartData[layerIdx];
        // Set opacity based on selection state
        if (selectedKey === null) {
          context.globalAlpha = 1.0;  // No selection, full opacity
        } else if (layer.key === selectedKey) {
          context.globalAlpha = 1.0;  // Selected flow, full opacity
        } else {
          context.globalAlpha = 0.3;  // Non-selected, de-emphasized
        }

        context.beginPath();
        area(layer);
        context.fillStyle = getFlowColor(layer.key);
        context.fill();
      }
      context.globalAlpha = 1.0;  // Reset for other drawing

      // distribution bar - use for loop instead of reduce() to avoid closure allocation
      let tbytes = 0;
      for (let i = 0; i < processedChartData.length; i++) {
        tbytes += processedChartData[i].tbytes;
      }

      // Reuse barData array and objects instead of map()
      let rangeStop = 0;
      for (let i = 0; i < processedChartData.length; i++) {
        const f = processedChartData[i];
        if (!reusableBarData[i]) {
          reusableBarData[i] = { k: '', x0: 0, x1: 0 };
        }
        reusableBarData[i].k = f.fkey;
        reusableBarData[i].x0 = rangeStop;
        reusableBarData[i].x1 = rangeStop + f.tbytes;
        rangeStop = reusableBarData[i].x1;
      }
      reusableBarData.length = processedChartData.length;  // Trim excess
      const barData = reusableBarData;  // Local alias for compatibility

      // Reuse bar scale (check if already exists) instead of creating new every frame
      if (!currentBarScale) {
        currentBarScale = d3.scaleLinear();
      }
      currentBarScale.rangeRound([0, width]).domain([0, tbytes]);
      const x = currentBarScale;  // Local alias for compatibility

      // Store for click handler
      currentBarData = barData;

      // Note: y scaleBand is created each frame but has minimal overhead
      // since it's only used for the distribution bar height (constant 10px)

      const barsbox = cachedSelections.barsbox;

      /* Use D3 data join pattern - only add/remove bars when data changes */
      const bars = barsbox.selectAll(".subbar")
          .data(barData, d => d.k);  /* Key function for object constancy */

      /* EXIT - remove bars no longer in data */
      bars.exit().remove();

      /* ENTER - add new bars only */
      const barsEnter = bars.enter()
          .append("g")
          .attr("class", "subbar");

      barsEnter.append("rect")
          .attr("height", 12)
          .attr("y", 9)
          .style("cursor", "pointer");

      /* UPDATE - update positions/styles on all bars (enter + existing) */
      const allBars = barsEnter.merge(bars);

      allBars.select("rect")
          .attr("x", d => x(d.x0))
          .attr("width", d => x(d.x1) - x(d.x0))
          .attr("data-fkey", d => d.k)
          .style("fill", d => getFlowColor(d.k))
          .style("stroke", d => d.k === selectedKey ? "#000" : "none")
          .style("stroke-width", d => d.k === selectedKey ? "2px" : "0");

      barsbox.attr("transform",
                   "translate(" + margin.left + "," + (height + 55) + ")");

      // Build flow data map for RTT and video lookup in legend
      // Reuse Map and pooled objects to reduce GC pressure
      reusableFlowDataMap.clear();
      for (let i = 0; i < processedChartData.length; i++) {
        const f = processedChartData[i];
        /* Get current bytes/sec rate from latest time slice for bitrate display */
        const currentBytes = (f.values && f.values.length > 0)
          ? f.values[f.values.length - 1].bytes
          : 0;
        // Get or create pooled flow data object
        if (!reusableFlowDataObjects[i]) {
          reusableFlowDataObjects[i] = {};
        }
        const flowData = reusableFlowDataObjects[i];
        // Update properties in place
        flowData.current_bytes = currentBytes;
        flowData.rtt_us = f.rtt_us;
        flowData.video_type = f.video_type || 0;
        flowData.video_codec = f.video_codec || 0;
        flowData.video_jitter_us = f.video_jitter_us || 0;
        flowData.video_jitter_hist = f.video_jitter_hist || DEFAULT_VIDEO_JITTER_HIST;
        flowData.video_seq_loss = f.video_seq_loss || 0;
        flowData.video_cc_errors = f.video_cc_errors || 0;
        flowData.video_ssrc = f.video_ssrc || 0;
        /* Extended video telemetry fields */
        flowData.video_codec_source = f.video_codec_source || 0;
        flowData.video_width = f.video_width || 0;
        flowData.video_height = f.video_height || 0;
        flowData.video_profile = f.video_profile || 0;
        flowData.video_level = f.video_level || 0;
        flowData.video_fps_x100 = f.video_fps_x100 || 0;
        flowData.video_bitrate_kbps = f.video_bitrate_kbps || 0;
        flowData.video_gop_frames = f.video_gop_frames || 0;
        flowData.video_keyframes = f.video_keyframes || 0;
        flowData.video_frames = f.video_frames || 0;
        /* Audio stream fields */
        flowData.audio_type = f.audio_type || 0;
        flowData.audio_codec = f.audio_codec || 0;
        flowData.audio_sample_rate = f.audio_sample_rate || 0;
        flowData.audio_jitter_us = f.audio_jitter_us || 0;
        flowData.audio_seq_loss = f.audio_seq_loss || 0;
        flowData.audio_ssrc = f.audio_ssrc || 0;
        flowData.audio_bitrate_kbps = f.audio_bitrate_kbps || 0;
        /* TCP health fields */
        flowData.health_rtt_hist = f.health_rtt_hist || DEFAULT_HEALTH_RTT_HIST;
        flowData.health_rtt_samples = f.health_rtt_samples || 0;
        flowData.health_status = f.health_status || 0;
        flowData.health_flags = f.health_flags || 0;
        /* IPG histogram (all flows) */
        flowData.ipg_hist = f.ipg_hist || DEFAULT_IPG_HIST;
        flowData.ipg_samples = f.ipg_samples || 0;
        flowData.ipg_mean_us = f.ipg_mean_us || 0;
        /* Frame size histogram (all flows) */
        flowData.frame_size_hist = f.frame_size_hist || null;
        flowData.frame_size_samples = f.frame_size_samples || 0;
        flowData.frame_size_mean = f.frame_size_mean || 0;
        flowData.frame_size_variance = f.frame_size_variance || 0;
        flowData.frame_size_min = f.frame_size_min || 0;
        flowData.frame_size_max = f.frame_size_max || 0;
        /* PPS histogram (all flows) */
        flowData.pps_hist = f.pps_hist || null;
        flowData.pps_samples = f.pps_samples || 0;
        flowData.pps_mean = f.pps_mean || 0;
        flowData.pps_variance = f.pps_variance || 0;
        reusableFlowDataMap.set(f.fkey, flowData);
      }
      const flowDataMap = reusableFlowDataMap;  // Local alias for compatibility
      currentFlowDataMap = flowDataMap; // Store for legend use

      // Update legend when flow set OR order changes (order affects color assignment)
      const fkeysChanged = fkeys.length !== cachedFkeys.length ||
                           !fkeys.every((k, i) => k === cachedFkeys[i]);
      if (fkeysChanged) {
        updateLegend(fkeys, flowDataMap);
        // Copy fkeys to cachedFkeys in place (avoids slice() allocation)
        cachedFkeys.length = fkeys.length;
        for (let i = 0; i < fkeys.length; i++) {
          cachedFkeys[i] = fkeys[i];
        }
      } else {
        // Update RTT, video, and health values (throttled - no need for 60Hz updates)
        const now = performance.now();
        if (now - lastLegendUpdateTime >= LEGEND_UPDATE_INTERVAL_MS) {
          lastLegendUpdateTime = now;

          // Use for loop instead of forEach to avoid closure allocation
          for (let fi = 0; fi < processedChartData.length; fi++) {
            const f = processedChartData[fi];
            if (f.fkey !== 'other') {
              const rttElem = d3.select(`.rtt-value[data-fkey="${f.fkey}"]`);
              if (!rttElem.empty()) {
                rttElem.text(formatRtt(f.rtt_us));
              }

              // Update video/audio icon and tooltip
              const videoElem = d3.select(`.video-icon[data-fkey="${f.fkey}"]`);
              if (!videoElem.empty()) {
                const flowData = flowDataMap.get(f.fkey);
                const videoType = flowData ? flowData.video_type : 0;
                const audioType = flowData ? flowData.audio_type : 0;
                if (videoType > 0) {
                  videoElem
                    .text(getVideoIcon(videoType))
                    .classed("video-stream-icon", true)
                    .classed("audio-stream-icon", false)
                    .attr("title", getVideoTooltip(flowData))
                    .style("color", "#e74c3c")
                    .style("cursor", "pointer");
                } else if (audioType > 0) {
                  const audioCodec = AUDIO_CODECS[flowData.audio_codec] || 'Audio';
                  videoElem
                    .text('\u266B')  /* Musical note */
                    .classed("video-stream-icon", false)
                    .classed("audio-stream-icon", true)
                    .attr("title", `RTP Audio: ${audioCodec}`)
                    .style("color", "#3498db")
                    .style("cursor", "default");
                } else {
                  videoElem
                    .text("")
                    .classed("video-stream-icon", false)
                    .classed("audio-stream-icon", false)
                    .attr("title", null)
                    .style("color", null)
                    .style("cursor", null);
                }
              }

              // Update health icon using unified function
              const healthElem = d3.select(`.health-icon[data-fkey="${f.fkey}"]`);
              if (!healthElem.empty()) {
                const flowData = flowDataMap.get(f.fkey);
                if (flowData) {
                  const isExpanded = expandedHealthRows.has(f.fkey);
                  const isTcp = f.fkey.indexOf('/TCP/') !== -1;
                  const iconInfo = getHealthIconForFlow(flowData, isTcp, isExpanded);

                  /* Determine title based on expanded state and data availability */
                  let title = '';
                  if (iconInfo.clickable) {
                    if (isExpanded) {
                      title = isTcp ? CLICK_TITLES.collapseHealth : CLICK_TITLES.collapseTiming;
                    } else {
                      title = isTcp ? CLICK_TITLES.expandHealth : CLICK_TITLES.expandTiming;
                    }
                  }

                  healthElem
                    .text(iconInfo.icon)
                    .style("color", iconInfo.color)
                    .style("cursor", iconInfo.cursor)
                    .attr("title", title);

                  // Add click handler if not already present and icon is clickable
                  if (iconInfo.clickable && !healthElem.attr("data-has-handler")) {
                    healthElem.attr("data-has-handler", "true");
                    healthElem.on("click", function(event) {
                      event.stopPropagation();
                      const fkey = d3.select(this).attr("data-fkey");
                      const detailsRow = d3.select(`.health-details-row-container[data-fkey="${fkey}"]`);
                      const fkeyIsTcp = fkey.indexOf('/TCP/') !== -1;

                      if (expandedHealthRows.has(fkey)) {
                        expandedHealthRows.delete(fkey);
                        detailsRow.remove();
                        const fd = currentFlowDataMap.get(fkey);
                        const collapsedIconInfo = getHealthIconForFlow(fd, fkeyIsTcp, false);
                        d3.select(this).text(collapsedIconInfo.icon).style("color", collapsedIconInfo.color).attr("title", fkeyIsTcp ? CLICK_TITLES.expandHealth : CLICK_TITLES.expandTiming);
                      } else {
                        expandedHealthRows.add(fkey);
                        const fd = currentFlowDataMap.get(fkey);
                        if (fd) {
                          const parentRow = d3.select(this.parentNode);
                          const samples = `${fd.health_rtt_samples || 0},${fd.ipg_samples || 0},${fd.frame_size_samples || 0},${fd.pps_samples || 0}`;
                          legendContainer.insert("div", function() {
                            return parentRow.node().nextSibling;
                          })
                            .attr("class", "health-details-row-container")
                            .attr("data-fkey", fkey)
                            .attr("data-samples", samples)
                            .html(createHealthDetailsRow(fkey, fd));
                        }
                        d3.select(this).text('\u25BC').style("color", "#333").attr("title", fkeyIsTcp ? CLICK_TITLES.collapseHealth : CLICK_TITLES.collapseTiming);
                      }
                    });
                  }
                }
              }
            }
          }

          // Update expanded video detail panels with fresh data
          updateLegendValues(flowDataMap);
        }
      }
    };


    /* Set the callback for resizing the chart */
    d3.select(window).on('resize.chartToptalk', function() {
      clearTimeout(resizeTimer);
      resizeTimer = setTimeout(function() {
        m.reset();
        my.charts.setDirty();
      }, 100);
    });

    /* Export getFlowColor for use by other charts (e.g., RTT chart) */
    m.getFlowColor = getFlowColor;

    return m;

  }({}));

})(JT);
/* End of jittertrap-chart-toptalk.js */
