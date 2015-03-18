// Returns true if the given DOM element is checked (assuming it's a checkbox)
var isChecked = function(checkbox) {
  var checkboxEnabled = $(checkbox).prop('checked');
  return checkboxEnabled;
};
// Returns a jQuery object of the Traps value input, found inside the given traps container
var trapInput = function(trapContainer) {
  var inputSelector = '.input-group input';
  return trapContainer.find(inputSelector).first();
};
// The trapId for the given traps container
var trapId = function(trapContainer) {
  return trapInput(trapContainer).prop('id');
};
// The value of the Trap inside the traps container
var trapValue = function(trapContainer) {
  return trapInput(trapContainer).val();
};
// Enable a trap
var addTrap = function(trapContainer) {
  var id = trapId(trapContainer);

  console.log("Adding Trap: " + id);
  traps[id] = trapValue(trapContainer);
  console.log(traps);
};
// Disable a trap
var removeTrap = function(trapContainer) {
  var id = trapId(trapContainer);

  console.log("Removing Trap: " + id);
  delete traps[id]
  console.log(traps);
};
// The change event handler, to be used as a toggle for enabling/disabling traps
var toggleTrap = function(event) {
  var trapContainer = $(event.target).parents('.trapContainer'),
      trapCheckbox  = event.target;

  console.log("Toggled Trap...");

  // Adding a Trap
  if (isChecked(trapCheckbox)) {
    addTrap(trapContainer);
  }
  // Removing a Trap
  else {
    removeTrap(trapContainer);
  }
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
  ;
};


/**
 * Handler for Trap input elements
 * Just enables and disables the associated checkbox when there is data or no-data
 */
var trapInputHandler = function(event) {
  var inputValue = $(event.target).val(),
      inputInt = parseInt(inputValue),
      $checkbox = $(event.target).parents('.trapContainer').find('input:checkbox');

  // Enable the checkbox to allow the trap to be activated when valid input is given
  if (inputInt > 0) {
    $checkbox.prop('disabled', false);
  }
  // Diable the checkbox when no or invalid input is given
  else if (isNaN(inputInt)) {
    $checkbox.prop('disabled', true);
  }
};



// Functions for the alternative approach to controlling traps
// function getParentTrapContainer($element) {
//   return $element.parents('.trapContainer');
// }

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
      trapNameSelected = $('#trap_names option:selected').text(),
      $trapTable       = $('#traps_table'),
      trapUnits        = $('#trap_names option:selected').val();

  // Validity/Verification checks first
  if ((! isNaN(trapValueInt)) && (trapValueInt > 0)) {
    // Add the trap to the traps table
    $.get('/templates/trap.html', function(template) {
      var template_data = { trapName: trapNameSelected, trapValue: trapValueInt, trapUnits: trapUnits };
      var rendered = Mustache.render(template, template_data);
      //$('#target').html(rendered);
      $trapTable.find('tbody').append(rendered);
    });

    closeAddTrapModal();
  }
};

var addTrapHandler = function(event) {
  var $selectedTrapOption = $(event.target).parents('.modal').find('option:selected'),
      trapId              = $selectedTrapOption.data('trapId'),
      trapValue           = $('#trap_value').val();

  traps[trapId] = trapValue;
  addTrapToUI();
};
