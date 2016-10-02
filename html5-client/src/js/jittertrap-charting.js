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

  var clearChartData = function () {
    my.charts.tput.getTputRef().length = 0;
    my.charts.pgaps.getMinMaxRef().length = 0;
    my.charts.pgaps.getMeanRef().length = 0;
  };

  /* must return a reference to an array of {x:x, y:y} */
  my.charts.getMainChartRef = function () {
    return my.charts.tput.getTputRef();
  };

  my.charts.getPacketGapMeanRef = function () {
    return my.charts.pgaps.getMeanRef();
  };

  my.charts.getPacketGapMinMaxRef = function () {
    return my.charts.pgaps.getMinMaxRef();
  };

  my.charts.getTopFlowsRef = function () {
    return my.charts.toptalk.getDataRef();
  };

  var resetChart = function() {
    var selectedSeriesOpt = $("#chopts_series option:selected").val();
    my.core.setSelectedSeriesName(selectedSeriesOpt);
    clearChartData();
    my.charts.tput.tputChart.reset(my.core.getSelectedSeries());
    my.charts.pgaps.packetGapChart.reset();
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
    my.charts.tput.tputChart.redraw();
    my.charts.pgaps.packetGapChart.redraw();
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
