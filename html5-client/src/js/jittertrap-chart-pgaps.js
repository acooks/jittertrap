/* jittertrap-chart-pgaps.js */

/* global d3 */
/* global JT:true */

JT = (function (my) {
  'use strict';

  my.charts.pgaps = {};

  var chartData = { packetGapMean: [], packetGapMinMax: [] };

  my.charts.pgaps.getMeanRef = function () {
    return chartData.packetGapMean;
  };

  my.charts.pgaps.getMinMaxRef = function () {
    return chartData.packetGapMinMax;
  };

  my.charts.pgaps.packetGapChart = (function (m) {
    var margin = {
      top: 20,
      right: 20,
      bottom: 40,
      left: 75
    };

    var size = { width: 960, height: 300 };
    var xScale = d3.scale.linear().range([0, size.width]);
    var yScale = d3.scale.linear().range([size.height, 0]);
    var xAxis = d3.svg.axis();
    var yAxis = d3.svg.axis();
    var xGrid = d3.svg.axis();
    var yGrid = d3.svg.axis();
    var line = d3.svg.line();
    var minMaxArea = d3.svg.area();

    var svg = {};

    m.reset = function() {

      d3.select("#packetGapContainer").selectAll("svg").remove();

      svg = d3.select("#packetGapContainer")
            .append("svg");

      var width = size.width - margin.left - margin.right;
      var height = size.height - margin.top - margin.bottom;

      xScale = d3.scale.linear().range([0, width]);
      yScale = d3.scale.linear().range([height, 0]);

      xAxis = d3.svg.axis()
              .scale(xScale)
              .ticks(10)
              .orient("bottom");

      yAxis = d3.svg.axis()
              .scale(yScale)
              .ticks(5)
              .orient("left");

      line = d3.svg.line()
        .x(function(d) { return xScale(d.x); })
        .y(function(d) { return yScale(d.y); })
        .interpolate("basis");


      minMaxArea = d3.svg.area()
        .x (function (d) { return xScale(d.x) || 1; })
        .y0(function (d) { return yScale(d.y[0]) || 0; })
        .y1(function (d) { return yScale(d.y[1]) || 0; })
        .interpolate("basis");

      svg.attr("width", width + margin.left + margin.right)
         .attr("height", height + margin.top + margin.bottom);

      var graph = svg.append("g")
         .attr("transform", "translate(" + margin.left + "," + margin.top + ")");

      graph.append("text")
         .attr("class", "title")
         .attr("text-anchor", "middle")
         .attr("x", width/2)
         .attr("y", -margin.top/2)
         .text("Inter Packet Gap");

      graph.append("g")
         .attr("class", "x axis")
         .attr("transform", "translate(0," + height + ")")
         .call(xAxis);

      graph.append("text")
           .attr("class", "x label")
           .attr("text-anchor", "middle")
           .attr("x", width/2)
           .attr("y", height + 15 + 0.5 * margin.bottom)
           .text("Time (ms)");

      graph.append("g")
         .attr("class", "y axis")
         .call(yAxis)
         .append("text")
         .attr("x", -margin.left)
         .attr("transform", "rotate(-90)")
         .attr("y", -margin.left)
         .attr("dy", ".71em")
         .style("text-anchor", "end")
         .text("Packet Gap (ms, mean)");

      graph.append("g")
        .attr("class", "xGrid")
        .attr("transform", "translate(0," + height + ")")
        .call(xGrid)
        .attr(
             {
               "fill" : "none",
               "shape-rendering" : "crispEdges",
               "stroke" : "grey",
               "opacity": 0.4,
               "stroke-width" : "1px"
             });

      graph.append("g")
        .attr("class", "yGrid")
        .call(yGrid)
        .attr(
             {
               "fill" : "none",
               "shape-rendering" : "crispEdges",
               "stroke" : "grey",
               "opacity": 0.4,
               "stroke-width" : "1px"
             });

      graph.append('path')
        .datum(chartData.packetGapMinMax)
        .attr('class', 'minMaxArea')
        .attr('d', minMaxArea)
        .attr({"fill": "pink", "opacity": 0.8});

      graph.append("path")
         .datum(chartData.packetGapMean)
         .attr("class", "line")
         .attr("d", line);

      my.charts.resizeChart("#packetGapContainer", size)();
    };

    m.redraw = function() {

      var width = size.width - margin.left - margin.right;
      var height = size.height - margin.top - margin.bottom;

      xScale = d3.scale.linear().range([0, width]);
      yScale = d3.scale.linear().range([height, 0]);

      xAxis = d3.svg.axis()
              .scale(xScale)
              .ticks(10)
              .orient("bottom");

      yAxis = d3.svg.axis()
              .scale(yScale)
              .ticks(5)
              .orient("left");

      /* Scale the range of the data again */
      xScale.domain(d3.extent(chartData.packetGapMean, function(d) {
        return d.x;
      }));

      yScale.domain([0, d3.max(chartData.packetGapMinMax, function(d) {
        return d.y[1];
      })]);

      xGrid = d3.svg.axis()
          .scale(xScale)
           .orient("bottom")
           .tickSize(-height)
           .ticks(10)
           .tickFormat("");

      yGrid = d3.svg.axis()
          .scale(yScale)
           .orient("left")
           .tickSize(-width)
           .ticks(5)
           .tickFormat("");

      svg = d3.select("#packetGapContainer");
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

  return my;
}(JT));
/* End of jittertrap-chart-pgaps.js */
