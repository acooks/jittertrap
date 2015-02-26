var chartData = {};

chartData.txDelta = {
  data:[],
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
  chart = new CanvasJS.Chart("chartContainer", {
    axisY:{
      includeZero: "false",
    },
    zoomEnabled: "true",
    panEnabled: "true",
    title: { text: chartData[s].title },
    axisY: { title: chartData[s].ylabel },
    axisX: { title: chartData[s].xlabel },
    data: [{
      name: s,
      type: "line",
      dataPoints: chartData[s].data
    }]
  });
  chart.render();

  histogram = new CanvasJS.Chart("histogramContainer", {
    title: {text: "Distribution" },
    axisY: {
      title: "Count",
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


var clearChart = function() {
  chartData.txDelta.data = [];
  chartData.txDelta.histData = [];

  chartData.rxDelta.data = [];
  chartData.rxDelta.histData = [];

  chartData.rxRate.data = [];
  chartData.rxRate.histData = [];

  chartData.txRate.data = [];
  chartData.txRate.histData = [];

  chartData.txPacketRate.data = [];
  chartData.txPacketRate.histData = [];

  chartData.rxPacketRate.data = [];
  chartData.rxPacketRate.histData = [];

  chartData.rxPacketDelta.data = [];
  chartData.rxPacketDelta.histData = [];

  chartData.txPacketDelta.data = [];
  chartData.txPacketDelta.histData = [];
  resetChart();
  xVal = 0;
};

var checkTriggers = function() {
  ;
};

var renderGraphs = function() {
  histogram.render();
  basicStatsGraph.render();
  chart.render();
};

function getRenderInterval(updatePeriod) {
  var interval = setInterval(renderGraphs, updatePeriod);
  return interval;
}

var drawInterval = getRenderInterval(updatePeriod);

var toggleStopStartGraph = function() {
  var maxUpdatePeriod = 9999999999;
  if (updatePeriod  != maxUpdatePeriod) {
    old_updatePeriod = updatePeriod;
    updatePeriod = maxUpdatePeriod;
  } else {
    updatePeriod = old_updatePeriod;
  }
  setUpdatePeriod();
  return false;
};        

var setUpdatePeriod = function() {
  var sampleRate = millisecondsToRate(samplePeriod);
  var updateRate = millisecondsToRate(updatePeriod);
  if (sampleRate < updateRate) {
    updatePeriod = rateToMilliseconds(sampleRate);
    $("#chopts_refresh").val(sampleRate);
  } else if (updateRate > 30) {
    updatePeriod = rateToMilliseconds(30);
    $("#chopts_refresh").val(30);
  } else {
    $("#chopts_refresh").val(updateRate);
  }
  clearInterval(drawInterval);
  drawInterval = getRenderInterval(updatePeriod);
  console.log("updateRate: " + updateRate + " sampleRate: " + sampleRate);
};
