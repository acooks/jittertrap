/* jittertrap-chart-rtt.js */

/* global d3 */
/* global JT:true */

((my) => {
  'use strict';

  my.charts.rtt = {};

  // TCP connection states (matches server-side enum)
  const TCP_STATE = {
    UNKNOWN: 0,
    ACTIVE: 1,
    FIN_WAIT: 2,
    CLOSED: 3
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
    let yScale = yScaleLinear;
    let useLogScale = false;

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

    const gapThreshold = 2;  // seconds - gap larger than this = idle

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
         .attr("class", "lines");

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

      // Filter to TCP flows with RTT data
      const rttFlows = flowData.filter(f =>
          f.fkey !== 'other' &&
          f.fkey.includes('/TCP/') &&
          f.values.some(v => v.rtt_us > 0)
      );

      // Collect all valid RTT values to compute domains
      const allValues = rttFlows.flatMap(f => f.values.filter(v => v.rtt_us > 0));

      // Get time bounds from all flows (not just RTT flows) so chart keeps scrolling
      const allFlowValues = flowData.flatMap(f => f.values);
      const [globalMinTs, globalMaxTs] = allFlowValues.length > 0
          ? d3.extent(allFlowValues, d => d.ts)
          : [0, 1];

      if (allValues.length === 0) {
        // No RTT data - clear lines/markers but keep X-axis scrolling
        linesGroup.selectAll(".rtt-line").remove();
        linesGroup.selectAll(".rtt-marker").remove();

        // Update X-axis with global time bounds
        xScale.domain([globalMinTs, globalMaxTs]);
        const domainSpan = globalMaxTs - globalMinTs;
        if (domainSpan > 0) {
          const tickCount = Math.min(10, Math.floor(domainSpan));
          const tickInterval = Math.max(1, Math.ceil(domainSpan / tickCount));
          const tickValues = [];
          for (let t = globalMaxTs; t >= globalMinTs && tickValues.length < 12; t -= tickInterval) {
            tickValues.unshift(t);
          }
          xAxis.tickValues(tickValues);
          xAxis.tickFormat(s => {
            const rel = s - globalMaxTs;
            return rel === 0 ? "0" : rel.toFixed(0);
          });
        }
        svg.select(".x.axis").call(xAxis);
        return;
      }

      // Use same X domain as Top Flows chart (from first flow's time extent)
      xScale.domain([globalMinTs, globalMaxTs]);

      const maxRtt = d3.max(allValues, d => d.rtt_us);
      const minRtt = d3.min(allValues, d => d.rtt_us);

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

      // Update line generator's y accessor
      line.y(d => yScale(d.rtt_us));
      yAxis.scale(yScale).tickFormat(formatRtt);

      // Update X axis with relative time format (matching Top Flows chart)
      const domainSpan = globalMaxTs - globalMinTs;
      if (domainSpan > 0) {
        const tickInterval = Math.max(1, Math.ceil(domainSpan / 10));
        const tickValues = [];
        for (let relativeTime = 0; relativeTime >= -domainSpan && tickValues.length < 12; relativeTime -= tickInterval) {
          tickValues.unshift(globalMaxTs + relativeTime);
        }
        xAxis.tickValues(tickValues);
        xGrid.tickValues(tickValues);
        xAxis.tickFormat(s => {
          const rel = s - globalMaxTs;
          return rel === 0 ? "0" : rel.toFixed(0);
        });
      }

      // Update grids
      xGrid.scale(xScale).tickSize(-height);
      yGrid.scale(yScale).tickSize(-width);

      svg.select(".x.axis").call(xAxis);
      svg.select(".y.axis").call(yAxis);
      svg.select(".xGrid").call(xGrid);
      svg.select(".yGrid").call(yGrid);

      // Update lines
      const lines = linesGroup.selectAll(".rtt-line")
          .data(rttFlows, d => d.fkey);

      lines.enter()
          .append("path")
          .attr("class", "rtt-line")
          .attr("fill", "none")
          .attr("stroke-width", 2)
        .merge(lines)
          .attr("stroke", d => getFlowColor(d.fkey))
          .attr("d", d => line(d.values));

      lines.exit().remove();

      // Build marker data - first point, last point, and gap boundaries
      // Marker types:
      //   'start'  - first RTT sample (▶)
      //   'resume' - resumption after gap (▶)
      //   'idle'   - no data for >2s, connection still open (×)
      //   'closed' - FIN/RST seen, connection terminated (■)
      const markerData = [];
      rttFlows.forEach(f => {
        const validPts = f.values.filter(v => v.rtt_us > 0);
        if (validPts.length === 0) return;

        // Get the flow's TCP state (from most recent data point with state)
        const tcpState = f.tcp_state;
        const isClosed = (tcpState === TCP_STATE.CLOSED || tcpState === TCP_STATE.FIN_WAIT);

        // First point - mark as 'start'
        const firstPt = validPts[0];
        markerData.push({ fkey: f.fkey, ts: firstPt.ts, rtt_us: firstPt.rtt_us, type: 'start' });

        let prevPt = firstPt;
        for (let i = 1; i < validPts.length; i++) {
          const pt = validPts[i];
          if ((pt.ts - prevPt.ts) > gapThreshold) {
            // Gap detected - mark end of previous segment as idle
            markerData.push({ fkey: f.fkey, ts: prevPt.ts, rtt_us: prevPt.rtt_us, type: 'idle' });
            // Mark start of new segment as resume
            markerData.push({ fkey: f.fkey, ts: pt.ts, rtt_us: pt.rtt_us, type: 'resume' });
          }
          prevPt = pt;
        }

        // Last point - determine marker type based on tcp_state and recency
        const lastPt = validPts[validPts.length - 1];
        const timeSinceLastData = globalMaxTs - lastPt.ts;

        let endMarkerType;
        if (isClosed) {
          endMarkerType = 'closed';
        } else if (timeSinceLastData > gapThreshold) {
          endMarkerType = 'idle';
        } else {
          // Active flow - no end marker needed (line extends to current time)
          endMarkerType = null;
        }

        if (endMarkerType) {
          // Don't duplicate if first == last (single point) - update existing marker
          if (validPts.length === 1) {
            markerData[markerData.length - 1].type = endMarkerType;
          } else {
            markerData.push({ fkey: f.fkey, ts: lastPt.ts, rtt_us: lastPt.rtt_us, type: endMarkerType });
          }
        }
      });

      // Update markers
      const markers = linesGroup.selectAll(".rtt-marker")
          .data(markerData, d => `${d.fkey}-${d.ts.toFixed(2)}-${d.type}`);

      const markersEnter = markers.enter()
          .append("g")
          .attr("class", "rtt-marker");

      // Create marker shapes on enter
      markersEnter.each(function(d) {
        const g = d3.select(this);
        if (d.type === 'closed') {
          // ■ for closed/terminated connection
          g.append("rect")
            .attr("class", "marker-shape")
            .attr("x", -4)
            .attr("y", -4)
            .attr("width", 8)
            .attr("height", 8);
        } else if (d.type === 'idle') {
          // ❚❚ pause symbol for idle (no data but connection still open)
          // Two vertical bars with gap between them
          g.append("rect")
            .attr("class", "marker-shape")
            .attr("x", -5)
            .attr("y", -4)
            .attr("width", 3)
            .attr("height", 8);
          g.append("rect")
            .attr("class", "marker-shape marker-shape-2")
            .attr("x", 2)
            .attr("y", -4)
            .attr("width", 3)
            .attr("height", 8);
        } else if (d.type === 'start' || d.type === 'resume') {
          // ▶ for start and resume
          g.append("path")
            .attr("class", "marker-shape")
            .attr("d", "M-4,-4 L4,0 L-4,4 Z");
        }
      });

      // Update all markers (position and color)
      const merged = markers.merge(markersEnter)
          .attr("transform", d => `translate(${xScale(d.ts)},${yScale(d.rtt_us)})`);
      merged.select(".marker-shape")
          .attr("fill", d => getFlowColor(d.fkey));
      // Also color the second rect for pause symbol
      merged.select(".marker-shape-2")
          .attr("fill", d => getFlowColor(d.fkey));

      markers.exit().remove();
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
