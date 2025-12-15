/* jittertrap-chart-rtt.js */

/* global d3 */
/* global JT:true */

((my) => {
  'use strict';

  my.charts.rtt = {};

  // TCP connection states (matches server-side enum)
  const TCP_STATE = {
    UNKNOWN: 0,
    SYN_SEEN: 1,
    ACTIVE: 2,
    FIN_WAIT: 3,
    CLOSED: 4
  };

  my.charts.rtt.rttChart = (function (m) {
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
    let yScale = yScaleLog;
    let useLogScale = true;

    // Use the same color function as Top Talkers for consistent flow colors
    const getFlowColor = (fkey) => my.charts.toptalk.toptalkChart.getFlowColor(fkey);

    const formatRtt = function(d) {
      if (d < 1000) return d.toFixed(0) + " us";
      if (d < 1000000) return (d / 1000).toFixed(1) + " ms";
      return (d / 1000000).toFixed(2) + " s";
    };

    let xAxis = d3.axisBottom();
    let yAxis = d3.axisLeft().tickFormat(formatRtt);
    let xGrid = d3.axisBottom();
    let yGrid = d3.axisLeft();

    let svg = {};
    let line = d3.line();
    let resizeTimer;
    let linesGroup = null;

    // Reusable arrays to avoid allocations in redraw
    let rttFlowsCache = [];
    let markerDataCache = [];
    let cachedFlowKeys = [];
    let tempLineValues = [];  // Reusable array for line drawing

    // Pool for marker data objects to avoid allocation
    const markerObjectPool = [];
    let markerPoolIndex = 0;

    const getPooledMarker = function(fkey, ts, rtt_us, type) {
      if (markerPoolIndex < markerObjectPool.length) {
        const obj = markerObjectPool[markerPoolIndex++];
        obj.fkey = fkey;
        obj.ts = ts;
        obj.rtt_us = rtt_us;
        obj.type = type;
        return obj;
      }
      const obj = { fkey, ts, rtt_us, type };
      markerObjectPool.push(obj);
      markerPoolIndex++;
      return obj;
    };

    // Cached values for tick formatter to avoid closure allocation
    let cachedMaxTs = 0;
    const xTickFormatter = function(s) {
      const rel = s - cachedMaxTs;
      return rel === 0 ? "0" : rel.toFixed(0);
    };

    // Reusable tick values array
    const tickValuesCache = [];

    // Pre-defined line accessor to avoid closure allocation
    const lineYAccessor = function(d) { return yScale(d.rtt_us); };

    m.reset = function() {
      d3.select("#chartRtt").selectAll("svg").remove();

      my.charts.resizeChart("#chartRtt", size)();

      svg = d3.select("#chartRtt")
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
      yAxis = d3.axisLeft().scale(yScale).ticks(5).tickFormat(formatRtt);

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

      // Add clipPath inside graph group so it shares the same coordinate system
      // This ensures clipping at (0,0) to (width,height) in chart space
      graph.append("defs")
         .append("clipPath")
         .attr("id", "rtt-clip")
         .append("rect")
         .attr("width", width)
         .attr("height", height);

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
         .text("RTT");

      linesGroup = graph.append("g")
         .attr("class", "lines")
         .attr("clip-path", "url(#rtt-clip)");

      line = d3.line()
               .defined(d => d.rtt_us > 0)
               .x(d => xScale(d.ts))
               .y(d => yScale(d.rtt_us));
    };

    m.redraw = function() {
      if (!linesGroup) return;

      const width = size.width - margin.left - margin.right;
      const height = size.height - margin.top - margin.bottom;

      const flowData = JT.charts.getTopFlowsRef();

      // Compute time bounds and RTT min/max without creating intermediate arrays
      let globalMinTs = Infinity;
      let globalMaxTs = -Infinity;
      let minRtt = Infinity;
      let maxRtt = -Infinity;
      let hasRttData = false;

      // Clear and rebuild rttFlows cache in place
      rttFlowsCache.length = 0;

      for (let i = 0; i < flowData.length; i++) {
        const f = flowData[i];
        const values = f.values;

        // Update global time bounds from all flows
        for (let j = 0; j < values.length; j++) {
          const ts = values[j].ts;
          if (ts < globalMinTs) globalMinTs = ts;
          if (ts > globalMaxTs) globalMaxTs = ts;
        }

        // Check if this is a TCP flow with RTT data
        if (f.fkey === 'other' || f.fkey.indexOf('/TCP/') === -1) continue;

        let hasValidRtt = false;
        for (let j = 0; j < values.length; j++) {
          const rtt = values[j].rtt_us;
          if (rtt > 0) {
            hasValidRtt = true;
            hasRttData = true;
            if (rtt < minRtt) minRtt = rtt;
            if (rtt > maxRtt) maxRtt = rtt;
          }
        }

        if (hasValidRtt) {
          rttFlowsCache.push(f);
        }
      }

      // Handle edge case of no data
      if (globalMinTs === Infinity) {
        globalMinTs = 0;
        globalMaxTs = 1;
      }

      if (!hasRttData) {
        // No RTT data - clear lines/markers but keep X-axis scrolling
        linesGroup.selectAll(".rtt-line").remove();
        linesGroup.selectAll(".rtt-marker").remove();

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

      // Get domain parameters from shared utility for smooth scrolling
      const domain = JT.core.getChartDomain();

      // Use lagged domain for smooth scrolling, fall back to data extent if not yet valid
      const xMax = domain.isValid ? domain.xMax : globalMaxTs;
      const xMin = domain.isValid ? domain.xMin : globalMinTs;
      xScale.domain([xMin, xMax]);

      if (useLogScale) {
        yScale = yScaleLog;
        const logMin = Math.max(1, minRtt * 0.9);
        const logMax = maxRtt * 1.1;
        yScale.domain([logMin, logMax]);

        // Generate sensible tick values for log scale (powers of 10 and key points)
        const logTickValues = [];
        let tick = 1; // 1 us
        while (tick <= logMax) {
          if (tick >= logMin) logTickValues.push(tick);
          tick *= 10;
        }
        yAxis.tickValues(logTickValues);
        yGrid.tickValues(logTickValues);
      } else {
        yScale = yScaleLinear;
        yScale.domain([0, maxRtt * 1.1]);
        yAxis.tickValues(null); // Use auto ticks for linear
        yGrid.tickValues(null);
      }

      // Update line generator's y accessor (uses pre-defined function to avoid closure)
      line.y(lineYAccessor);
      yAxis.scale(yScale).tickFormat(formatRtt);

      // Update X axis with relative time format (matching Top Flows chart)
      // Generate tick positions at ROUND intervals for clean axis labels
      if (domain.isValid) {
        const tickInterval = Math.max(1, Math.ceil(domain.visibleWidthSec / 10));

        // Generate ticks at round relative-time positions (0, -20, -40, etc.)
        tickValuesCache.length = 0;
        for (let relativeTime = 0; relativeTime >= -domain.windowWidthSec && tickValuesCache.length < 15; relativeTime -= tickInterval) {
          const absoluteTime = domain.currentTime + relativeTime;
          if (absoluteTime >= domain.xMin && absoluteTime <= domain.xMax) {
            tickValuesCache.unshift(absoluteTime);
          }
        }
        // Use currentTime as reference for stable tick labels
        cachedMaxTs = domain.currentTime;
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

      // Update lines - stop drawing after CLOSED state
      const lines = linesGroup.selectAll(".rtt-line")
          .data(rttFlowsCache, d => d.fkey);

      // Get selected flow for highlighting
      const selectedKey = my.charts.getSelectedFlow();

      lines.enter()
          .append("path")
          .attr("class", "rtt-line")
          .attr("fill", "none")
          .attr("stroke-width", 2)
        .merge(lines)
          .attr("stroke", d => getFlowColor(d.fkey))
          .attr("stroke-opacity", d => {
            if (selectedKey === null) return 1.0;
            return d.fkey === selectedKey ? 1.0 : 0.3;
          })
          .attr("d", d => {
            // Find first CLOSED point and stop drawing there
            const values = d.values;
            let stopIdx = values.length;
            for (let i = 0; i < values.length; i++) {
              if (values[i].tcp_state === TCP_STATE.CLOSED) {
                // Include this point but stop after
                stopIdx = i + 1;
                break;
              }
            }
            // Use reusable array instead of slice() to avoid allocation
            tempLineValues.length = stopIdx;
            for (let i = 0; i < stopIdx; i++) {
              tempLineValues[i] = values[i];
            }
            return line(tempLineValues);
          });

      lines.exit().remove();

      // Build marker data in place - reuse cache array and pooled objects
      // Marker types:
      //   'new'    - SYN observed, connection starting (▶)
      //   'closed' - FIN/RST seen, connection terminated (■)
      // Gaps in the line naturally show idle periods - no markers needed
      markerDataCache.length = 0;
      markerPoolIndex = 0;  // Reset pool for this frame

      for (let fi = 0; fi < rttFlowsCache.length; fi++) {
        const f = rttFlowsCache[fi];
        const values = f.values;
        const fkey = f.fkey;

        // Find first and last valid RTT points without creating array
        let firstValidIdx = -1;
        let lastValidIdx = -1;

        for (let i = 0; i < values.length; i++) {
          if (values[i].rtt_us > 0) {
            if (firstValidIdx === -1) firstValidIdx = i;
            lastValidIdx = i;
          }
        }

        if (firstValidIdx === -1) continue;

        // Get the flow's TCP state
        const tcpState = f.tcp_state;
        const isHalfClosed = (tcpState === TCP_STATE.FIN_WAIT);
        const isClosed = (tcpState === TCP_STATE.CLOSED);

        const firstPt = values[firstValidIdx];
        const lastPt = values[lastValidIdx];

        // New connection marker (SYN observed) - use flow-level saw_syn flag
        if (f.saw_syn) {
          markerDataCache.push(getPooledMarker(fkey, firstPt.ts, firstPt.rtt_us, 'new'));
        }

        // Find when state transitions occurred (for placing markers at transition point)
        // This allows the line to continue if data arrives after CLOSED (pathological case)
        let closedTransitionIdx = -1;
        let halfClosedTransitionIdx = -1;

        for (let i = 0; i < values.length; i++) {
          if (values[i].rtt_us <= 0) continue;

          const state = values[i].tcp_state;
          if (state === TCP_STATE.CLOSED && closedTransitionIdx === -1) {
            closedTransitionIdx = i;
          }
          if (state === TCP_STATE.FIN_WAIT && halfClosedTransitionIdx === -1) {
            halfClosedTransitionIdx = i;
          }
        }

        // Fully closed marker - place at first point where state became CLOSED
        if (closedTransitionIdx !== -1) {
          const pt = values[closedTransitionIdx];
          markerDataCache.push(getPooledMarker(fkey, pt.ts, pt.rtt_us, 'closed'));
        }

        // Half-closed marker - only show if flow has stopped AND didn't transition to CLOSED
        if (halfClosedTransitionIdx !== -1 && closedTransitionIdx === -1) {
          const flowIsOngoing = (globalMaxTs - lastPt.ts) < 1.0;
          if (!flowIsOngoing) {
            const pt = values[halfClosedTransitionIdx];
            markerDataCache.push(getPooledMarker(fkey, pt.ts, pt.rtt_us, 'half_closed'));
          }
        }
      }

      // Update markers - use stable key function
      const markers = linesGroup.selectAll(".rtt-marker")
          .data(markerDataCache, markerKey);

      const markersEnter = markers.enter()
          .append("g")
          .attr("class", "rtt-marker");

      // Create marker shapes on enter
      markersEnter.each(function(d) {
        const g = d3.select(this);
        if (d.type === 'closed') {
          // ■ for closed/terminated connection (FIN in both directions or RST)
          g.append("rect")
            .attr("class", "marker-shape")
            .attr("x", -4)
            .attr("y", -4)
            .attr("width", 8)
            .attr("height", 8);
        } else if (d.type === 'half_closed') {
          // ⏸ pause for half-closed (one FIN seen)
          g.append("text")
            .attr("class", "marker-shape")
            .attr("text-anchor", "middle")
            .attr("dominant-baseline", "middle")
            .attr("font-size", "12px")
            .text('\u23F8\uFE0E');
        } else if (d.type === 'new') {
          // ▶ for new connection (SYN observed)
          g.append("path")
            .attr("class", "marker-shape")
            .attr("d", "M-4,-4 L4,0 L-4,4 Z");
        }
      });

      // Update all markers (position, color, and opacity based on selection)
      const merged = markers.merge(markersEnter)
          .attr("transform", d => markerTransform(d))
          .attr("opacity", d => {
            if (selectedKey === null) return 1.0;
            return d.fkey === selectedKey ? 1.0 : 0.3;
          });
      merged.selectAll(".marker-shape")
          .attr("fill", d => getFlowColor(d.fkey));

      markers.exit().remove();
    };

    // Pre-allocated key function to avoid template literal allocation
    const markerKey = function(d) {
      return d.fkey + '-' + d.ts.toFixed(2) + '-' + d.type;
    };

    // Pre-allocated transform function
    const markerTransform = function(d) {
      return 'translate(' + xScale(d.ts) + ',' + yScale(d.rtt_us) + ')';
    };

    m.setLogScale = function(isLog) {
      useLogScale = isLog;
      m.reset();  // Force full redraw to fix Y-axis labels
      my.charts.setDirty();
    };

    // Handle window resize
    d3.select(window).on('resize.chartRtt', function() {
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
    $('#rtt-y-axis-scale input').on('change', function() {
      const isLog = $(this).val() === '1';
      my.charts.rtt.rttChart.setLogScale(isLog);
    });
  });

})(JT);
/* End of jittertrap-chart-rtt.js */
