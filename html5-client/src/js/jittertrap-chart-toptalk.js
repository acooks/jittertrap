/* jittertrap-chart-toptalk.js */

/* global d3 */
/* global JT:true */

((my) => {
  'use strict';

  my.charts.toptalk = {};

  const chartData = [];

  const clearChartData = function () {
    chartData.length = 0;
  };

  /* must return a reference to an array of {x:x, y:y} */
  my.charts.toptalk.getDataRef = function () {
    return chartData;
  };

  const processAndAggregateChartData = function(incomingData) {
    const LEGEND_DISPLAY_LIMIT = 10;

    if (incomingData.length <= LEGEND_DISPLAY_LIMIT) {
      return incomingData;
    }

    const topNFlows = incomingData.slice(0, LEGEND_DISPLAY_LIMIT);
    const remainingFlows = incomingData.slice(LEGEND_DISPLAY_LIMIT);

    const otherFlow = {
      fkey: 'other',
      tbytes: 0,
      values: []
    };

    const otherValuesMap = new Map();

    remainingFlows.forEach(flow => {
      otherFlow.tbytes += flow.tbytes;
      flow.values.forEach(dataPoint => {
        const currentBytes = otherValuesMap.get(dataPoint.ts) || 0;
        otherValuesMap.set(dataPoint.ts, currentBytes + dataPoint.bytes);
      });
    });

    otherValuesMap.forEach((bytes, ts) => {
      otherFlow.values.push({ ts: ts, bytes: bytes });
    });

    // Ensure the values are sorted by timestamp, as d3 expects
    otherFlow.values.sort((a, b) => a.ts - b.ts);

    return topNFlows.concat(otherFlow);
  };

  my.charts.toptalk.toptalkChart = (function (m) {
    const margin = {
      top: 20,
      right: 20,
      bottom: 440,
      left: 75
    };

    const size = { width: 960, height: 700 };
    let xScale = d3.scaleLinear();
    let yScale = d3.scaleLinear();
    const colorScale = d3.scaleOrdinal(d3.schemeCategory10);
    let xAxis = d3.axisBottom();
    let yAxis = d3.axisLeft();
    let xGrid = d3.axisBottom();
    let yGrid = d3.axisLeft();
    let area = d3.area();

    const stack = d3.stack()
                .order(d3.stackOrderReverse)
                .offset(d3.stackOffsetNone);

    let svg = {};
    let context = {};
    let canvas = {};

    /* Reset and redraw the things that don't change for every redraw() */
    m.reset = function() {

      d3.select("#chartToptalk").selectAll("svg").remove();
      d3.select("#chartToptalk").selectAll("canvas").remove();


      canvas = d3.select("#chartToptalk")
            .append("canvas")
            .style("position", "absolute");

      my.charts.resizeChart("#chartToptalk", size)();
      context = canvas.node().getContext("2d");

      svg = d3.select("#chartToptalk")
            .append("svg")
            .style("position", "relative");

      area.context(context);


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

      graph.append("g")
         .attr("class", "x axis")
         .attr("transform", "translate(0," + height + ")")
         .call(xAxis);

      graph.append("text")
           .attr("class", "axis-label")
           .attr("text-anchor", "middle")
           .attr("x", width/2)
           .attr("y", height + 35)
           .text("Time");

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
         .text("Bytes");

      graph.append("g")
        .attr("class", "xGrid")
        .attr("transform", "translate(0," + height + ")")
        .call(xGrid);

      graph.append("g")
        .attr("class", "yGrid")
        .call(yGrid);

      context.clearRect(0, 0, width, height);

      svg.append("g")
         .attr("class", "barsbox")
         .attr("id", "barsbox")
         .append("text")
           .text("Byte Distribution")

      svg.append("g")
         .attr("class", "legendbox")
         .attr("id", "ttlegendbox")
         .attr("transform", "translate(" + margin.left + ", 400)")
         .append("text")
           .attr("class", "legendheading legend-text");

      const legendHeader = svg.select(".legendheading");
      legendHeader.append("tspan")
        .attr("x", "25em")
        .attr("text-anchor", "end")
        .text("Source IP");
      legendHeader.append("tspan").attr("x", "25.5em").text(":Port");
      legendHeader.append("tspan").attr("x", "30.5em").text("->");
      legendHeader.append("tspan").attr("x", "32.5em").text("Destination IP");
      legendHeader.append("tspan").attr("x", "58em").text(":Port");
      legendHeader.append("tspan").attr("x", "63.5em").text("| Protocol");
      legendHeader.append("tspan").attr("x", "70em").text("| T/Class");


      my.charts.resizeChart("#chartToptalk", size)();
    };

    /* Reformat chartData to work with the new d3 v4 API
     * Ref: https://github.com/d3/d3-shape/blob/master/README.md#stack */
    const formatDataAndGetMaxSlice = function(chartData) {
      // Use a Map for O(1) indexed lookups, which is much faster than map().indexOf().
      const binsMap = new Map();
      let maxSlice = 0;

      for (let i = 0; i < chartData.length; i++) {
        const row = chartData[i];
        for (let j = 0; j < row.values.length; j++) {
          const o = row.values[j];
          const ts = o.ts;
          const fkey = row.fkey;
          const bytes = o.bytes;

          // Check if we have seen this timestamp before.
          if (!binsMap.has(ts)) {
            // If not, create a new entry for it in the map.
            binsMap.set(ts, { "ts": ts });
          }

          // Get the bin for the current timestamp.
          const bin = binsMap.get(ts);
          bin[fkey] = (bin[fkey] || 0) + bytes;
        }
      }

      const formattedData = Array.from(binsMap.values());

      // Calculate the sum of each time slice to find the maximum for the Y-axis domain.
      formattedData.forEach(slice => {
        let currentSliceSum = 0;
        for (const key in slice) {
          if (key !== 'ts') {
            currentSliceSum += slice[key];
          }
        }
        if (currentSliceSum > maxSlice) {
          maxSlice = currentSliceSum;
        }
      });

      // Convert the map's values back into an array for d3.stack().
      return { formattedData, maxSlice };
    }

    const getFlowColor = (key) => {
      if (key === 'other') {
        return '#cccccc'; // a neutral grey
      }
      return colorScale(key);
    };


    /* Update the chart (try to avoid memory allocations here!) */
    m.redraw = function() {

      // Process the raw chartData to aggregate "other" flows before drawing
      const processedChartData = processAndAggregateChartData(chartData);

      const width = size.width - margin.left - margin.right;
      const height = size.height - margin.top - margin.bottom;

      xScale = d3.scaleLinear().range([0, width]);
      /* compute the domain of x as the [min,max] extent of timestamps
       * of the first (largest) flow */
      if (processedChartData && processedChartData[0])
        xScale.domain(d3.extent(processedChartData[0].values, d => d.ts));

      const { formattedData, maxSlice } = formatDataAndGetMaxSlice(processedChartData);

      const yPow = d3.select('input[name="y-axis-is-log"]:checked').node().value;

      if (yPow == 1) {
        yScale = d3.scalePow().exponent(0.5).clamp(true).range([height, 0]);
      } else {
        yScale = d3.scaleLinear().clamp(true).range([height, 0]);
      }
      yScale.domain([0, maxSlice]);

      xAxis.scale(xScale);
      yAxis.scale(yScale);

      xGrid.scale(xScale);
      yGrid.scale(yScale);

      svg = d3.select("#chartToptalk");

      svg.select(".x.axis").transition().duration(20).call(xAxis);
      svg.select(".y.axis").transition().duration(20).call(yAxis);
      svg.select(".xGrid").call(xGrid);
      svg.select(".yGrid").call(yGrid);

      const fkeys = processedChartData.map(f => f.fkey);
      colorScale.domain(fkeys);

      stack.keys(fkeys);

      // Format the data, so they're flat arrays
      const stackedChartData = stack(formattedData);

      area = d3.area()
               .curve(d3.curveMonotoneX)
               .context(context)
               .x(d => xScale(d.data.ts))
               .y0(d => yScale(d[0] || 0))
               .y1(d => yScale(d[1] || 0));

      context.clearRect(0, 0, width, height);

      stackedChartData.forEach(layer => {
        context.beginPath();
        area(layer);
        context.fillStyle = getFlowColor(layer.key);
        context.fill();
      });

      // distribution bar
      const tbytes = processedChartData.reduce((sum, f) => sum + f.tbytes, 0);

      let rangeStop = 0;
      const barData = processedChartData.map(f => {
        const new_d = {
          k: f.fkey,
          x0: rangeStop,
          x1: (rangeStop + f.tbytes)
        };
        rangeStop = new_d.x1;
        return new_d;
      });

      const x = d3.scaleLinear()
                      .rangeRound([0, width])
                      .domain([0,tbytes]);

      const y = d3.scaleBand()
                      .range([0, 10])
                      .round(.3);

      const barsbox = svg.select("#barsbox");
      barsbox.selectAll(".subbar").remove();
      const bars = barsbox.selectAll("rect")
                    .data(barData)
                    .enter().append("g").attr("class", "subbar");

      bars.append("rect")
          .attr("height", 23)
          .attr("y", 9)
          .attr("x", d => x(d.x0))
          .attr("width", d => x(d.x1) - x(d.x0))
          .style("fill", d => getFlowColor(d.k));

      barsbox.attr("transform",
                   "translate(" + margin.left + "," + 350 + ")");

      // legend box handling
      const legendbox = svg.select("#ttlegendbox");

      // General Update Pattern for the legend
      const legend = legendbox.selectAll(".legend")
        .data(fkeys, d => d); // Use a key function for object constancy

      // EXIT - remove old legend items that are no longer in fkeys
      legend.exit().remove();

      // ENTER - create new <g> elements for new flows
      const legendEnter = legend.enter()
        .append("g")
        .attr("class", "legend");

      // Append rect and text elements only to the new <g> elements
      legendEnter.append("rect")
        .attr("x", 0)
        .attr("width", 18)
        .attr("height", 18);

      const legendTextEnter = legendEnter.append("text")
        .attr("class", "legend-text")
        .attr("y", 9)
        .attr("dy", ".35em");

      // Add the complex <tspan> structure only ONCE when elements are created
      legendTextEnter.each(function(d) {
        const textNode = d3.select(this);
        if (d === 'other') {
          textNode.append("tspan").attr("x", 25).text("Other Flows");
        } else {
          const parts = d.split('/');
          const sourceIP = parts[1];
          const sourcePort = parts[2];
          const destIP = parts[3];
          const destPort = parts[4];
          const proto = parts[5];
          const tclass = parts[6];

          textNode.append("tspan").attr("x", "25em").attr("text-anchor", "end").text(sourceIP);
          textNode.append("tspan").attr("x", "25.5em").text(":" + sourcePort.padEnd(6));
          textNode.append("tspan").attr("x", "30.5em").text("->");
          textNode.append("tspan").attr("x", "32.5em").text(destIP);
          textNode.append("tspan").attr("x", "58em").text(":" + destPort);
          textNode.append("tspan").attr("x", "63.5em").text("| " + proto);
          textNode.append("tspan").attr("x", "70em").text("| " + tclass);
        }
      });

      // UPDATE + ENTER - update positions and colors for all visible items
      const legendUpdate = legend.merge(legendEnter);
      legendUpdate.attr("transform", (d, i) => "translate(0, " + ((i + 1) * 25) + ")");
      legendUpdate.select("rect").style("fill", getFlowColor);
    };


    /* Set the callback for resizing the chart */
    d3.select(window).on('resize.chartToptalk',
                         my.charts.resizeChart("#chartToptalk", size));

    return m;

  }({}));

})(JT);
/* End of jittertrap-chart-toptalk.js */
