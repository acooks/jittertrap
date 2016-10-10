/* jittertrap-chart-toptalk.js */

/* global d3 */
/* global JT:true */

JT = (function (my) {
  'use strict';

  my.charts.toptalk = {};

  var chartData = [];

  var clearChartData = function () {
    chartData.length = 0;
  };

  /* must return a reference to an array of {x:x, y:y} */
  my.charts.toptalk.getDataRef = function () {
    return chartData;
  };

  my.charts.toptalk.toptalkChart = (function (m) {
    var margin = {
      top: 20,
      right: 20,
      bottom: 40,
      left: 75
    };

    var size = {};
    size.width = 960 - margin.left - margin.right;
    size.height = 300 - margin.top - margin.bottom;
    var xScale = d3.scale.linear().range([0, size.width]);
    var yScale = d3.scale.linear().range([size.height, 0]);
    var colorScale = d3.scale.category10();

    var xAxis = d3.svg.axis()
                .scale(xScale)
                .ticks(10)
                .orient("bottom");

    var yAxis = d3.svg.axis()
                .scale(yScale)
                .ticks(5)
                .orient("left");

/*
    var line = d3.svg
          .line()
          .x(function(d) { return xScale(d.f.ts); })
          .y(function(d) { return yScale(d.f[0].bytes); })
          .interpolate("basis");
*/

    var stack = d3.layout.stack()
                .offset("zero")
                .values(function(flow) { return flow.values; })
                .x(function(d) { return d.ts; })
                .y(function(d) { return d.bytes; })

    var area = d3.svg.area()
               .x(function (d) { return xScale(d.ts); })
               .y0(function (d) { return (d.y); })
               .y1(function (d) { return yScale(d.y0 + d.y); });
    
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
   
    m.reset = function() {

      d3.select("#chartToptalk").selectAll("svg").remove();

      svg = d3.select("#chartToptalk")
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

/*
      line = d3.svg.line()
             .x(function(d) { return xScale(d.f[0].ts); })
             .y(function(d) { return yScale(d.f[0].bytes); })
             .interpolate("basis");
*/

      svg.attr("width", width + margin.left + margin.right)
         .attr("height", height + margin.top + margin.bottom);

      var graph = svg.append("g")
         .attr("transform", "translate(" + margin.left + "," + margin.top + ")");

      graph.append("text")
         .attr("class", "title")
         .attr("text-anchor", "middle")
         .attr("x", width/2)
         .attr("y", -margin.top/2)
         .text("Top flows");

      graph.append("g")
         .attr("class", "x axis")
         .attr("transform", "translate(0," + height + ")")
         .call(xAxis);

      graph.append("text")
           .attr("class", "x label")
           .attr("text-anchor", "middle")
           .attr("x", width/2)
           .attr("y", height + 15 + 0.5 * margin.bottom)
           .text("Time");

      graph.append("g")
         .attr("class", "y axis")
         .call(yAxis)
         .append("text")
         .attr("x", -margin.left)
         .attr("transform", "rotate(-90)")
         .attr("y", -margin.left)
         .attr("dy", ".71em")
         .style("text-anchor", "end")
         .text("Bytes");

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

      graph.append("g")
         .attr("id", "flows");

      my.charts.resizeChart("#chartToptalk", size)();
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


      /* Scale the range of the data again
      /* by computing the range of 'ts' in the first flow */
      if (chartData[0]) {
        xScale.domain(d3.extent(chartData[0].values, function(d) {
          return d.ts;
        }));
      }

      var maxBytesSlice = function(chartData) {
        var i, j;
        var flowCount, sampleCount, maxSlice = 0;

        flowCount = chartData.length;
        if (!flowCount) {
          return 0;
        }
        sampleCount = chartData[0].values.length;

        for (i = 0; i < sampleCount; i++) {
          var thisSliceBytes = 0;
          for (j = 0; j < flowCount; j++) {
            thisSliceBytes += chartData[j].values[i].bytes;
          }
          if (thisSliceBytes > maxSlice) {
            maxSlice = thisSliceBytes;
          }
        }
        return maxSlice;
      };

      yScale.domain([0, maxBytesSlice(chartData)]);

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

      svg = d3.select("#chartToptalk");
      //svg.select(".line").attr("d", line(chartData));
      svg.select(".x.axis").call(xAxis);
      svg.select(".y.axis").call(yAxis);
      svg.select(".xGrid").call(xGrid());
      svg.select(".yGrid").call(yGrid());

      var fkeys = chartData.map(function(f) { return f.fkey; });
      colorScale.domain(fkeys);
      var stackedChartData = stack(chartData);

      var title = function(fkey) {
        var t = fkey.split("/");
        return "" + t[1] + ":" + t[2] + " -> " + t[3] + ":" + t[4] +
               " " + t[5];
      };

      area = d3.svg.area()
               .interpolate("monotone")
               .x(function (d) { return xScale(d.ts); })
               .y0(function (d) { return yScale(d.y0); })
               .y1(function (d) { return yScale(d.y0 + d.y); });

      svg.select("#flows").selectAll(".layer").remove();

      svg.select("#flows").selectAll("path")
         .data(stackedChartData)
       .enter().append("path")
         .attr("class", "layer")
         .attr("d", function(d) { return area(d.values); })
         .style("fill", function(d, i) { return colorScale(i); })
         .append("svg:title").text(function(d) { return title(d.fkey); });
    };

    d3.select(window).on('resize.chartToptalk',
                         my.charts.resizeChart("#chartToptalk", size));

    return m;

  }({}));

  return my;
}(JT));
/* End of jittertrap-chart-toptalk.js */
