/* jittertrap-traps.js */

/* global Mustache */
/* global JT:true */

((my) => {
  'use strict';
  my.trapModule = {};

  let trapsNextUID = 123123;

  const trapsBin = {}; // a container for Traps

  const trapStates = { disarmed: 0, armed: 1, triggered: 2 };

  const testTypes = {
    curLessThan: (threshold, tripped, stats) => ({
      pass: (stats.cur < threshold),
      val:  (stats.cur < tripped) ? stats.cur : tripped }),
    curMoreThan: (threshold, tripped, stats) => ({
      pass: (stats.cur > threshold),
      val:  (stats.cur > tripped) ? stats.cur : tripped }),
    minLessThan: (threshold, tripped, stats) => ({
      pass: (stats.min < threshold),
      val:  (stats.min < tripped) ? stats.min : tripped }),
    maxMoreThan: (threshold, tripped, stats) => ({
      pass: (stats.max > threshold),
      val:  (stats.max > tripped) ? stats.max : tripped }),
    meanMoreThan: (threshold, tripped, stats) => ({
      pass: (stats.mean > threshold),
      val:  (stats.mean > tripped) ? stats.mean : tripped }),
    meanLessThan: (threshold, tripped, stats) => ({
      pass: (stats.mean < threshold),
      val:  (stats.mean < tripped) ? stats.mean : tripped }),
    meanPGapMoreThan: (threshold, tripped, stats) => ({
      pass: (stats.meanPG > threshold),
      val:  (stats.meanPG > tripped) ? stats.meanPG : tripped }),
    maxPGapMoreThan: (threshold, tripped, stats) => ({
      pass:(stats.maxPG > threshold),
      val: (stats.maxPG > tripped) ? stats.maxPG : tripped })
  };

  const mapTrapIdToSeriesAndTest = {
    cur_rx_bitrate_lt : { series: "rxRate", test: testTypes.curLessThan },
    cur_rx_bitrate_mt : { series: "rxRate", test: testTypes.curMoreThan },
    mean_rx_bitrate: { series: "rxRate", test: testTypes.meanMoreThan },
    mean_tx_bitrate: { series: "txRate", test: testTypes.meanMoreThan },
    max_rx_bitrate : { series: "rxRate", test: testTypes.maxMoreThan },
    max_tx_bitrate : { series: "txRate", test: testTypes.maxMoreThan },
    min_rx_bitrate : { series: "rxRate", test: testTypes.minLessThan },
    min_tx_bitrate : { series: "txRate", test: testTypes.minLessThan },
    rx_pkt_gap     : { series: "rxRate", test: testTypes.maxPGapMoreThan },
    tx_pkt_gap     : { series: "txRate", test: testTypes.maxPGapMoreThan }
  };

  let actionTypes = {};

  actionTypes = (function (my) {
    my.logAction = {};
    my.logAction.act = (trap, val) => {
      console.log("log action. Trap: " + trap.trapType
                + " series: " + trap.seriesName
                + " threshold Val: " + trap.threshVal
                + " triggered Val: " + val);
    };

    my.logAction.reset = () => {};

    return my;
  }(actionTypes));


  actionTypes = (function (my) {
    my.blinkAction = {};
    my.blinkTimeoutHandles = {};
    const handles = actionTypes.blinkTimeoutHandles;

    const ledOff = function (ledId) {
      const led = $("#"+ledId);
      led.css("color", "#FF9900");
      my.blinkTimeoutHandles[ledId] = 0;
    };

    const ledOn = function (trap, val) {
      const ledId = trap.trapUID + "_led";
      const led = $("#"+ledId);
      led.css("color", "red");
      led.html("&nbsp;"+val.toFixed(2));
      if (my.blinkTimeoutHandles[ledId]) {
        clearTimeout(my.blinkTimeoutHandles[ledId]);
      }
      my.blinkTimeoutHandles[ledId] =
        setTimeout(() => ledOff(ledId),
                   JT.charts.getChartPeriod() + 10);
    };

    my.blinkAction.act = (trap, val) => ledOn(trap, val);

    my.blinkAction.reset = (trap) => {
      const ledId = trap.trapUID + "_led";
      ledOff(ledId);
      const led = $("#"+ledId);
      led.css("color", "black");
      led.html("&nbsp;");
    };

    return my;
  }(actionTypes));

  /**
   *
   */
  const TrapAction = function (trap, actionType) {
    this.trap = trap;

    this.execute = function (triggeredVal) {
      actionType.act(trap, triggeredVal);
    };

    this.reset = function () {
      actionType.reset(trap);
    };
  };

  /**
   *
   */
  const Trap = function (trapType, seriesName, triggerTester, threshVal) {
    this.trapType = trapType;
    this.trapUID = trapType + "_" + trapsNextUID++;
    this.seriesName = seriesName;
    this.triggerTester = triggerTester;
    this.threshVal = threshVal;  // threshold value
    this.tripVal = threshVal;
    this.state = trapStates.disarmed;
    this.actionList = []; // a list of TrapAction

    this.addAction = function (actionType) {
      const ta = new TrapAction(this, actionType);
      this.actionList.push(ta);
    };

    this.testAndAct = function (stats) {
      const result = this.triggerTester(this.threshVal, this.tripVal, stats);
      if (result.pass) {
        //console.log("trap triggered.");
        this.tripVal = result.val;
        this.actionList.forEach(action => action.execute(result.val));
      }
    };

    this.reset = function () {
      this.tripVal = this.threshVal;
      this.state = trapStates.disarmed;

      this.actionList.forEach(action => action.reset());
    };

    console.log("new trap: " + trapsNextUID);
  };

  /**
   *
   */
  my.trapModule.checkTriggers = function(seriesName, stats) {
    Object.values(trapsBin).forEach(trap => {
      if (trap.seriesName == seriesName) {
        trap.testAndAct(stats);
      }
    });
  };

  /**
   * Handler for selecting a trap in the modal for adding traps
   * Just needs to ensure the trap measurement units are displayed
   */
  my.trapModule.trapSelectionHandler = function(event){
    const $input_group_addon = $(event.target).siblings('.input-group').find('.input-group-addon');
    const units = $(event.target).find('option:selected')
                .data('trapUnits');

    // Update input-group-addon with correct units for type of trap selected
    $input_group_addon.text(units);
  };

  /**
   *
   */
  const addTrapToUI = function(trap){
    const trapValue        = $('#trap_value').val(),
          trapValueInt     = parseInt(trapValue, 10),
          trapTypeSelected = $('#trap_names option:selected').data('trapType'),
          trapNameSelected = $('#trap_names option:selected').text(),
          trapTable        = $('#traps_table'),
        trapUnits        = $('#trap_names option:selected').data('trapUnits');

    // Validity/Verification checks first
    if ((! isNaN(trapValueInt)) && (trapValueInt > 0)) {
      // Add the trap to the traps table
      $.get('/templates/trap.html', (template) => {
        const template_data = { trapType: trapTypeSelected,
                              trapUID:  trap.trapUID,
                              trapName: trapNameSelected,
                              trapValue: trapValueInt,
                              trapUnits: trapUnits
                            },
            rendered      = Mustache.render(template, template_data);

        trapTable.find('tbody').append(rendered);

        // Remove trap button
        $("#"+trap.trapUID+"_delete").on('click', (event) => {
          // Remove from JS
          delete trapsBin[trap.trapUID];

          // Removal from the UI
          const trapTr = $(event.target).closest('tr');
          trapTr.remove();
        });

        // Reset trap button
        $(`#${trap.trapUID}_reset`).on('click', () => {
          trap.reset();
        });

      });

      $('#add_trap_modal input').val("");
      $('#add_trap_modal').modal('hide');
    }
  };

  /**
   *
   */
  my.trapModule.addTrapHandler = function(event) {
    const $selectedTrapOption = $(event.target).closest('.modal').find('option:selected');
    const trapType            = $selectedTrapOption.data('trapType');
    const trapValue           = $('#trap_value').val();
    const trapValueInt        = parseInt(trapValue, 10);

    if (trapValueInt > 0) {
      const map = mapTrapIdToSeriesAndTest[trapType];
      const t = new Trap(trapType, map.series, map.test, trapValueInt);
      //t.addAction(actionTypes.logAction);
      t.addAction(actionTypes.blinkAction);
      trapsBin[t.trapUID] = t;
      addTrapToUI(t);
    }
  };

})(JT);
