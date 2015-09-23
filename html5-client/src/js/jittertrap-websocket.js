/* jittertrap-websocket.js */

/* global JT:true */

JT = (function (my) {
  'use strict';

  my.ws = {};

  var selectedIface = {}; // set on dev_select message

  /* the websocket object, see my.ws.init() */
  var sock = {};

  /**
   * Websocket Callback Functions
   * i.e. Referred to in websocket.onmessage
   */

  var handleMsgUpdateStats = function (stats) {
    JT.core.processDataMsg(stats);
  };

  var handleMsgDevSelect = function(params) {
    var iface = params.iface;
    console.log("iface: " + iface);
    $('#dev_select').val(iface);
    selectedIface = $('#dev_select').val();
    JT.core.clearAllSeries();
    JT.charts.resetChart();
  };

  var handleMsgIfaces = function(params) {
    var ifaces = params.ifaces;
    $('#dev_select').empty();
    $.each(ifaces,
      function (ix, val) {
        var option = $('<option>').text(val).val(val);
        $('#dev_select').append(option);
      }
    );
  };

  var handleMsgNetemParams = function(params) {
    if (params.delay === -1 && params.jitter === -1 && params.loss === -1) {
      $("#netem_status").html("No active impairment on device. Set parameters to activate.");
      $("#delay").val("0");
      $("#jitter").val("0");
      $("#loss").val("0");
    } else {
      $("#netem_status").html("Ready");
      $("#delay").val(params.delay);
      $("#jitter").val(params.jitter);
      $("#loss").val(params.loss);
    }
  };

  var handleMsgSamplePeriod = function(params) {
    var period = params.period;
    my.core.samplePeriod(period);
    $("#jt-measure-sample-period").html(period / 1000.0 + "ms");
    console.log("sample period: " + period);
    my.charts.setUpdatePeriod();
    JT.core.clearAllSeries();
    JT.charts.resetChart();
  };


  /**
   * Websocket Sending Functions
   */

  var dev_select = function() {
    var msg = JSON.stringify({'msg':'dev_select',
                              'p': { 'dev': $("#dev_select").val()}});
    sock.send(msg);
  };

  var set_netem = function() {
    var msg = JSON.stringify(
      {'msg': 'set_netem',
       'p': {
         'dev': $("#dev_select").val(),
         'delay': $("#delay").val(),
         'jitter': $("#jitter").val(),
         'loss': $("#loss").val()
       }
      });
    sock.send(msg);
    return false;
  };

  var clear_netem = function() {
    $("#delay").val(0);
    $("#jitter").val(0);
    $("#loss").val(0);
    set_netem();
    return false;
  };

  my.ws.init = function(uri) {
    // Initialize WebSocket
    sock = new WebSocket(uri, "jittertrap");

    sock.onopen = function(evt) {
      sock.send("open!");
    };

    sock.onclose = function(evt) {
      console.log("unhandled websocket onclose event: " + evt);
      $("#error-msg").html($("#error-msg").html()
                           + "<p>Websocket closed.</p>");
      $("#error-modal").modal('show');
    };

    sock.onerror = function(evt) {
      console.log("unhandled websocket onerror event: " + evt);
      $("#error-msg").html("<p>"
                           + "Websocket error."
                           + " (Sorry, that's all we know, but the javascript console might contain useful debug information.)"
                           + "</p>"
                           + "<p>Are you connecting through a proxy?</p>");
      $("#error-modal").modal('show');
    };

    sock.onmessage = function(evt) {
      var msg;
      try {
        msg = JSON.parse(evt.data);
      }
      catch (err) {
        console.log("Error: " + err.message);
      }

      if (!msg || !msg.msg) {
        console.log("unrecognised message: " + evt.data);
        return;
      }
      var msgType = msg.msg;

      if ((msgType === "stats") && msg.p.iface === selectedIface) {
        handleMsgUpdateStats(msg.p.s);
      } else if (msgType === "dev_select") {
        handleMsgDevSelect(msg.p);
      } else if (msgType === "iface_list") {
        handleMsgIfaces(msg.p);
      } else if (msgType === "netem_params") {
        handleMsgNetemParams(msg.p);
      } else if (msgType === "sample_period") {
        handleMsgSamplePeriod(msg.p);
      } else {
        console.log("unhandled message: " + evt.data);
      }
    };
  };

  /**
   * Websocket Sending Functions
   */
  my.ws.dev_select = dev_select;
  my.ws.set_netem = set_netem;
  my.ws.clear_netem = clear_netem;

  return my;
}(JT));
/* End of jittertrap-websocket.js */
