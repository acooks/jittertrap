/* jittertrap-traps.js */

/* global Mustache */
/* global JT:true */

((my) => {
  'use strict';
  my.trapModule = {};

  var trapsNextUID = 123123;

  var trapsBin = {}; // a container for Traps

  var trapStates = { disarmed: 0, armed: 1, triggered: 2 };

  var testTypes = {
    curLessThan: function (threshold, tripped, stats) {
      return { pass: (stats.cur < threshold),
               val:  (stats.cur < tripped) ? stats.cur : tripped };
    },
    curMoreThan: function (threshold, tripped, stats) {
      return { pass: (stats.cur > threshold),
               val:  (stats.cur > tripped) ? stats.cur : tripped };
    },
    minLessThan: function (threshold, tripped, stats) {
      return { pass: (stats.min < threshold),
               val:  (stats.min < tripped) ? stats.min : tripped };
    },
    maxMoreThan: function (threshold, tripped, stats) {
      return { pass: (stats.max > threshold),
               val:  (stats.max > tripped) ? stats.max : tripped };
    },
    meanMoreThan: function (threshold, tripped, stats) {
      return { pass: (stats.mean > threshold),
               val:  (stats.mean > tripped) ? stats.mean : tripped };
    },
    meanLessThan: function (threshold, tripped, stats) {
      return { pass: (stats.mean < threshold),
               val:  (stats.mean < tripped) ? stats.mean : tripped };
    },
    meanPGapMoreThan: function (threshold, tripped, stats) {
      return { pass: (stats.meanPG > threshold),
               val:  (stats.meanPG > tripped) ? stats.meanPG : tripped };
    },
    maxPGapMoreThan: function (threshold, tripped, stats) {
      return { pass:(stats.maxPG > threshold),
               val: (stats.maxPG > tripped) ? stats.maxPG : tripped };
    }
  };

  var mapTrapIdToSeriesAndTest = {
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

  var actionTypes = {};

  actionTypes = (function (my) {
    my.logAction = {};
    my.logAction.act = function (trap, val) {
      console.log("log action. Trap: " + trap.trapType
                + " series: " + trap.seriesName
                + " threshold Val: " + trap.threshVal
                + " triggered Val: " + val);
    };

    my.logAction.reset = function (trap) {};

    return my;
  }(actionTypes));


  actionTypes = (function (my) {
    my.blinkAction = {};
    my.blinkTimeoutHandles = {};
    var handles = actionTypes.blinkTimeoutHandles;

    var ledOff = function (ledId) {
      var led = $("#"+ledId);
      led.css("color", "#FF9900");
      my.blinkTimeoutHandles.ledId = 0;
    };

    var ledOn = function (trap, val) {
      var ledId = trap.trapUID + "_led";
      var led = $("#"+ledId);
      led.css("color", "red");
      led.html("&nbsp;"+val.toFixed(2));
      if (my.blinkTimeoutHandles[ledId]) {
        clearTimeout(my.blinkTimeoutHandles[ledId]);
      }
      my.blinkTimeoutHandles[ledId] =
        setTimeout(function() { ledOff(ledId); },
                   JT.charts.getChartPeriod() + 10);
    };

    my.blinkAction.act = function (trap, val) {
      ledOn(trap, val);
    };

    my.blinkAction.reset = function (trap) {
      var ledId = trap.trapUID + "_led";
      ledOff(ledId);
      var led = $("#"+ledId);
      led.css("color", "black");
      led.html("&nbsp;");
    };

    return my;
  }(actionTypes));

  /**
   *
   */
  var TrapAction = function (trap, actionType) {
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
  var Trap = function (trapType, seriesName, triggerTester, threshVal) {
    this.trapType = trapType;
    this.trapUID = trapType + "_" + trapsNextUID++;
    this.seriesName = seriesName;
    this.triggerTester = triggerTester;
    this.threshVal = threshVal;  // threshold value
    this.tripVal = threshVal;
    this.state = trapStates.disarmed;
    this.actionList = []; // a list of TrapAction

    this.addAction = function (actionType) {
      var ta = new TrapAction(this, actionType);
      this.actionList.push(ta);
    };

    this.testAndAct = function (stats) {
      var result = this.triggerTester(this.threshVal, this.tripVal, stats);
      if (result.pass) {
        //console.log("trap triggered.");
        this.tripVal = result.val;
        $.each(this.actionList, function(idx, action) {
          //console.log("taking action: " + idx);
          action.execute(result.val);
        });
      }
    };

    this.reset = function () {
      this.tripVal = this.threshVal;
      this.state = trapStates.disarmed;

      $.each(this.actionList, function(idx, action) {
        action.reset();
      });
    };

    console.log("new trap: " + trapsNextUID);
  };

  /**
   *
   */
  my.trapModule.checkTriggers = function(seriesName, stats) {
    $.each(trapsBin, function(idx, trap) {
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
  var addTrapToUI = function(trap){
    var trapValue        = $('#trap_value').val(),
        trapValueInt     = parseInt(trapValue),
        trapTypeSelected = $('#trap_names option:selected').data('trapType'),
        trapNameSelected = $('#trap_names option:selected').text(),
        trapTable        = $('#traps_table'),
        trapUnits        = $('#trap_names option:selected').data('trapUnits');

    // Validity/Verification checks first
    if ((! isNaN(trapValueInt)) && (trapValueInt > 0)) {
      // Add the trap to the traps table
      $.get('/templates/trap.html', function(template) {
        var template_data = { trapType: trapTypeSelected,
                              trapUID:  trap.trapUID,
                              trapName: trapNameSelected,
                              trapValue: trapValueInt,
                              trapUnits: trapUnits
                            },
            rendered      = Mustache.render(template, template_data);

        trapTable.find('tbody').append(rendered);

        // Remove trap button
        $("#"+trap.trapUID+"_delete").on('click', function(event) {
          // Remove from JS
          delete trapsBin[trap.trapUID];

          // Removal from the UI
          var trapTr = $(event.target).parents('tr');
          trapTr.remove();
        });

        // Reset trap button
        $("#"+trap.trapUID+"_reset").on('click', function(event) {
          trap.reset();
        });

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
    var trapType            = $selectedTrapOption.data('trapType');
    var trapValue           = $('#trap_value').val();
    var trapValueInt        = parseInt(trapValue);

    if (trapValueInt > 0) {
      var map = mapTrapIdToSeriesAndTest[trapType];
      var t = new Trap(trapType, map.series, map.test, trapValue);
      //t.addAction(actionTypes.logAction);
      t.addAction(actionTypes.blinkAction);
      trapsBin[t.trapUID] = t;
      addTrapToUI(t);
    }
  };

})(JT);
