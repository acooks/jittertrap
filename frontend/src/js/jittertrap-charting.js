/* jittertrap-charting.js */

/* global CanvasJS */
/* global JT:true */

JT = (function (my) {
  'use strict';

  my.charts = {};

  /* Add a container for charting parameters */
  var params = {};

  /* time (milliseconds) represented by each point on the chart */
  params.plotPeriod        = 60;
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
    packetGapMinMax: [],
  };

  var clearChartData = function () {
    chartData.mainChart.length = 0;
    chartData.histogram.length = 0;
    chartData.basicStats.length = 0;
    chartData.packetGapMean.length = 0;
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

  my.charts.getPacketGapMinMaxRef = function () {
    return chartData.packetGapMinMax;
  };

  var resetChart = function() {
    var selectedSeriesOpt = $("#chopts_series option:selected").val();
    var selectedSeries = my.core.getSeriesByName(selectedSeriesOpt);

    clearChartData();

    my.charts.mainChart = new CanvasJS.Chart("chartContainer", {
      height: 300,
      animationEnabled: false,
      exportEnabled: false,
      toolTip:{
        enabled: true
      },
      interactivityEnabled: true,
      zoomEnabled: "true",
      panEnabled: "true",
      title: { text: selectedSeries.title },
      axisY: {
        title: selectedSeries.ylabel,
        includeZero: "false"
      },
      axisX: { title: selectedSeries.xlabel },
      data: [{
        name: selectedSeries.name,
        type: "line",
        dataPoints: chartData.mainChart
      }]
    });
    my.charts.mainChart.render();

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

    my.charts.packetGap = new CanvasJS.Chart("packetGapContainer", {
      title: { text: "Inter Packet Gap" },
      axisY: {
        includeZero: "false",
        title: "Packet Gap (ms, mean)"
      },
      data: [
        {
          showInLegend: true,
          name: "range(min,max)",
          type: "rangeArea",
          dataPoints: chartData.packetGapMinMax
        },
        {
          showInLegend: true,
          name: "mean",
          type: "line",
          dataPoints: chartData.packetGapMean
        }
      ]
    });
    my.charts.packetGap.render();

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
    my.charts.histogram.render();
    my.charts.basicStats.render();
    my.charts.mainChart.render();
    my.charts.packetGap.render();
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
