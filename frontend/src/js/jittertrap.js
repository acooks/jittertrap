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
  $("#chopts_dataLen").html(JT.rawData.dataLength);
  $("#chopts_chartPeriod").val(JT.charts.params.plotPeriod);

  // Initialize WebSockets
  var wsUri = "ws://" + document.domain + ":" + location.port;
  JT.ws.init(wsUri);
  
  // UI Event Handlers
  $("#chopts_series").bind('change', JT.charts.resetChart);
  $("#dev_select").bind('change', JT.charts.clearChart);
  $('#set_netem_button').bind('click', JT.ws.set_netem);
  $('#clear_netem_button').bind('click', JT.ws.clear_netem);
  $('#dev_select').bind('change', JT.ws.dev_select);
  $('#chopts_stop_start').bind('click', JT.charts.toggleStopStartGraph);

  $("#chopts_chartPeriod").bind('change', function() {
    JT.charts.params.plotPeriod = $("#chopts_chartPeriod").val();
    if (JT.charts.params.plotPeriod < JT.charts.params.plotPeriodMin) {
       JT.charts.params.plotPeriod = JT.charts.params.plotPeriodMin;
       $("#chopts_chartPeriod").val(JT.charts.params.plotPeriod);
    } else if (JT.charts.params.plotPeriod > JT.charts.params.plotPeriodMax) {
       JT.charts.params.plotPeriod = JT.charts.params.plotPeriodMax;
       $("#chopts_chartPeriod").val(JT.charts.params.plotPeriod);
    }

    JT.rawData.dataLength = Math.floor(JT.rawData.dataLengthMultiplier * JT.charts.params.plotPeriod);
    $("#chopts_dataLen").html(JT.rawData.dataLength);
    JT.charts.resizeDataBufs(JT.rawData.dataLength);
    JT.charts.resetChart();
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
  $('#trap_names').bind('change', JT.trapModule.trapSelectionHandler);
  // Add a trap
  $('#add_trap_modal button').last().click(JT.trapModule.addTrapHandler);
  // Remove trap button(s)
  $('#traps_table tbody').on('click', 'tr button', function(event){
    var $trapTr = $(event.target).parents('tr');

    // Remove from JS
    var trapId = $trapTr.data("trapId");
    JT.trapModule.deleteTrap(trapId);

    // Removal from the UI
    $trapTr.remove();
  });

  $('#help_toggle').click(function() {
    $('#help').toggle("fast");
  });
});

