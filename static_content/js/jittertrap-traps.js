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
  ;
};


// Functions for the alternative approach to controlling traps
/**
 * 
 */
var closeAddTrapModal = function() {
  $('#add_trap_modal input').val("");
  $('#add_trap_modal button').get(1).click();
}

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
      trapUnits        = $('#trap_names option:selected').val();

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
