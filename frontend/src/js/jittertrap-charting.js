var chartData = {};

chartData.txDelta = {
  data:[],  // raw samples
  filteredData:[], // filtered & decimated to chartingPeriod
  histData:[],
  title:"Tx Bytes per sample period",
  ylabel:"Tx Bytes per sample",
  xlabel:"Time",
  minY: {x: 0, y: 0},
  maxY: {x: 0, y: 0},
  basicStats:[]
};

chartData.rxDelta = {
  data:[],
  filteredData:[], // filtered & decimated to chartingPeriod
  histData:[],
  title:"Rx Bytes per sample period",
  ylabel:"Rx Bytes per sample",
  xlabel:"Time",
  minY: {x: 0, y: 0},
  maxY: {x: 0, y: 0},
  basicStats:[]
};

chartData.rxRate = {
  data:[],
  filteredData:[], // filtered & decimated to chartingPeriod
  histData:[],
  title: "Ingress throughput in kbps",
  ylabel:"kbps, mean",
  xlabel:"sample number",
  minY: {x: 0, y: 0},
  maxY: {x: 0, y: 0},
  basicStats:[]
};

chartData.txRate = {
  data:[],
  filteredData:[], // filtered & decimated to chartingPeriod
  histData:[],
  title: "Egress throughput in kbps",
  ylabel:"kbps, mean",
  xlabel:"sample number",
  minY: {x: 0, y: 0},
  maxY: {x: 0, y: 0},
  basicStats:[]
};

chartData.txPacketRate = {
  data:[],
  filteredData:[], // filtered & decimated to chartingPeriod
  histData:[],
  title: "Egress packet rate",
  ylabel:"pkts per sec, mean",
  xlabel:"time",
  minY: {x: 0, y: 0},
  maxY: {x: 0, y: 0},
  basicStats:[]
};

chartData.rxPacketRate = {
  data:[],
  filteredData:[], // filtered & decimated to chartingPeriod
  histData:[],
  title: "Ingress packet rate",
  ylabel:"pkts per sec, mean",
  xlabel:"time",
  minY: {x: 0, y: 0},
  maxY: {x: 0, y: 0},
  basicStats:[]
};

chartData.txPacketDelta = {
  data:[],
  filteredData:[], // filtered & decimated to chartingPeriod
  histData:[],
  title: "Egress packets per sample",
  ylabel:"packets sent",
  xlabel:"sample number",
  minY: {x: 0, y: 0},
  maxY: {x: 0, y: 0},
  basicStats:[]
};

chartData.rxPacketDelta = {
  data:[],
  filteredData:[], // filtered & decimated to chartingPeriod
  histData:[],
  title: "Ingress packets per sample",
  ylabel:"packets received",
  xlabel:"sample number",
  minY: {x: 0, y: 0},
  maxY: {x: 0, y: 0},
  basicStats:[]
};

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

  if (updatePeriod < updatePeriodMin) updatePeriod = updatePeriodMin;
  else if (updatePeriod > updatePeriodMax) updatePeriod = updatePeriodMax;

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
  if (updatePeriod != maxUpdatePeriod) {
    old_updatePeriod = updatePeriod;
    updatePeriod = maxUpdatePeriod;
  } else {
    updatePeriod = old_updatePeriod;
  }
  setUpdatePeriod();
  return false;
};        


