/* global JT */

$(document).ready(function() {

  // Initialize Chart Options
  $("#jt-measure-datalength").html(JT.core.sampleCount());
  $("#chopts_chartPeriod").val(JT.charts.getChartPeriod());

  // Initialize WebSockets
  var wsUri = "ws://" + document.domain + ":" + location.port;
  JT.ws.init(wsUri);

  // UI Event Handlers
  $("#chopts_series").bind('change', JT.charts.resetChart);
  $('#set_netem_button').bind('click', JT.ws.set_netem);
  $('#clear_netem_button').bind('click', JT.ws.clear_netem);
  $('#dev_select').bind('change', JT.ws.dev_select);
  $('#chopts_stop_start').bind('click', JT.charts.toggleStopStartGraph);

  $("#chopts_chartPeriod").bind('change', function() {
    var plotPeriod = $("#chopts_chartPeriod").val();
    var result = JT.charts.setChartPeriod(plotPeriod);
    $("#chopts_chartPeriod").val(result.newPeriod);
    $("#jt-measure-datalength").html(result.sampleCount);
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

