/* jittertrap-charting.js */

/* global CanvasJS */
/* global JT:true */

JT = (function (my) {
  'use strict';

  my.charts = {};

  /* Add a container for charting parameters */
  var params = {};

  /* time (milliseconds) represented by each point on the chart */
  params.plotPeriod        = 50;
  params.plotPeriodMin     = 1;
  params.plotPeriodMax     = 500;

  /* chart redraw/refresh/updates; milliseconds; 40ms == 25 Hz */
  params.redrawPeriod      = 60;
  params.redrawPeriodMin   = 40;
  params.redrawPeriodMax   = 100;
  params.redrawPeriodSaved = 0;

  var chartData = {
    mainChart: [],
    histogram: [],
    basicStats: [],
    packetGapMean: [],
    packetGapMeanNew: [],
    packetGapMinMax: [],
  };

  var clearChartData = function () {
    chartData.mainChart.length = 0;
    chartData.histogram.length = 0;
    chartData.basicStats.length = 0;
    chartData.packetGapMean.length = 0;
    chartData.packetGapMeanNew.length = 0;
    chartData.packetGapMinMax.length = 0;
  };

  /* must return a reference to an array of {x:x, y:y, label:l} */
  my.charts.getHistogramRef = function () {
    return chartData.histogram;
  };

  /* must return a reference to an array of {x:x, y:y, label:l} */
  my.charts.getBasicStatsRef = function () {
    return chartData.basicStats;
  };

  /* must return a reference to an array of {x:x, y:y} */
  my.charts.getMainChartRef = function () {
    return chartData.mainChart;
  };

  my.charts.getPacketGapMeanRef = function () {
    return chartData.packetGapMean;
  };

  my.charts.getPacketGapMeanRefNew = function() {
    return chartData.packetGapMeanNew;
  };

  my.charts.getPacketGapMinMaxRef = function () {
    return chartData.packetGapMinMax;
  };

  my.charts.mainChart = (function (m) {
    var margin = {
      top: 20,
      right: 20,
      bottom: 40,
      left: 75
    };

    var width = 960 - margin.left - margin.right;
    var height = 300 - margin.top - margin.bottom;
    var xScale = d3.scale.linear().range([0, width]);
    var yScale = d3.scale.linear().range([height, 0]);
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
    
    var svg = {}

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

      width = $("#chartThroughput").width() - margin.left - margin.right;
      height = $("#chartThroughput").height() - margin.top - margin.bottom;

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
         .attr("transform", "translate(" + margin.left + "," + margin.top + ")")

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

    };

    m.redraw = function() {

      width = $("#chartThroughput").width() - margin.left - margin.right;
      height = $("#chartThroughput").height() - margin.top - margin.bottom;

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
           .tickFormat("")
      };

      yGrid = function() {
        return d3.svg.axis()
          .scale(yScale)
           .orient("left")
           .tickSize(-width)
           .ticks(5)
           .tickFormat("")
      };

      svg = d3.select("#chartThroughput");
      svg.select(".line").attr("d", line(chartData.mainChart));
      svg.select(".x.axis").call(xAxis);
      svg.select(".y.axis").call(yAxis);
      svg.select(".xGrid").call(xGrid());
      svg.select(".yGrid").call(yGrid());

    };


    return m;

  }({}));


  my.charts.packetGapChart = (function (m) {
    var margin = {
      top: 20,
      right: 20,
      bottom: 40,
      left: 75
    };

    var width = 960 - margin.left - margin.right;
    var height = 300 - margin.top - margin.bottom;
    var xScale = d3.scale.linear().range([0, width]);
    var yScale = d3.scale.linear().range([height, 0]);
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

    var svg = {}

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

      d3.select("#packetGapContainer").selectAll("svg").remove();

      svg = d3.select("#packetGapContainer")
            .append("svg");

      width = $("#packetGapContainer").width() - margin.left - margin.right;
      height = $("#packetGapContainer").height() - margin.top - margin.bottom;

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
        .x(function(d) { return xScale(d.timestamp); })
        .y(function(d) { return yScale(d.value); })
        .interpolate("basis");


      minMaxArea = d3.svg.area()
        .x (function (d) { return xScale(d.x) || 1; })
        .y0(function (d) { return yScale(d.y[0]) || 0; })
        .y1(function (d) { return yScale(d.y[1]) || 0; })
        .interpolate("basis");

      svg.attr("width", width + margin.left + margin.right)
         .attr("height", height + margin.top + margin.bottom);

      var graph = svg.append("g")
         .attr("transform", "translate(" + margin.left + "," + margin.top + ")")

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
         .datum(chartData.packetGapMeanNew)
         .attr("class", "line")
         .attr("d", line);

    };

    m.redraw = function() {

      width = $("#packetGapContainer").width() - margin.left - margin.right;
      height = $("#packetGapContainer").height() - margin.top - margin.bottom;

      /* Scale the range of the data again */
      xScale.domain(d3.extent(chartData.packetGapMeanNew, function(d) {
        return d.timestamp;
      }));

      yScale.domain([0, d3.max(chartData.packetGapMeanNew, function(d) {
        return d.value;
      })]);

      xGrid = function() {
        return d3.svg.axis()
          .scale(xScale)
           .orient("bottom")
           .tickSize(-height)
           .ticks(10)
           .tickFormat("")
      };

      yGrid = function() {
        return d3.svg.axis()
          .scale(yScale)
           .orient("left")
           .tickSize(-width)
           .ticks(5)
           .tickFormat("")
      };

      svg = d3.select("#packetGapContainer");
      svg.select(".line").attr("d", line(chartData.packetGapMeanNew));
      svg.select(".x.axis").call(xAxis);
      svg.select(".y.axis").call(yAxis);
      svg.select(".xGrid").call(xGrid());
      svg.select(".yGrid").call(yGrid());
      svg.select(".minMaxArea").attr("d", minMaxArea(chartData.packetGapMinMax));

    };


    return m;

  }({}));

  var resetChart = function() {
    var selectedSeriesOpt = $("#chopts_series option:selected").val();
    var selectedSeries = my.core.getSeriesByName(selectedSeriesOpt);

    clearChartData();

    my.charts.mainChart.reset(selectedSeries);
    my.charts.packetGapChart.reset(selectedSeries);

    my.charts.histogram = new CanvasJS.Chart("histogramContainer", {
      title: {text: "Distribution" },
      axisY: {
        title: "log(Count)",
        includeZero: "false"
      },
      axisX: {
        title: "Bin",
        includeZero: "false"
      },
      data: [{
        name: selectedSeries.name + "_hist",
        type: "column",
        dataPoints: chartData.histogram
      }]
    });
    my.charts.histogram.render();

    my.charts.basicStats = new CanvasJS.Chart("basicStatsContainer", {
      title: { text: "Basic Stats" },
      axisY: {
        includeZero: "false",
        title: selectedSeries.ylabel
      },
      data: [{
        name: selectedSeries.name + "_stats",
        type: "column",
        dataPoints: chartData.basicStats
      }]
    });
    my.charts.basicStats.render();

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
    //my.charts.histogram.render();
    //my.charts.basicStats.render();
    my.charts.mainChart.redraw();
    my.charts.packetGapChart.redraw();

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
