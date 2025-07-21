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
    const margin = {
      top: 20,
      right: 20,
      bottom: 40,
      left: 75
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

    m.reset = function() {

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
         .attr("y", 0)
         .attr("dy", "-0.6em")
         .text("Inter Packet Gap");

      graph.append("g")
         .attr("class", "x axis")
         .attr("transform", "translate(0," + height + ")")
         .call(xAxis);

      graph.append("text")
           .attr("class", "x-axis-label")
           .attr("text-anchor", "middle")
           .attr("x", width/2)
           .attr("y", height + margin.bottom - 10)
           .text("Time (ms)");

      graph.append("g")
         .attr("class", "y axis")
         .call(yAxis);

      graph.append("text")
         .attr("class", "y-axis-label")
         .attr("transform", "rotate(-90)")
         .attr("y", 0 - margin.left)
         .attr("x", 0 - (height / 2))
         .attr("dy", "1em")
         .style("text-anchor", "middle")
         .text("Packet Gap (ms)");

      graph.append("g")
        .attr("class", "xGrid")
        .attr("transform", "translate(0," + height + ")")
        .call(xGrid);


      graph.append("g")
        .attr("class", "yGrid")
        .call(yGrid);


      graph.append('path')
        .datum(chartData.packetGapMinMax)
        .attr('class', 'minMaxArea')
        .attr('d', minMaxArea)
        .style('fill', 'pink')
        .style("opacity", 0.8);

      graph.append("path")
         .datum(chartData.packetGapMean)
         .attr("class", "line")
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

      /* Scale the range of the data again */
      xScale.domain(d3.extent(chartData.packetGapMean, d => d.x));
      yScale.domain([0, d3.max(chartData.packetGapMinMax, d => d.y[1])]);

      // Update grids
      xGrid.tickSize(-height);
      yGrid.tickSize(-width);

      // Redraw the paths and axes.
      svg.select(".line").attr("d", line(chartData.packetGapMean));
      svg.select(".x.axis").call(xAxis);
      svg.select(".y.axis").call(yAxis);
      svg.select(".xGrid").call(xGrid);
      svg.select(".yGrid").call(yGrid);
      svg.select(".minMaxArea").attr("d", minMaxArea(chartData.packetGapMinMax));
    };

    d3.select(window).on('resize.packetGapContainer',
                         my.charts.resizeChart("#packetGapContainer", size));
    return m;

  }({}));

})(JT);
/* End of jittertrap-chart-pgaps.js */
