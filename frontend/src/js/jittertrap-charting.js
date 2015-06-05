/* jittertrap-charting.js */

/* global CanvasJS */
/* global JT:true */

JT = (function (my) {
  'use strict';

  var resetChart = function() {
    var selectedSeriesOpt = $("#chopts_series option:selected").val();
    var selectedSeries = my.core.series[selectedSeriesOpt];
    selectedSeries.filteredData.length = 0;

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
        dataPoints: selectedSeries.filteredData
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
        dataPoints: selectedSeries.histData
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
        dataPoints: selectedSeries.basicStats
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

    my.charts.params.redrawPeriod = my.charts.params.plotPeriod / 2;

    if (my.charts.params.redrawPeriod < my.charts.params.redrawPeriodMin) {
      my.charts.params.redrawPeriod = my.charts.params.redrawPeriodMin;
    } else if (my.charts.params.redrawPeriod > my.charts.params.redrawPeriodMax) {
      my.charts.params.redrawPeriod = my.charts.params.redrawPeriodMax;
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
    var d2 = Date.now();
    renderCount++;
    renderTime += d2 - d1;
    tuneChartUpdatePeriod();
  };

  var drawIntervalID = setInterval(renderGraphs, my.charts.params.redrawPeriod);

  var setUpdatePeriod = function() {
    var updateRate = 1000.0 / my.charts.params.redrawPeriod; /* Hz */
    clearInterval(drawIntervalID);
    drawIntervalID = setInterval(renderGraphs, my.charts.params.redrawPeriod);
  };

  var toggleStopStartGraph = function() {
    var maxUpdatePeriod = 9999999999;
    if (my.charts.params.redrawPeriod !== maxUpdatePeriod) {
      my.charts.params.redrawPeriodSaved = my.charts.params.redrawPeriod;
      my.charts.params.redrawPeriod = maxUpdatePeriod;
    } else {
      my.charts.params.redrawPeriod = my.charts.params.redrawPeriodSaved;
    }
    setUpdatePeriod();
    return false;
  };

  var setChartPeriod = function (newPeriod) {
    if (newPeriod < JT.charts.params.plotPeriodMin) {
       newPeriod = JT.charts.params.plotPeriodMin;
    } else if (newPeriod > JT.charts.params.plotPeriodMax) {
       newPeriod = JT.charts.params.plotPeriodMax;
    }

    var sampleCount = JT.core.sampleCount(newPeriod);
    JT.core.resizeDataBufs(sampleCount);
    JT.charts.resetChart();

    return {newPeriod: newPeriod, sampleCount: sampleCount};
  };

  /* Export "public" functions */
  my.charts.toggleStopStartGraph = toggleStopStartGraph;
  my.charts.setUpdatePeriod = setUpdatePeriod;
  my.charts.setChartPeriod = setChartPeriod;
  my.charts.resetChart = resetChart;

  return my;
}(JT));
/* End of jittertrap-charting.js */
