/* jittertrap-charting.js */

/* global d3 */
/* global JT:true */

((my) => {
  'use strict';

  my.charts = {};

  /* Add a container for charting parameters */
  const params = {};

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

  const clearChartData = function () {
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
    return function () {
      const container = d3.select(containerId);
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

  const resetChart = function () {
    const selectedSeriesOpt = $("#chopts_series option:selected").val();
    my.core.setSelectedSeriesName(selectedSeriesOpt);
    clearChartData();
    my.charts.tput.tputChart.reset(my.core.getSelectedSeries());
    my.charts.pgaps.packetGapChart.reset();
    my.charts.toptalk.toptalkChart.reset();
  };

  let renderCount = 0;
  let renderTime = 0;

  const renderGraphs = function() {
    /* Only redraw if the data has changed */
    if (!isDirty) {
      return;
    }

    my.charts.tput.tputChart.redraw();
    my.charts.pgaps.packetGapChart.redraw();
    my.charts.toptalk.toptalkChart.redraw();

    isDirty = false; // Clear the dirty flag after drawing
    const tuneWindowSize = 5; // how often to adjust the updatePeriod.

    params.redrawPeriod = params.plotPeriod / 2;

    if (params.redrawPeriod < params.redrawPeriodMin) {
      params.redrawPeriod = params.redrawPeriodMin;
    } else if (params.redrawPeriod > params.redrawPeriodMax) {
      params.redrawPeriod = params.redrawPeriodMax;
    }

    renderTime = 0;
  };

  let drawIntervalID;

  const animationLoop = function() {
    renderGraphs();
    drawIntervalID = requestAnimationFrame(animationLoop);
  }

  my.charts.setDirty = function () {
    isDirty = true;
  };

  const getChartPeriod = function () {
    return params.plotPeriod;
  };


  const toggleStopStartGraph = function() {
    if (drawIntervalID) {
      cancelAnimationFrame(drawIntervalID);
      drawIntervalID = 0;
    } else {
      drawIntervalID = requestAnimationFrame(animationLoop);
    }
  };

  const setChartPeriod = function (newPeriod) {
    if (newPeriod < params.plotPeriodMin) {
       newPeriod = params.plotPeriodMin;
    } else if (newPeriod > params.plotPeriodMax) {
       newPeriod = params.plotPeriodMax;
    }

    params.plotPeriod = newPeriod;
    const sampleCount = JT.core.sampleCount(newPeriod);
    JT.core.resizeDataBufs(sampleCount);
    JT.charts.resetChart();

    return {newPeriod: newPeriod, sampleCount: sampleCount};
  };

  /* Initialize all charts and start the render loop */
  my.charts.init = function () {
    resetChart();
    drawIntervalID = requestAnimationFrame(animationLoop);
  };

  /* Export "public" functions */
  my.charts.toggleStopStartGraph = toggleStopStartGraph;
  my.charts.getChartPeriod = getChartPeriod;
  my.charts.setChartPeriod = setChartPeriod;
  my.charts.resetChart = resetChart;

})(JT);
/* End of jittertrap-charting.js */
