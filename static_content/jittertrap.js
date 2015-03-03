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
    minTxThroughputTriggerVal: 0,
  };

  // Charting
  var chart = new CanvasJS.Chart("chartContainer", {});
  var histogram = new CanvasJS.Chart("histogramContainer", {});
  var basicStatsGraph = new CanvasJS.Chart("basicStatsContainer", {});

  var old_updatePeriod = updatePeriod;


  // Initialize Chart Options
  $("#chopts_refresh").val(millisecondsToRate(updatePeriod));
  $("#chopts_dataLen").val(dataLength);


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
    if (msg["stats"] && msg.stats.iface == $('#dev_select').val()) {
      handleMsgUpdateStats(samplePeriod, msg.stats);
    } else if (msg["ifaces"]) {
      handleMsgIfaces(msg["ifaces"]);
    } else if (msg["netem_params"]) {
      handleMsgNetemParams(msg["netem_params"]);
    } else if (msg["sample_period"]) {
      handleMsgSamplePeriod(msg["sample_period"]);
    }
  };


  // Console Debug Logging
  var logHistogram = function () {
    var s = $("#chopts_series option:selected").val();
    for (var i = 0; i < chartData[s].histData.length; i++) {
      console.log(chartData[s].histData[i]);
    }
  };


  // UI Event Handlers
  $("#chopts_series").bind('change', resetChart);
  $("#dev_select").bind('change', clearChart);
  $('#set_netem_button').bind('click', set_netem);
  $('#clear_netem_button').bind('click', clear_netem);
  $('#sample_period').bind('change', set_sample_period);
  $('#dev_select').bind('change', dev_select);
  $('#chopts_stop_start').bind('click', toggleStopStartGraph);
  $('#chopts_refresh').bind('change', function() {
    updatePeriod = rateToMilliseconds($("#chopts_refresh").val());
    setUpdatePeriod();
  });

  $("#chopts_dataLen").bind('change', function() {
    dataLength = $("#chopts_dataLen").val();
  });

  $('#more_chopts_toggle').click(function() {
    $('#more_chopts').toggle("fast");
    return false;
  });

  $('#trigger_toggle').click(function() {
    $('#trigger_chopts').toggle("fast");
    return false;
  });

  $('#help_toggle').click(function() {
    $('#help').toggle("fast");
  });
});

