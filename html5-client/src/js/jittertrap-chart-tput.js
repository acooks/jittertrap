/* jittertrap-chart-tput.js */

/* global d3 */
/* global JT:true */

((my) => {
  'use strict';

  my.charts.tput = {};

  const chartData = [];

  /* must return a reference to an array of {x:x, y:y} */
  my.charts.tput.getTputRef = function () {
    return chartData;
  };

  my.charts.tput.tputChart = (function (m) {
    // Use smaller margins on mobile devices
    const isMobile = window.innerWidth <= 768;
    const margin = {
      top: isMobile ? 15 : 20,
      right: isMobile ? 10 : 20,
      bottom: isMobile ? 35 : 40,
      left: isMobile ? 50 : 75
    };

    const size = { width: 960, height: 400 };
    let xScale = d3.scaleLinear().range([0, size.width]);
    let yScale = d3.scaleLinear().range([size.height, 0]);

    const formatBitrate = function(d) {
        // d is in bps. d3.format('.2s') auto-scales and adds SI prefix.
        return d3.format(".2s")(d) + "bps";
    };

    let xAxis = d3.axisBottom();
    let yAxis = d3.axisLeft().tickFormat(formatBitrate);
    let xGrid = d3.axisBottom();
    let yGrid = d3.axisLeft();

    let line = d3.line()
          .x(d => xScale(d.timestamp))
          .y(d => yScale(d.value))
          .curve(d3.curveBasis);

    let svg = {};

    // Cached D3 selections to avoid repeated select() calls which create
    // new selection objects that form cycles with DOM nodes, increasing
    // Cycle Collector (CC) pressure in Firefox
    let cachedSelections = {
      line: null,
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

    m.reset = function(selectedSeries) {
      // Clear cached selections before removing DOM (breaks cycles)
      cachedSelections.line = null;
      cachedSelections.xAxis = null;
      cachedSelections.yAxis = null;
      cachedSelections.xGrid = null;
      cachedSelections.yGrid = null;

      d3.select("#chartThroughput").selectAll("svg").remove();

      svg = d3.select("#chartThroughput")
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

      line = d3.line()
          .x(d => xScale(d.timestamp))
          .y(d => yScale(d.value))
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
         .text(selectedSeries.title);

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
           .text(selectedSeries.xlabel);

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
         .text(selectedSeries.ylabel);

      cachedSelections.xGrid = graph.append("g")
        .attr("class", "xGrid")
        .attr("transform", "translate(0," + height + ")")
        .call(xGrid);

      cachedSelections.yGrid = graph.append("g")
        .attr("class", "yGrid")
        .call(yGrid);

      // Add clip path to prevent drawing outside chart area
      graph.append("defs").append("clipPath")
        .attr("id", "clip-tput")
        .append("rect")
        .attr("width", width)
        .attr("height", height);

      cachedSelections.line = graph.append("path")
         .datum(chartData)
         .attr("class", "line")
         .attr("clip-path", "url(#clip-tput)")
         .attr("d", line);

      my.charts.resizeChart("#chartThroughput", size)();
    };

    m.redraw = function() {

      // No data, no draw.
      if (chartData.length === 0) {
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
        xScale.domain(d3.extent(chartData, d => d.timestamp));
      }
      yScale.domain([0, d3.max(chartData, d => d.value)]);

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

      // Redraw the line and axes using cached selections (avoids cycle collector pressure)
      cachedSelections.line.attr("d", line(chartData));
      cachedSelections.xAxis.call(xAxis);
      cachedSelections.yAxis.call(yAxis);
      cachedSelections.xGrid.call(xGrid);
      cachedSelections.yGrid.call(yGrid);
    };

    let resizeTimer;
    d3.select(window).on('resize.chartThroughput', function() {
      clearTimeout(resizeTimer);
      resizeTimer = setTimeout(function() {
        my.charts.resizeChart("#chartThroughput", size)();
        m.reset(my.core.getSelectedSeries());
        my.charts.setDirty();
      }, 100);
    });
    return m;

  }({}));

})(JT);
/* End of jittertrap-chart-tput.js */
