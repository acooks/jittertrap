/* jittertrap-charting.js */

/* global d3 */
/* global JT:true */

JT = (function (my) {
  'use strict';

  my.charts = {};

  /* Add a container for charting parameters */
  var params = {};

  /* Dirty flag signals when a redraw is needed. */
  var isDirty = true;

  /* time (milliseconds) represented by each point on the chart */
  params.plotPeriod        = 100;
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

  /* newSize is an OUT parameter */
  my.charts.resizeChart = function(containerId, newSize) {
    return function() {
      var container = d3.select(containerId);
      var new_width = container.node().getBoundingClientRect().width;
      var new_height = container.node().getBoundingClientRect().height;
      if (new_width === 0 ) {
        return;
      }
      newSize.width = new_width;
      newSize.height = new_height;
      container.attr("width", new_width)
               .attr("height", new_height);
      container.select("svg")
               .attr("width", new_width)
               .attr("height", new_height);
    };
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

  var renderGraphs = function() {
    /* Only redraw if the data has changed */
    if (!isDirty) {
      return;
    }

    my.charts.tput.tputChart.redraw();
    my.charts.pgaps.packetGapChart.redraw();
    my.charts.toptalk.toptalkChart.redraw();

    isDirty = false; // Clear the dirty flag after drawing
    var tuneWindowSize = 5; // how often to adjust the updatePeriod.

    params.redrawPeriod = params.plotPeriod / 2;

    if (params.redrawPeriod < params.redrawPeriodMin) {
      params.redrawPeriod = params.redrawPeriodMin;
    } else if (params.redrawPeriod > params.redrawPeriodMax) {
      params.redrawPeriod = params.redrawPeriodMax;
    }

    renderTime = 0;
  };

  var drawIntervalID;

  var animationLoop = function() {
    renderGraphs();
    drawIntervalID = requestAnimationFrame(animationLoop);
  }

  my.charts.setDirty = function() {
    isDirty = true;
  };

  var getChartPeriod = function () {
    return params.plotPeriod;
  };


  var toggleStopStartGraph = function() {
    if (drawIntervalID) {
      cancelAnimationFrame(drawIntervalID);
      drawIntervalID = 0;
    } else {
      drawIntervalID = requestAnimationFrame(animationLoop);
    }
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

  /* Initialize all charts and start the render loop */
  my.charts.init = function() {
    resetChart();
    drawIntervalID = requestAnimationFrame(animationLoop);
  };

  /* Export "public" functions */
  my.charts.toggleStopStartGraph = toggleStopStartGraph;
  my.charts.getChartPeriod = getChartPeriod;
  my.charts.setChartPeriod = setChartPeriod;
  my.charts.resetChart = resetChart;

  return my;
}(JT));
/* End of jittertrap-charting.js */
