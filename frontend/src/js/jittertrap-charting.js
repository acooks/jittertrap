var chartData = {};

var series = function(title, ylabel) {
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

chartData.txDelta = new series("Tx Bytes per sample period",
                               "Tx Bytes per sample");

chartData.rxDelta = new series("Rx Bytes per sample period",
                               "Rx Bytes per sample");

chartData.rxRate = new series("Ingress throughput in kbps",
                              "kbps, mean");

chartData.txRate = new series("Egress throughput in kbps",
                              "kbps, mean");

chartData.txPacketRate = new series("Egress packet rate",
                                    "pkts per sec, mean");

chartData.rxPacketRate = new series("Ingress packet rate",
                                    "pkts per sec, mean");

chartData.txPacketDelta = new series("Egress packets per sample",
                                     "packets sent");

chartData.rxPacketDelta = new series("Ingress packets per sample",
                                     "packets received");

var resetChart = function() {
  var s = $("#chopts_series option:selected").val();
  chartData[s].filteredData.length = 0;
  chart = new CanvasJS.Chart("chartContainer", {
    height: 300,
    animationEnabled: false,
    exportEnabled: false,
    toolTip:{
      enabled: true,
    },
    interactivityEnabled: true,
    zoomEnabled: "true",
    panEnabled: "true",
    title: { text: chartData[s].title },
    axisY: {
      title: chartData[s].ylabel,
      includeZero: "false",
    },
    axisX: { title: chartData[s].xlabel },
    data: [{
      name: s,
      type: "line",
      dataPoints: chartData[s].filteredData
    }]
  });
  chart.render();

  histogram = new CanvasJS.Chart("histogramContainer", {
    title: {text: "Distribution" },
    axisY: {
      title: "log(Count)",
      includeZero: "false",
    },
    axisX: {
      title: "Bin",
      includeZero: "false",
    },
    data: [{
      name: s + "_hist",
      type: "column",
      dataPoints: chartData[s].histData
    }]
  });
  histogram.render();

  basicStatsGraph = new CanvasJS.Chart("basicStatsContainer", {
    title: { text: "Basic Stats" },
    axisY: {
      includeZero: "false",
      title: chartData[s].ylabel
    },
    data: [{
      name: s + "_stats",
      type: "column",
      dataPoints: chartData[s].basicStats
    }]
  });
  basicStatsGraph.render();
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
  resizeCBuf(chartData.txDelta, newlen);
  resizeCBuf(chartData.rxDelta, newlen);

  resizeCBuf(chartData.rxRate, newlen);
  resizeCBuf(chartData.txRate, newlen);

  resizeCBuf(chartData.txPacketRate, newlen);
  resizeCBuf(chartData.rxPacketRate, newlen);

  resizeCBuf(chartData.txPacketDelta, newlen);
  resizeCBuf(chartData.rxPacketDelta, newlen);
};

var clearChart = function() {

  var clearSeries = function (s) {
    s.data = new CBuffer(dataLength);
    s.filteredData = [];
    s.histData = [];
  };

  clearSeries(chartData.txDelta);
  clearSeries(chartData.rxDelta);
  clearSeries(chartData.txRate);
  clearSeries(chartData.rxRate);
  clearSeries(chartData.txPacketRate);
  clearSeries(chartData.rxPacketRate);
  clearSeries(chartData.txPacketDelta);
  clearSeries(chartData.rxPacketDelta);

  resetChart();
  xVal = 0;
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

  updatePeriod = chartingPeriod / 2;

  if (updatePeriod < updatePeriodMin) {
    updatePeriod = updatePeriodMin;
  } else if (updatePeriod > updatePeriodMax) {
    updatePeriod = updatePeriodMax;
  }

  setUpdatePeriod();

  renderCount = renderCount % tuneWindowSize;
  renderTime = 0;
};

var renderGraphs = function() {
  var d1 = Date.now();
  histogram.render();
  basicStatsGraph.render();
  chart.render();
  var d2 = Date.now();
  renderCount++;
  renderTime += d2 - d1;
  tuneChartUpdatePeriod();

};

var drawIntervalID = setInterval(renderGraphs, updatePeriod);

var setUpdatePeriod = function() {
  var updateRate = 1000.0 / updatePeriod; /* Hz */
  clearInterval(drawIntervalID);
  drawIntervalID = setInterval(renderGraphs, updatePeriod);
  //console.log("chart updateRate: " + updateRate + "Hz. period: "+ updatePeriod + "ms");
};

var toggleStopStartGraph = function() {
  var maxUpdatePeriod = 9999999999;
  if (updatePeriod !== maxUpdatePeriod) {
    old_updatePeriod = updatePeriod;
    updatePeriod = maxUpdatePeriod;
  } else {
    updatePeriod = old_updatePeriod;
  }
  setUpdatePeriod();
  return false;
};        


