/* jittertrap-charting.js */

/* global CanvasJS */
/* global CBuffer */
/* global JT:true */

JT = (function (my) {
  'use strict';
  my.charts.series = {};

  /* short, local alias */
  var series = my.charts.series;

  /*
   * series is a prototype object that encapsulates chart data.
   */
  var Series = function(name, title, ylabel) {
    this.name = name;
    this.title = title;
    this.ylabel = ylabel;
    this.xlabel = "Time (ms)";
  this.data = []; // raw samples
    this.filteredData = []; // filtered & decimated to chartingPeriod
    this.histData = [];
    this.minY = {x:0, y:0};
    this.maxY = {x:0, y:0};
    this.basicStats = [];
  };

  series.txDelta = new Series("txDelta",
                              "Tx Bytes per sample period",
                              "Tx Bytes per sample");

  series.rxDelta = new Series("rxDelta",
                              "Rx Bytes per sample period",
                              "Rx Bytes per sample");

  series.rxRate = new Series("rxRate",
                             "Ingress throughput in kbps",
                             "kbps, mean");

  series.txRate = new Series("txRate",
                             "Egress throughput in kbps",
                             "kbps, mean");

  series.txPacketRate = new Series("txPacketRate",
                                   "Egress packet rate",
                                   "pkts per sec, mean");

  series.rxPacketRate = new Series("rxPacketRate",
                                   "Ingress packet rate",
                                   "pkts per sec, mean");

  series.txPacketDelta = new Series("txPacketDelta",
                                    "Egress packets per sample",
                                    "packets sent");

  series.rxPacketDelta = new Series("rxPacketDelta",
                                    "Ingress packets per sample",
                                    "packets received");

  var resetChart = function() {
    var selectedSeriesOpt = $("#chopts_series option:selected").val();
    var selectedSeries = my.charts.series[selectedSeriesOpt];
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

  var resizeCBuf = function(cbuf, len) {
    cbuf.filteredData = [];
    var b = new CBuffer(len);

    var l = (len < cbuf.data.size) ? len : cbuf.data.size;
    while (l--) {
      b.push(cbuf.data.shift());
    }
    cbuf.data = b;
  };

  var resizeDataBufs = function(newlen) {

    /* local alias for brevity */
    var s = my.charts.series;

    resizeCBuf(s.txDelta, newlen);
    resizeCBuf(s.rxDelta, newlen);

    resizeCBuf(s.rxRate, newlen);
    resizeCBuf(s.txRate, newlen);

    resizeCBuf(s.txPacketRate, newlen);
    resizeCBuf(s.rxPacketRate, newlen);

    resizeCBuf(s.txPacketDelta, newlen);
    resizeCBuf(s.rxPacketDelta, newlen);
  };

  var clearChart = function() {

    var clearSeries = function (s) {
      s.data = new CBuffer(my.rawData.dataLength);
      s.filteredData = [];
      s.histData = [];
    };

    var s = my.charts.series; /* local alias for brevity */

    clearSeries(s.txDelta);
    clearSeries(s.rxDelta);
    clearSeries(s.txRate);
    clearSeries(s.rxRate);
    clearSeries(s.txPacketRate);
    clearSeries(s.rxPacketRate);
    clearSeries(s.txPacketDelta);
    clearSeries(s.rxPacketDelta);

    resetChart();
    my.rawData.xVal = 0;
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

  /* Export "public" functions */
  my.charts.toggleStopStartGraph = toggleStopStartGraph;
  my.charts.setUpdatePeriod = setUpdatePeriod;
  my.charts.clearChart = clearChart;
  my.charts.resizeDataBufs = resizeDataBufs;
  my.charts.resetChart = resetChart;

  return my;
}(JT));
/* End of jittertrap-charting.js */
