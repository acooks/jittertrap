/* jittertrap-chart-tput.js */

/* global d3 */
/* global JT:true */

JT = (function (my) {
  'use strict';

  my.charts.tput = {};

  var chartData = [];

  /* must return a reference to an array of {x:x, y:y} */
  my.charts.tput.getTputRef = function () {
    return chartData;
  };

  my.charts.tput.tputChart = (function (m) {
    var margin = {
      top: 20,
      right: 20,
      bottom: 40,
      left: 75
    };

    var c_width = 960 - margin.left - margin.right;
    var c_height = 300 - margin.top - margin.bottom;
    var xScale = d3.scale.linear().range([0, c_width]);
    var yScale = d3.scale.linear().range([c_height, 0]);

    var xAxis = d3.svg.axis()
                .scale(xScale)
                .ticks(10)
                .orient("bottom");

    var yAxis = d3.svg.axis()
                .scale(yScale)
                .ticks(5)
                .orient("left");

    var line = d3.svg
          .line()
          .x(function(d) { return xScale(d.timestamp); })
          .y(function(d) { return yScale(d.value); })
          .interpolate("basis");

    var svg = {};

    var xGrid = function() {
        return d3.svg.axis()
          .scale(xScale)
           .orient("bottom")
           .ticks(0);
      };

    var yGrid = function() {
        return d3.svg.axis()
          .scale(yScale)
           .orient("left")
           .ticks(0);
      };

    m.reset = function(selectedSeries) {

      d3.select("#chartThroughput").selectAll("svg").remove();

      svg = d3.select("#chartThroughput")
            .append("svg");

      var width = c_width - margin.left - margin.right;
      var height = c_height - margin.top - margin.bottom;

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

      line = d3.svg
          .line()
          .x(function(d) { return xScale(d.timestamp); })
          .y(function(d) { return yScale(d.value); })
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
        .call(xGrid())
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
        .call(yGrid())
        .attr(
             {
               "fill" : "none",
               "shape-rendering" : "crispEdges",
               "stroke" : "grey",
               "opacity": 0.4,
               "stroke-width" : "1px"
             });

      graph.append("path")
         .datum(chartData)
         .attr("class", "line")
         .attr("d", line);

      resize();
    };

    m.redraw = function() {

      var width = c_width - margin.left - margin.right;
      var height = c_height - margin.top - margin.bottom;

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
      xScale.domain(d3.extent(chartData, function(d) {
        return d.timestamp;
      }));

      yScale.domain([0, d3.max(chartData, function(d) {
        return d.value;
      })]);

      xGrid = function() {
        return d3.svg.axis()
          .scale(xScale)
           .orient("bottom")
           .tickSize(-height)
           .ticks(10)
           .tickFormat("");
      };

      yGrid = function() {
        return d3.svg.axis()
          .scale(yScale)
           .orient("left")
           .tickSize(-width)
           .ticks(5)
           .tickFormat("");
      };

      svg = d3.select("#chartThroughput");
      svg.select(".line").attr("d", line(chartData));
      svg.select(".x.axis").call(xAxis);
      svg.select(".y.axis").call(yAxis);
      svg.select(".xGrid").call(xGrid());
      svg.select(".yGrid").call(yGrid());
    };

    var resize = function() {
      var container = d3.select("#chartThroughput");
      var new_width = container.node().getBoundingClientRect().width;
      var new_height = container.node().getBoundingClientRect().height;
      if (new_width === 0 ) {
        return;
      }
      c_width = new_width;
      c_height = new_height;
      container.attr("width", c_width)
               .attr("height", c_height);
      container.select("svg")
               .attr("width", c_width)
               .attr("height", c_height);
    };

    d3.select(window).on('resize.chartThroughput', resize);

    return m;

  }({}));

  return my;
}(JT));
/* End of jittertrap-chart-tput.js */
