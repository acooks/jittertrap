/* jittertrap-chart-window.js */

/* global d3 */
/* global JT:true */

((my) => {
  'use strict';

  my.charts.window = {};

  // Congestion event bitmask constants (match backend)
  const CONG_EVENT = {
    ZERO_WINDOW: 0x01,
    DUP_ACK: 0x02,
    RETRANSMIT: 0x04,
    ECE: 0x08,
    CWR: 0x10
  };

  my.charts.window.windowChart = (function (m) {
    const isMobile = window.innerWidth <= 768;
    const margin = {
      top: isMobile ? 5 : 10,
      right: isMobile ? 10 : 20,
      bottom: isMobile ? 30 : 40,
      left: isMobile ? 50 : 75
    };

    const size = { width: 960, height: 250 };
    let xScale = d3.scaleLinear();
    let yScaleLinear = d3.scaleLinear();
    let yScaleLog = d3.scaleLog().clamp(true);
    let yScale = yScaleLog;  // Default to log for window sizes
    let useLogScale = true;

    // Use the same color function as Top Talkers for consistent flow colors
    const getFlowColor = (fkey) => my.charts.toptalk.toptalkChart.getFlowColor(fkey);

    // Reusable arrays to avoid allocations in redraw
    let windowFlowsCache = [];
    let markerDataCache = [];

    // Cached values for tick formatter to avoid closure allocation
    let cachedMaxTs = 0;
    const xTickFormatter = function(s) {
      const rel = s - cachedMaxTs;
      return rel === 0 ? "0" : rel.toFixed(0);
    };

    // Reusable tick values array
    const tickValuesCache = [];

    // Pre-defined line accessor to avoid closure allocation
    const lineYAccessor = function(d) { return yScale(d.rwnd_bytes); };

    const formatWindow = function(d) {
      if (d < 1024) return d.toFixed(0) + " B";
      if (d < 1024 * 1024) return (d / 1024).toFixed(1) + " KB";
      if (d < 1024 * 1024 * 1024) return (d / (1024 * 1024)).toFixed(1) + " MB";
      return (d / (1024 * 1024 * 1024)).toFixed(2) + " GB";
    };

    let xAxis = d3.axisBottom();
    let yAxis = d3.axisLeft().tickFormat(formatWindow);
    let xGrid = d3.axisBottom();
    let yGrid = d3.axisLeft();

    let svg = {};
    let line = d3.line();
    let resizeTimer;
    let linesGroup = null;
    let markersGroup = null;

    m.reset = function() {
      d3.select("#chartWindow").selectAll("svg").remove();

      my.charts.resizeChart("#chartWindow", size)();

      svg = d3.select("#chartWindow")
            .append("svg")
            .attr("width", size.width)
            .attr("height", size.height);

      const width = size.width - margin.left - margin.right;
      const height = size.height - margin.top - margin.bottom;

      xScale = d3.scaleLinear().range([0, width]);
      yScaleLinear = d3.scaleLinear().range([height, 0]);
      yScaleLog = d3.scaleLog().range([height, 0]).clamp(true);
      yScale = useLogScale ? yScaleLog : yScaleLinear;

      xAxis = d3.axisBottom().scale(xScale).ticks(10);
      yAxis = d3.axisLeft().scale(yScale).ticks(5).tickFormat(formatWindow);

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

      const graph = svg.append("g")
         .attr("transform", "translate(" + margin.left + "," + margin.top + ")");

      graph.append("g")
         .attr("class", "xGrid")
         .attr("transform", "translate(0," + height + ")")
         .call(xGrid);

      graph.append("g")
         .attr("class", "yGrid")
         .call(yGrid);

      graph.append("g")
         .attr("class", "x axis")
         .attr("transform", "translate(0," + height + ")")
         .call(xAxis);

      graph.append("text")
           .attr("class", "axis-label")
           .attr("text-anchor", "middle")
           .attr("x", width/2)
           .attr("y", height + 35)
           .text("Time (s)");

      graph.append("g")
         .attr("class", "y axis")
         .call(yAxis);

      graph.append("text")
         .attr("class", "axis-label")
         .attr("transform", "rotate(-90)")
         .attr("y", 0 - margin.left)
         .attr("x", 0 - (height / 2))
         .attr("dy", "1em")
         .style("text-anchor", "middle")
         .text("Advertised Window");

      linesGroup = graph.append("g")
         .attr("class", "window-lines");

      markersGroup = graph.append("g")
         .attr("class", "event-markers");

      line = d3.line()
               .defined(d => d.rwnd_bytes > 0)
               .x(d => xScale(d.ts))
               .y(d => yScale(d.rwnd_bytes));
    };

    m.redraw = function() {
      if (!linesGroup) return;

      const width = size.width - margin.left - margin.right;
      const height = size.height - margin.top - margin.bottom;

      const flowData = JT.charts.getTopFlowsRef();

      // Compute time bounds and window min/max without creating intermediate arrays
      let globalMinTs = Infinity;
      let globalMaxTs = -Infinity;
      let minWindow = Infinity;
      let maxWindow = -Infinity;
      let hasWindowData = false;

      // Clear and rebuild windowFlows cache in place
      windowFlowsCache.length = 0;

      for (let i = 0; i < flowData.length; i++) {
        const f = flowData[i];
        const values = f.values;

        // Update global time bounds from all flows
        for (let j = 0; j < values.length; j++) {
          const ts = values[j].ts;
          if (ts < globalMinTs) globalMinTs = ts;
          if (ts > globalMaxTs) globalMaxTs = ts;
        }

        // Check if this is a TCP flow with window data
        if (f.fkey === 'other' || f.fkey.indexOf('/TCP/') === -1) continue;

        let hasValidWindow = false;
        for (let j = 0; j < values.length; j++) {
          const rwnd = values[j].rwnd_bytes;
          if (rwnd > 0) {
            hasValidWindow = true;
            hasWindowData = true;
            if (rwnd < minWindow) minWindow = rwnd;
            if (rwnd > maxWindow) maxWindow = rwnd;
          }
        }

        if (hasValidWindow) {
          windowFlowsCache.push(f);
        }
      }

      // Handle edge case of no data
      if (globalMinTs === Infinity) {
        globalMinTs = 0;
        globalMaxTs = 1;
      }

      if (!hasWindowData) {
        // No window data - clear lines/markers but keep X-axis scrolling
        linesGroup.selectAll(".window-line").remove();
        markersGroup.selectAll(".event-marker").remove();

        // Update X-axis with global time bounds
        xScale.domain([globalMinTs, globalMaxTs]);
        const domainSpan = globalMaxTs - globalMinTs;
        if (domainSpan > 0) {
          const tickCount = Math.min(10, Math.floor(domainSpan));
          const tickInterval = Math.max(1, Math.ceil(domainSpan / tickCount));
          tickValuesCache.length = 0;
          for (let t = globalMaxTs; t >= globalMinTs && tickValuesCache.length < 12; t -= tickInterval) {
            tickValuesCache.unshift(t);
          }
          cachedMaxTs = globalMaxTs;
          xAxis.tickValues(tickValuesCache);
          xAxis.tickFormat(xTickFormatter);
        }
        svg.select(".x.axis").call(xAxis);
        return;
      }

      // Set X domain
      xScale.domain([globalMinTs, globalMaxTs]);

      if (useLogScale) {
        yScale = yScaleLog;
        const logMin = Math.max(1, minWindow * 0.5);
        const logMax = maxWindow * 2;
        yScale.domain([logMin, logMax]);

        // Generate sensible tick values for log scale
        const logTickValues = [];
        let tick = 1024; // 1 KB
        while (tick <= logMax) {
          if (tick >= logMin) logTickValues.push(tick);
          tick *= 4;  // 1KB, 4KB, 16KB, 64KB, 256KB, 1MB, etc.
        }
        if (logTickValues.length === 0) {
          logTickValues.push(logMin, logMax);
        }
        yAxis.tickValues(logTickValues);
        yGrid.tickValues(logTickValues);
      } else {
        yScale = yScaleLinear;
        yScale.domain([0, maxWindow * 1.1]);
        yAxis.tickValues(null);
        yGrid.tickValues(null);
      }

      // Update line generator's y accessor (uses pre-defined function to avoid closure)
      line.y(lineYAccessor);
      yAxis.scale(yScale).tickFormat(formatWindow);

      // Update X axis with relative time format
      const domainSpan = globalMaxTs - globalMinTs;
      if (domainSpan > 0) {
        const tickInterval = Math.max(1, Math.ceil(domainSpan / 10));
        tickValuesCache.length = 0;
        for (let relativeTime = 0; relativeTime >= -domainSpan && tickValuesCache.length < 12; relativeTime -= tickInterval) {
          tickValuesCache.unshift(globalMaxTs + relativeTime);
        }
        cachedMaxTs = globalMaxTs;
        xAxis.tickValues(tickValuesCache);
        xGrid.tickValues(tickValuesCache);
        xAxis.tickFormat(xTickFormatter);
      }

      // Update grids
      xGrid.scale(xScale).tickSize(-height);
      yGrid.scale(yScale).tickSize(-width);

      svg.select(".x.axis").call(xAxis);
      svg.select(".y.axis").call(yAxis);
      svg.select(".xGrid").call(xGrid);
      svg.select(".yGrid").call(yGrid);

      // Update window lines
      const lines = linesGroup.selectAll(".window-line")
          .data(windowFlowsCache, d => d.fkey);

      lines.enter()
          .append("path")
          .attr("class", "window-line")
          .attr("fill", "none")
          .attr("stroke-width", 2)
        .merge(lines)
          .attr("stroke", d => getFlowColor(d.fkey))
          .attr("d", d => line(d.values));

      lines.exit().remove();

      // Build event marker data in place - reuse cache array
      markerDataCache.length = 0;

      for (let fi = 0; fi < windowFlowsCache.length; fi++) {
        const f = windowFlowsCache[fi];
        const values = f.values;
        const fkey = f.fkey;

        for (let i = 0; i < values.length; i++) {
          const v = values[i];
          if (v.rwnd_bytes <= 0) continue;

          const events = v.recent_events || 0;
          if (events === 0) continue;

          const ts = v.ts;
          const y = v.rwnd_bytes;

          // Only add markers for actual events
          if (events & CONG_EVENT.ZERO_WINDOW) {
            markerDataCache.push({ fkey: fkey, ts: ts, y: y, type: 'zero_window' });
          }
          if (events & CONG_EVENT.DUP_ACK) {
            markerDataCache.push({ fkey: fkey, ts: ts, y: y, type: 'dup_ack' });
          }
          if (events & CONG_EVENT.RETRANSMIT) {
            markerDataCache.push({ fkey: fkey, ts: ts, y: y, type: 'retransmit' });
          }
          if (events & CONG_EVENT.ECE) {
            markerDataCache.push({ fkey: fkey, ts: ts, y: y, type: 'ece' });
          }
          if (events & CONG_EVENT.CWR) {
            markerDataCache.push({ fkey: fkey, ts: ts, y: y, type: 'cwr' });
          }
        }
      }

      // Update event markers using text symbols
      const markers = markersGroup.selectAll(".event-marker")
          .data(markerDataCache, markerKey);

      const markersEnter = markers.enter()
          .append("text")
          .attr("class", "event-marker")
          .attr("text-anchor", "middle")
          .attr("dominant-baseline", "middle")
          .attr("font-size", "14px")
          .attr("font-weight", "bold");

      markers.merge(markersEnter)
          .attr("x", d => xScale(d.ts))
          .attr("y", d => yScale(d.y) - 12)  // Offset above line
          .attr("fill", markerFill)
          .text(markerText);

      markers.exit().remove();
    };

    // Pre-allocated key function to avoid template literal allocation
    const markerKey = function(d) {
      return d.fkey + '-' + d.ts.toFixed(3) + '-' + d.type;
    };

    // Pre-allocated fill function
    const markerFill = function(d) {
      switch(d.type) {
        case 'zero_window': return '#ff4444';   // Red
        case 'dup_ack': return '#ff8800';       // Orange
        case 'retransmit': return '#ff0000';    // Red
        case 'ece': return '#8800ff';           // Purple
        case 'cwr': return '#0088ff';           // Blue
        default: return '#888888';
      }
    };

    // Pre-allocated text function
    const markerText = function(d) {
      switch(d.type) {
        case 'zero_window': return '\u26A0';    // Warning sign
        case 'dup_ack': return '\u21BB';        // Clockwise arrow
        case 'retransmit': return '\u21A9';     // Leftward arrow with hook
        case 'ece': return '\u25BC';            // Down triangle
        case 'cwr': return '\u25B2';            // Up triangle
        default: return '?';
      }
    };

    m.setLogScale = function(isLog) {
      useLogScale = isLog;
      m.reset();
      my.charts.setDirty();
    };

    // Handle window resize
    d3.select(window).on('resize.chartWindow', function() {
      clearTimeout(resizeTimer);
      resizeTimer = setTimeout(function() {
        m.reset();
        my.charts.setDirty();
      }, 100);
    });

    return m;
  }({}));

  // Set up event handler for Y-axis scale toggle
  $(document).ready(function() {
    $('#window-y-axis-scale input').on('change', function() {
      const isLog = $(this).val() === '1';
      my.charts.window.windowChart.setLogScale(isLog);
    });
  });

})(JT);
/* End of jittertrap-chart-window.js */
