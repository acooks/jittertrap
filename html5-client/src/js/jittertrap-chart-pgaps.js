/* jittertrap-chart-pgaps.js */

/* global d3 */
/* global JT:true */

((my) => {
  'use strict';

  my.charts.pgaps = {};

  const chartData = { packetGapMean: [], packetGapMinMax: [] };

  my.charts.pgaps.getMeanRef = function () {
    return chartData.packetGapMean;
  };

  my.charts.pgaps.getMinMaxRef = function () {
    return chartData.packetGapMinMax;
  };

  my.charts.pgaps.packetGapChart = (function (m) {
    // Use smaller margins on mobile devices
    const isMobile = window.innerWidth <= 768;
    const margin = {
      top: isMobile ? 15 : 20,
      right: isMobile ? 10 : 20,
      bottom: isMobile ? 35 : 40,
      left: isMobile ? 50 : 75
    };

    const size = { width: 960, height: 300 };
    let xScale = d3.scaleLinear().range([0, size.width]);
    let yScale = d3.scaleLinear().range([size.height, 0]);
    let xAxis = d3.axisBottom();
    let yAxis = d3.axisLeft();
    let xGrid = d3.axisBottom();
    let yGrid = d3.axisLeft();
    let line = d3.line();
    let minMaxArea = d3.area();

    let svg = {};

    // Cached D3 selections to avoid repeated select() calls which create
    // new selection objects that form cycles with DOM nodes, increasing
    // Cycle Collector (CC) pressure in Firefox
    let cachedSelections = {
      line: null,
      minMaxArea: null,
      xAxis: null,
      yAxis: null,
      xGrid: null,
      yGrid: null
    };

    // Cached max timestamp for tick formatter (avoids closure allocation)
    let cachedMaxTimestamp = 0;
    const xTickFormatter = function(seconds) {
      const relativeSeconds = seconds - cachedMaxTimestamp;
      return relativeSeconds % 1 === 0 ? relativeSeconds.toString() : relativeSeconds.toFixed(1);
    };

    m.reset = function() {
      // Clear cached selections before removing DOM (breaks cycles)
      cachedSelections.line = null;
      cachedSelections.minMaxArea = null;
      cachedSelections.xAxis = null;
      cachedSelections.yAxis = null;
      cachedSelections.xGrid = null;
      cachedSelections.yGrid = null;

      d3.select("#packetGapContainer").selectAll("svg").remove();

      svg = d3.select("#packetGapContainer")
            .append("svg");

      const width = size.width - margin.left - margin.right;
      const height = size.height - margin.top - margin.bottom;

      xScale = d3.scaleLinear().range([0, width]);
      yScale = d3.scaleLinear().range([height, 0]);

      xAxis = d3.axisBottom()
              .scale(xScale)
              .ticks(10);

      yAxis = d3.axisLeft()
              .scale(yScale)
              .ticks(5);

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

      line = d3.line()
        .x(d => xScale(d.x))
        .y(d => yScale(d.y))
        .curve(d3.curveBasis);


      minMaxArea = d3.area()
        .x(d => xScale(d.x) || 1)
        .y0(d => yScale(d.y[0]) || 0)
        .y1(d => yScale(d.y[1]) || 0)
        .curve(d3.curveBasis);

      svg.attr("width", width + margin.left + margin.right)
         .attr("height", height + margin.top + margin.bottom);

      const graph = svg.append("g")
         .attr("transform", "translate(" + margin.left + "," + margin.top + ")");

      graph.append("text")
         .attr("class", "title")
         .attr("text-anchor", "middle")
         .attr("x", width/2)
         .attr("y", 0 - margin.top / 2)
         .attr("dominant-baseline", "middle")
         .text("Inter Packet Gap");

      // Cache selections as they're created to avoid repeated select() calls in redraw
      cachedSelections.xAxis = graph.append("g")
         .attr("class", "x axis")
         .attr("transform", "translate(0," + height + ")")
         .call(xAxis);

      graph.append("text")
           .attr("class", "axis-label")
           .attr("text-anchor", "middle")
           .attr("x", width/2)
           .attr("y", height + margin.bottom - 10)
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
         .text("Packet Gap (ms)");

      cachedSelections.xGrid = graph.append("g")
        .attr("class", "xGrid")
        .attr("transform", "translate(0," + height + ")")
        .call(xGrid);


      cachedSelections.yGrid = graph.append("g")
        .attr("class", "yGrid")
        .call(yGrid);

      // Add clip path to prevent drawing outside chart area
      graph.append("defs").append("clipPath")
        .attr("id", "clip-pgaps")
        .append("rect")
        .attr("width", width)
        .attr("height", height);

      cachedSelections.minMaxArea = graph.append('path')
        .datum(chartData.packetGapMinMax)
        .attr('class', 'minMaxArea')
        .attr('d', minMaxArea)
        .attr("clip-path", "url(#clip-pgaps)")
        .style('fill', 'pink')
        .style("opacity", 0.8);

      cachedSelections.line = graph.append("path")
         .datum(chartData.packetGapMean)
         .attr("class", "line")
         .attr("clip-path", "url(#clip-pgaps)")
         .attr("d", line);

      my.charts.resizeChart("#packetGapContainer", size)();
    };

    m.redraw = function() {

      if (chartData.packetGapMean.length === 0) {
        return;
      }

      const width = size.width - margin.left - margin.right;
      const height = size.height - margin.top - margin.bottom;

      // Update scale ranges and domains.
      xScale.range([0, width]);
      yScale.range([height, 0]);

      /* Use shared domain calculation for synchronized smooth scrolling */
      const domain = JT.core.getChartDomain();

      if (domain.isValid) {
        // Fixed window from the start - smooth scrolling enabled immediately
        xScale.domain([domain.xMin, domain.xMax]);
      } else {
        // Fallback to data extent if no valid domain yet
        xScale.domain(d3.extent(chartData.packetGapMean, d => d.x));
      }
      yScale.domain([0, d3.max(chartData.packetGapMinMax, d => d.y[1])]);

      // Update time formatter: 0 = now (max), negative = past (oscilloscope style)
      const domainExtent = xScale.domain();
      const maxTimestamp = domainExtent[1];
      const minTimestamp = domainExtent[0];
      const domainSpanSec = maxTimestamp - minTimestamp;  // Domain is now in seconds

      // Generate fixed tick positions in relative time to prevent scrolling
      const tickIntervalSec = Math.max(1, Math.ceil(domainSpanSec / 10)); // Round to whole seconds, minimum 1s
      const tickValues = [];
      let iterations = 0;
      for (let relativeTime = 0; relativeTime >= -domainSpanSec && iterations < 100; relativeTime -= tickIntervalSec) {
        tickValues.unshift(maxTimestamp + relativeTime);
        iterations++;
      }

      xAxis.tickValues(tickValues);
      cachedMaxTimestamp = maxTimestamp;  // Update cached value for formatter
      xAxis.tickFormat(xTickFormatter);
      xGrid.tickValues(tickValues);

      // Update grids
      xGrid.tickSize(-height);
      yGrid.tickSize(-width);

      // Redraw the paths and axes using cached selections (avoids cycle collector pressure)
      cachedSelections.line.attr("d", line(chartData.packetGapMean));
      cachedSelections.xAxis.call(xAxis);
      cachedSelections.yAxis.call(yAxis);
      cachedSelections.xGrid.call(xGrid);
      cachedSelections.yGrid.call(yGrid);
      cachedSelections.minMaxArea.attr("d", minMaxArea(chartData.packetGapMinMax));
    };

    let resizeTimer;
    d3.select(window).on('resize.packetGapContainer', function() {
      clearTimeout(resizeTimer);
      resizeTimer = setTimeout(function() {
        my.charts.resizeChart("#packetGapContainer", size)();
        m.reset();
        my.charts.setDirty();
      }, 100);
    });
    return m;

  }({}));

})(JT);
/* End of jittertrap-chart-pgaps.js */
