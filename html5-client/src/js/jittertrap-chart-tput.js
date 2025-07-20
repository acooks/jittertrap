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

    let line = d3.line()
          .x(function(d) { return xScale(d.timestamp); })
          .y(function(d) { return yScale(d.value); })
          .curve(d3.curveBasis);

    let svg = {};

    m.reset = function(selectedSeries) {

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
          .x(function(d) { return xScale(d.timestamp); })
          .y(function(d) { return yScale(d.value); })
          .curve(d3.curveBasis);

      svg.attr("width", width + margin.left + margin.right)
         .attr("height", height + margin.top + margin.bottom);


      const graph = svg.append("g")
         .attr("transform", "translate(" + margin.left + "," + margin.top + ")");

      graph.append("text")
         .attr("class", "title")
         .attr("text-anchor", "middle")
         .attr("x", width/2)
         .attr("y", -margin.top/2)
         .text(selectedSeries.title);

      graph.append("g")
         .attr("class", "x axis")
         .attr("transform", "translate(0," + height + ")")
         .call(xAxis);

      graph.append("text")
           .attr("class", "x label")
           .attr("text-anchor", "middle")
           .attr("x", width/2)
           .attr("y", height + 15 + 0.5 * margin.bottom)
           .text(selectedSeries.xlabel);

      graph.append("g")
         .attr("class", "y axis")
         .call(yAxis)
         .append("text")
         .attr("x", -margin.left)
         .attr("transform", "rotate(-90)")
         .attr("y", -margin.left)
         .attr("dy", ".71em")
         .style("text-anchor", "end")
         .text(selectedSeries.ylabel);

      graph.append("g")
        .attr("class", "xGrid")
        .attr("transform", "translate(0," + height + ")")
        .call(xGrid);

      graph.append("g")
        .attr("class", "yGrid")
        .call(yGrid);

      graph.append("path")
         .datum(chartData)
         .attr("class", "line")
         .attr("d", line);

      my.charts.resizeChart("#chartThroughput", size)();
    };

    m.redraw = function() {

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

      /* Scale the range of the data again */
      xScale.domain(d3.extent(chartData, function(d) {
        return d.timestamp;
      }));

      yScale.domain([0, d3.max(chartData, function(d) {
        return d.value;
      })]);

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

      svg = d3.select("#chartThroughput");
      svg.select(".line").attr("d", line(chartData));
      svg.select(".x.axis").call(xAxis);
      svg.select(".y.axis").call(yAxis);
      svg.select(".xGrid").call(xGrid);
      svg.select(".yGrid").call(yGrid);
    };

    d3.select(window).on('resize.chartThroughput',
                         my.charts.resizeChart("#chartThroughput", size));
    return m;

  }({}));

})(JT);
/* End of jittertrap-chart-tput.js */
