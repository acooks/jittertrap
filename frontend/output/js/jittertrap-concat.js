var xVal = 0;

/**
 * chart updates; milliseconds; 40ms == 25 Hz
 */
var updatePeriod = 60;
var updatePeriodMin = 40;
var updatePeriodMax = 100;
var old_updatePeriod; // used for pausing/resuming
/**
 * data samples; microseconds; fixed.
 */
var samplePeriod = 1000;

/**
 * time (milliseconds) represented by each data point
 */
var chartingPeriod = 60;
var chartingPeriodMin = 1;
var chartingPeriodMax = 500;

/*
 *
 */
var dataLengthMultiplier = 300;

/*
 * number of raw data samples.
 * dataLength = chartingPeriod * dataLengthMultiplier
 */
var dataLength = 18000;

/*
 * list of active traps
 */
var traps = {};


var websocket = {};
var chart = {};
var histogram = {};
var basicStatsGraph = {};
/* count must be bytes, samplePeriod is microseconds */
var byteCountToKbpsRate = function(count) {
  'use strict';
  var rate = count / samplePeriod * 8000.0;
  return rate;
};

var packetDeltaToRate = function(count) {
  'use strict';
  return count * (1000000.0 / samplePeriod);
};

var updateStats = function (series) {
  'use strict';

  if (! series.filteredData || series.filteredData.length === 0) {
    return;
  }

  var sortedData = series.filteredData.slice(0);
  sortedData.sort(function(a,b) {return (a.y - b.y);});

  var maxY = sortedData[sortedData.length-1].y;
  var minY = sortedData[0].y;
  var median = sortedData[Math.floor(sortedData.length / 2.0)].y;
  var mean = 0;
  var sum = 0;
  var i = 0;

  for (i = sortedData.length-1; i >=0; i--) {
    sum += sortedData[i].y;
  }
  mean = sum / sortedData.length;

  if (series.basicStats[0]) {
    series.basicStats[0].y = minY;
    series.basicStats[1].y = median;
    series.basicStats[2].y = mean;
    series.basicStats[3].y = maxY;
  } else {
    series.basicStats.push({x:1, y:minY, label:"Min"});
    series.basicStats.push({x:2, y:median, label:"Median"});
    series.basicStats.push({x:3, y:mean, label:"Mean"});
    series.basicStats.push({x:4, y:maxY, label:"Max"});
  }
};

var updateHistogram = function(series) {
  var binCnt = 25;
  var normBins = new Float32Array(binCnt);

  var sortedData = series.data.slice(0);
  sortedData.sort();

  var maxY = sortedData[sortedData.length-1];
  var minY = sortedData[0];
  var range = (maxY - minY) * 1.1;

  /* bins must use integer indexes, so we have to normalise the
    * data and then convert it back before display.
    * [0,1) falls into bin[0] */
  var i = 0;
  var j = 0;

  /* initialise the bins */
  for (; i < binCnt; i++) {
    normBins[i] = 0;
  }
  series.histData.length = 0;

  /* bin the normalized data */
  for (j = 0; j < series.data.size; j++) {
    var normY = (series.data.get(j) - minY) / range * binCnt;
    normBins[Math.round(normY)]++;
  }

  /* convert to logarithmic scale */
  for (i = 0; i < normBins.length; i++) {
    if (normBins[i] > 0) {
      normBins[i] = Math.log(normBins[i]);
    }
  }

  /* write the histogram x,y data */
  for (i = 0; i < binCnt; i++) {
    var xVal = Math.round(i * (maxY / binCnt));
    xVal += Math.round(minY);  /* shift x to match original y range */
    series.histData.push({x: xVal, y: normBins[i], label: xVal});
  }

};

var updateFilteredSeries = function (series) {

  /* FIXME: float vs integer is important here! */
  var decimationFactor = Math.floor(chartingPeriod / (samplePeriod / 1000.0));
  var fseriesLength = Math.floor(series.data.size / decimationFactor);

  // the downsampled data has to be scaled.
  var scale = 1/chartingPeriod;

  // how many filtered data points have been collected already?
  var filteredDataCount = series.filteredData.length;

  // if there isn't enough data for one filtered sample, return.
  if (fseriesLength === 0) {
    return;
  }

  // if the series is complete, expire the first value.
  if (filteredDataCount === fseriesLength) {
    series.filteredData.shift();
    filteredDataCount--;
  }

  // all the X values will be updated, but save the Y values.
  var filteredY = new Float32Array(fseriesLength);
  for (var i = filteredDataCount - 1; i >= 0; i--) {
    filteredY[i] = series.filteredData[i].y;
  }

  // now, discard all previous values, because all the X values will change.
  series.filteredData.length = 0;

  // calculate any/all missing Y values from raw data
  for (i = filteredDataCount; i < fseriesLength; i++) {
    filteredY[i] = 0.0;
    for (var j = 0; j < decimationFactor; j++) {
      var idx = i * decimationFactor + j;
      if (idx >= series.data.size) {
        break;
      }
      filteredY[i] += series.data.get(idx);
    }

    // scale the value to the correct range.
    filteredY[i] *= scale;
  }

  // finally, update the filteredData
  for (i = 0; i < fseriesLength; i++) {
    series.filteredData.push({x: i * chartingPeriod, y: filteredY[i]});
  }

};

var updateSeries = function (series, xVal, yVal, selectedSeries) {
  series.data.push(yVal);

  /* do expensive operations once per filtered sample/chartingPeriod. */
  if ((xVal % chartingPeriod === 0) && (series === selectedSeries)) {
    updateStats(series);
    updateHistogram(series);
    updateFilteredSeries(series);
  }
};
/**
 * Websocket Callback Functions
 * i.e. Referred to in websocket.onmessage
 */

var handleMsgUpdateStats = function (samplePeriod, stats, seriesName) {
   var selectedSeries = chartData[seriesName];
   var len = stats.length;
   for (var i = 0; i < len; i++) {
     var d = stats[i];
     updateSeries(chartData.txDelta, xVal, d.txDelta, selectedSeries);
     updateSeries(chartData.rxDelta, xVal, d.rxDelta, selectedSeries);
     updateSeries(chartData.txRate, xVal, byteCountToKbpsRate(d.txDelta), selectedSeries);
     updateSeries(chartData.rxRate, xVal, byteCountToKbpsRate(d.rxDelta), selectedSeries);
     updateSeries(chartData.txPacketRate, xVal, packetDeltaToRate(d.txPktDelta), selectedSeries);
     updateSeries(chartData.rxPacketRate, xVal, packetDeltaToRate(d.rxPktDelta), selectedSeries);
     updateSeries(chartData.txPacketDelta, xVal, d.txPktDelta, selectedSeries);
     updateSeries(chartData.rxPacketDelta, xVal, d.rxPktDelta, selectedSeries);
     xVal++;
     xVal = xVal % dataLength;
  }

  checkTriggers();

};

var handleMsgIfaces = function(ifaces) {
  $('#dev_select').empty();
  $.each(ifaces,
    function (ix, val) {
      var option = $('<option>').text(val).val(val);
      $('#dev_select').append(option);
    }
  );
  dev_select();
};

var handleMsgNetemParams = function(params) {
  if (params.delay === -1 && params.jitter === -1 && params.loss === -1) {
    $("#netem_status").html("No active impairment on device. Set parameters to activate.");
    $("#delay").val("0");
    $("#jitter").val("0");
    $("#loss").val("0");
  } else {
    $("#netem_status").html("Ready");
    $("#delay").val(params.delay);
    $("#jitter").val(params.jitter);
    $("#loss").val(params.loss);
  }
};

var handleMsgSamplePeriod = function(period) {
  samplePeriod = period;
  $("#sample_period").html(period / 1000.0 + "ms");
  console.log("sample_period: " + period);
  setUpdatePeriod();
  clearChart();
};


/**
 * Websocket Sending Functions
 */
var list_ifaces = function() {
  var msg = JSON.stringify({'msg':'list_ifaces'});
  websocket.send(msg);
};

var dev_select = function() {
  var msg = JSON.stringify({'msg':'dev_select',
                            'dev': $("#dev_select").val()});
  websocket.send(msg);
  get_netem();
};

var get_netem = function() {
  var msg = JSON.stringify(
    {'msg': 'get_netem', 
      'dev': $("#dev_select").val()
    });
  websocket.send(msg);
};

var set_netem = function() {
  var msg = JSON.stringify(
    {'msg': 'set_netem',
      'dev': $("#dev_select").val(),
      'delay': $("#delay").val(),
      'jitter': $("#jitter").val(),
      'loss': $("#loss").val()
    });
  websocket.send(msg);
  return false;
};

var clear_netem = function() {
  $("#delay").val(0);
  $("#jitter").val(0);
  $("#loss").val(0);
  set_netem();
  return false;
};

var get_sample_period = function() {
  var msg = JSON.stringify({'msg': 'get_sample_period'});
  websocket.send(msg);
};

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


// Trap Checking Functions
/**
 * Returns the data to be used in checking to see if a particualar trap has been triggered.
 * The data is an array of objects {x,y} containing x,y values for the chart.
 */
var trapData = function(trapId) {
  var data = null;
  switch (trapId) {
    case 'max_rx_bitrate':
      data = chartData.rxRate.data;
    break;
    case 'max_tx_bitrate':
      data = chartData.txRate.data;
    break;
  }
  return data;
};
/**
 * Returns true if the given trap has been triggered
 */
var trapTriggered = function(trapId, trapVal) {
  var triggered = false,
      data      = trapData(trapId);

  switch (trapId) {
    case 'max_rx_bitrate':
    case 'max_tx_bitrate':
      if (data[data.length-1].y > trapVal) {
        triggered = true;
      }
    break;
  }
  return triggered;
};
var checkTriggers = function() {
  $.each(traps, function(trapId, trapVal){
    if (trapTriggered(trapId, trapVal)) {
      console.log("Trap Triggered: " + trapId + "/" + trapVal);
    }
  });
};


// Functions for the alternative approach to controlling traps
/**
 * 
 */
var closeAddTrapModal = function() {
  $('#add_trap_modal input').val("");
  $('#add_trap_modal button').get(1).click();
};

/**
 * Handler for selecting a trap in the modal for adding traps
 * Just needs to ensure the trap measurement units are displayed
 */
var trapSelectionHandler = function(event){
  var $input_group_addon = $(event.target).parent().find('.input-group-addon'),
      units              = $(event.target).find('option:selected').data('trapUnits');

  // Update the input-group-addon with the correct units for the type of trap selected
  $input_group_addon.text(units);
};

/**
 * 
 */
var addTrapToUI = function(){
  var trapValue        = $('#trap_value').val(),
      trapValueInt     = parseInt(trapValue),
      trapIdSelected   = $('#trap_names option:selected').data('trapId'),
      trapNameSelected = $('#trap_names option:selected').text(),
      $trapTable       = $('#traps_table'),
      trapUnits        = $('#trap_names option:selected').data('trapUnits');

  // Validity/Verification checks first
  if ((! isNaN(trapValueInt)) && (trapValueInt > 0)) {
    // Add the trap to the traps table
    $.get('/templates/trap.html', function(template) {
      var template_data = { trapId: trapIdSelected, trapName: trapNameSelected, trapValue: trapValueInt, trapUnits: trapUnits },
          rendered      = Mustache.render(template, template_data);

      $trapTable.find('tbody').append(rendered);
    });

    closeAddTrapModal();
  }
};

var addTrapHandler = function(event) {
  var $selectedTrapOption = $(event.target).parents('.modal').find('option:selected'),
      trapId              = $selectedTrapOption.data('trapId'),
      trapValue           = $('#trap_value').val(),
      trapValueInt        = parseInt(trapValue);

  if (trapValueInt > 0) {
    traps[trapId] = trapValue;
    addTrapToUI();
  }
};
$(document).ready(function() {

  var triggers = {
    maxRxThroughputEnabled: false,
    maxTxThroughputEnabled: false,
    minRxThroughputEnabled: false,
    minTxThroughputEnabled: false,
    maxTxSilenceEnabled:    false,
    maxRxSilenceEnabled:    false,
    maxRxThroughputTriggerVal: 0,
    maxTxThroughputTriggerVal: 0,
    minRxThroughputTriggerVal: 0,
    minTxThroughputTriggerVal: 0
  };

  // Initialize Chart Options
  $("#chopts_dataLen").html(dataLength);
  $("#chopts_chartPeriod").val(chartingPeriod);

  // Initialize WebSockets
  var wsUri = "ws://" + document.domain + ":" + location.port;
  websocket = new WebSocket(wsUri);

  websocket.onopen = function(evt) { 
    websocket.send("open!");
    list_ifaces();
    get_sample_period();
  };

  websocket.onclose = function(evt) {
    console.log("unhandled websocket onclose event: " + evt);
  };

  websocket.onerror = function(evt) {
    console.log("unhandled websocket onerror event: " + evt);
  };

  websocket.onmessage = function(evt) {
    var msg = JSON.parse(evt.data);
    var selectedIface = $('#dev_select').val();

    if (msg.stats && msg.stats.iface === selectedIface) {
      var visibleSeries = $("#chopts_series option:selected").val();
      handleMsgUpdateStats(samplePeriod, msg.stats.s, visibleSeries);
    } else if (msg.ifaces) {
      handleMsgIfaces(msg.ifaces);
    } else if (msg.netem_params) {
      handleMsgNetemParams(msg.netem_params);
    } else if (msg.sample_period) {
      handleMsgSamplePeriod(msg.sample_period);
    }
  };


  // UI Event Handlers
  $("#chopts_series").bind('change', resetChart);
  $("#dev_select").bind('change', clearChart);
  $('#set_netem_button').bind('click', set_netem);
  $('#clear_netem_button').bind('click', clear_netem);
  $('#dev_select').bind('change', dev_select);
  $('#chopts_stop_start').bind('click', toggleStopStartGraph);

  $("#chopts_chartPeriod").bind('change', function() {
    chartingPeriod = $("#chopts_chartPeriod").val();
    if (chartingPeriod < chartingPeriodMin) {
       chartingPeriod = chartingPeriodMin;
       $("#chopts_chartPeriod").val(chartingPeriod);
    } else if (chartingPeriod > chartingPeriodMax) {
       chartingPeriod = chartingPeriodMax;
       $("#chopts_chartPeriod").val(chartingPeriod);
    }

    dataLength = Math.floor(dataLengthMultiplier * chartingPeriod);
    $("#chopts_dataLen").html(dataLength);
    resizeDataBufs(dataLength);
    resetChart();
  });

  $('#more_chopts_toggle').click(function() {
    $('#more_chopts').toggle("fast");
    return false;
  });

  $('#trigger_toggle').click(function() {
    $('#trigger_chopts').toggle("fast");
    return false;
  });

  // Changing traps from the list of traps in the trap modal
  $('#trap_names').bind('change', trapSelectionHandler);
  // Add a trap
  $('#add_trap_modal button').last().click(addTrapHandler);
  // Remove trap button(s)
  $('#traps_table tbody').on('click', 'tr button', function(event){
    var $trapTr = $(event.target).parents('tr');

    // Remove from JS
    var trapId = $trapTr.data("trapId");
    delete traps[trapId];

    // Removal from the UI
    $trapTr.remove();
  });

  $('#help_toggle').click(function() {
    $('#help').toggle("fast");
  });
});

