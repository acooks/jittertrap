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
// The trapId for a given traps container
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
