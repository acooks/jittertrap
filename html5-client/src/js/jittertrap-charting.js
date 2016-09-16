/* jittertrap-charting.js */

/* global d3 */
/* global JT:true */

JT = (function (my) {
  'use strict';

  my.charts = {};

  /* Add a container for charting parameters */
  var params = {};

  /* time (milliseconds) represented by each point on the chart */
  params.plotPeriod        = 1000;
  params.plotPeriodMin     = 1;
  params.plotPeriodMax     = 1000;

  /* chart redraw/refresh/updates; milliseconds; 40ms == 25 Hz */
  params.redrawPeriod      = 60;
  params.redrawPeriodMin   = 40;
  params.redrawPeriodMax   = 100;
  params.redrawPeriodSaved = 0;

  var chartData = {
    mainChart: [],
    packetGapMean: [],
    packetGapMinMax: [],
  };

  var clearChartData = function () {
    chartData.mainChart.length = 0;
    chartData.packetGapMean.length = 0;
    chartData.packetGapMinMax.length = 0;
  };

  /* must return a reference to an array of {x:x, y:y} */
  my.charts.getMainChartRef = function () {
    return chartData.mainChart;
  };

  my.charts.getPacketGapMeanRef = function () {
    return chartData.packetGapMean;
  };

  my.charts.getPacketGapMinMaxRef = function () {
    return chartData.packetGapMinMax;
  };

  my.charts.getTopFlowsRef = function () {
    return my.charts.toptalk.getDataRef();
  };

  my.charts.mainChart = (function (m) {
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
         .datum(chartData.mainChart)
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
      xScale.domain(d3.extent(chartData.mainChart, function(d) {
        return d.timestamp;
      }));

      yScale.domain([0, d3.max(chartData.mainChart, function(d) {
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
      svg.select(".line").attr("d", line(chartData.mainChart));
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


  my.charts.packetGapChart = (function (m) {
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

    var line = d3.svg.line()
          .x(function(d) { return xScale(d.timestamp); })
          .y(function(d) { return yScale(d.value); })
          .interpolate("basis");

    var minMaxArea = d3.svg.area()
        .x (function (d) { return xScale(d.x) || 1; })
        .y0(function (d) { return yScale(d.y[0]); })
        .y1(function (d) { return yScale(d.y[1]); })
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

    var resize = function() {
      var container = d3.select("#packetGapContainer");
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

    m.reset = function() {

      d3.select("#packetGapContainer").selectAll("svg").remove();

      svg = d3.select("#packetGapContainer")
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

      graph.append('path')
        .datum(chartData.packetGapMinMax)
        .attr('class', 'minMaxArea')
        .attr('d', minMaxArea)
        .attr({"fill": "pink", "opacity": 0.8});

      graph.append("path")
         .datum(chartData.packetGapMean)
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
      xScale.domain(d3.extent(chartData.packetGapMean, function(d) {
        return d.x;
      }));

      yScale.domain([0, d3.max(chartData.packetGapMinMax, function(d) {
        return d.y[1];
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

      svg = d3.select("#packetGapContainer");
      svg.select(".line").attr("d", line(chartData.packetGapMean));
      svg.select(".x.axis").call(xAxis);
      svg.select(".y.axis").call(yAxis);
      svg.select(".xGrid").call(xGrid());
      svg.select(".yGrid").call(yGrid());
      svg.select(".minMaxArea").attr("d", minMaxArea(chartData.packetGapMinMax));
    };

    d3.select(window).on('resize.packetGapContainer', resize);

    return m;

  }({}));

  var resetChart = function() {
    var selectedSeriesOpt = $("#chopts_series option:selected").val();
    my.core.setSelectedSeriesName(selectedSeriesOpt);
    clearChartData();
    my.charts.mainChart.reset(my.core.getSelectedSeries());
    my.charts.packetGapChart.reset();
    my.charts.toptalk.toptalkChart.reset();
  };

  var renderCount = 0;
  var renderTime = 0;

  var tuneChartUpdatePeriod = function() {

    var tuneWindowSize = 5; // how often to adjust the updatePeriod.

    if ((renderCount % tuneWindowSize) !== 0) {
      return;
    }

    var avgRenderTime =  Math.floor(renderTime / renderCount);
    //console.log("Rendering time: " + avgRenderTime
    //            + " Processing time: " + procTime
    //            + " Charting Period: " + chartingPeriod);

    params.redrawPeriod = params.plotPeriod / 2;

    if (params.redrawPeriod < params.redrawPeriodMin) {
      params.redrawPeriod = params.redrawPeriodMin;
    } else if (params.redrawPeriod > params.redrawPeriodMax) {
      params.redrawPeriod = params.redrawPeriodMax;
    }

    setUpdatePeriod();

    renderCount = renderCount % tuneWindowSize;
    renderTime = 0;
  };


  var renderGraphs = function() {
    var d1 = Date.now();
    my.charts.mainChart.redraw();
    my.charts.packetGapChart.redraw();
    my.charts.toptalk.toptalkChart.redraw();

    var d2 = Date.now();
    renderCount++;
    renderTime += d2 - d1;
    tuneChartUpdatePeriod();
  };

  var drawIntervalID = setInterval(renderGraphs, params.redrawPeriod);

  var setUpdatePeriod = function() {
    var updateRate = 1000.0 / params.redrawPeriod; /* Hz */
    clearInterval(drawIntervalID);
    drawIntervalID = setInterval(renderGraphs, params.redrawPeriod);
  };

  var toggleStopStartGraph = function() {
    var maxUpdatePeriod = 9999999999;
    if (params.redrawPeriod !== maxUpdatePeriod) {
      params.redrawPeriodSaved = params.redrawPeriod;
      params.redrawPeriod = maxUpdatePeriod;
    } else {
      params.redrawPeriod = params.redrawPeriodSaved;
    }
    setUpdatePeriod();
    return false;
  };

  var getChartPeriod = function () {
    return params.plotPeriod;
  };

  var setChartPeriod = function (newPeriod) {
    if (newPeriod < params.plotPeriodMin) {
       newPeriod = params.plotPeriodMin;
    } else if (newPeriod > params.plotPeriodMax) {
       newPeriod = params.plotPeriodMax;
    }

    params.plotPeriod = newPeriod;
    var sampleCount = JT.core.sampleCount(newPeriod);
    JT.core.resizeDataBufs(sampleCount);
    JT.charts.resetChart();

    return {newPeriod: newPeriod, sampleCount: sampleCount};
  };

  /* Export "public" functions */
  my.charts.toggleStopStartGraph = toggleStopStartGraph;
  my.charts.setUpdatePeriod = setUpdatePeriod;
  my.charts.getChartPeriod = getChartPeriod;
  my.charts.setChartPeriod = setChartPeriod;
  my.charts.resetChart = resetChart;

  return my;
}(JT));
/* End of jittertrap-charting.js */
