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
  $("#chopts_dataLen").val(dataLength);
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

    if (msg["stats"] && msg.stats.iface == selectedIface) {
      var visibleSeries = $("#chopts_series option:selected").val();
      handleMsgUpdateStats(samplePeriod, msg.stats.s, visibleSeries);
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
  $('#dev_select').bind('change', dev_select);
  $('#chopts_stop_start').bind('click', toggleStopStartGraph);

  $("#chopts_chartPeriod").bind('change', function() {
    chartingPeriod = $("#chopts_chartPeriod").val();
    dataLength = Math.floor(dataLengthMultiplier * chartingPeriod);
    $("#chopts_dataLen").val(dataLength);
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

