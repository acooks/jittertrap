/* jittertrap-traps.js */

/* global Mustache */
/* global JT:true */

JT = (function (my) {
  'use strict';
  my.trapModule = {};

  var trapsBin = {}; // a container for Traps

  var trapStates = { disarmed: 0, armed: 1, triggered: 2 };

  var testTypes = {
    minLessThan:      function (val, stats) { return (stats.min < val); },
    maxMoreThan:      function (val, stats) { return (stats.max > val); },
    meanMoreThan:     function (val, stats) { return (stats.mean > val); },
    meanLessThan:     function (val, stats) { return (stats.mean < val); },
    meanPGapMoreThan: function (val, stats) { return (stats.meanZ > val); },
    maxPGapMoreThan:  function (val, stats) { return (stats.maxZ > val); }
  };

  var mapTrapIdToSeriesAndTest = {
    mean_rx_bitrate: { series: "rxRate", test: testTypes.meanMoreThan },
    mean_tx_bitrate: { series: "txRate", test: testTypes.meanMoreThan },
    max_rx_bitrate : { series: "rxRate", test: testTypes.maxMoreThan },
    max_tx_bitrate : { series: "txRate", test: testTypes.maxMoreThan },
    min_rx_bitrate : { series: "rxRate", test: testTypes.minLessThan },
    min_tx_bitrate : { series: "txRate", test: testTypes.minLessThan },
    rx_pkt_gap     : { series: "rxRate", test: testTypes.maxPGapMoreThan },
    tx_pkt_gap     : { series: "txRate", test: testTypes.maxPGapMoreThan },
  };

  var actionTypes = {};

  actionTypes.logAction = function (sName, tVal) {
    console.log("log action. Series: " + sName + " triggerVal: " + tVal);
  };

  /**
   *
   */
  var TrapAction = function (seriesName, actionType, triggerVal) {
    this.seriesName = seriesName;
    this.triggerVal = triggerVal;
    this.execute = function () {
      actionType(this.seriesName, this.triggerVal);
    };
  };

  /**
   *
   */
  var Trap = function (seriesName, triggerTester, triggerVal) {
    this.seriesName = seriesName;
    this.triggerTester = triggerTester;
    this.triggerVal = triggerVal;  // threshold value
    this.state = trapStates.disarmed;
    this.actionList = []; // a list of TrapAction

    this.addAction = function (actionType) {
      var ta = new TrapAction(this.seriesName, actionType, this.triggerVal);
      this.actionList.push(ta);
    };

    this.testAndAct = function (stats) {
      if (this.triggerTester(this.triggerVal, stats)) {
        //console.log("trap triggered.");
        $.each(this.actionList, function(idx, action) {
          //console.log("taking action: " + idx);
          action.execute();
        });
      }
    };
  };

  /**
   *
   */
  my.trapModule.checkTriggers = function(seriesName, stats) {
    $.each(trapsBin, function(idx, trap) {
      trap.testAndAct(stats);
    });
  };

  /**
   * Handler for selecting a trap in the modal for adding traps
   * Just needs to ensure the trap measurement units are displayed
   */
  my.trapModule.trapSelectionHandler = function(event){
    var $input_group_addon = $(event.target).parent()
                             .find('.input-group-addon');
    var units = $(event.target).find('option:selected')
                .data('trapUnits');

    // Update input-group-addon with correct units for type of trap selected
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
        var template_data = { trapId: trapIdSelected,
                              trapName: trapNameSelected,
                              trapValue: trapValueInt,
                              trapUnits: trapUnits
                            },
            rendered      = Mustache.render(template, template_data);

        $trapTable.find('tbody').append(rendered);
      });

      $('#add_trap_modal input').val("");
      $('#add_trap_modal button').get(1).click();
    }
  };

  /**
   *
   */
  my.trapModule.addTrapHandler = function(event) {
    var $selectedTrapOption = $(event.target).parents('.modal')
                              .find('option:selected');
    var trapId              = $selectedTrapOption.data('trapId');
    var trapValue           = $('#trap_value').val();
    var trapValueInt        = parseInt(trapValue);

    if (trapValueInt > 0) {
      var map = mapTrapIdToSeriesAndTest[trapId];
      var t = new Trap(map.series, map.test, trapValue);
      t.addAction(actionTypes.logAction);
      trapsBin[trapId] = t;
      addTrapToUI();
    }
  };

  /**
   *
   */
  my.trapModule.deleteTrap = function (trapId) {
    delete trapsBin[trapId];
  };

  return my;
}(JT));
