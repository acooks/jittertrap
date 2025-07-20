/* jittertrap-websocket.js */

/* global JT:true */

((my) => {
  'use strict';

  my.ws = {};

  let selectedIface = {}; // set on dev_select message

  /* the websocket object, see my.ws.init() */
  let sock = {};

  /**
   * Websocket Callback Functions
   * i.e. Referred to in websocket.onmessage
   */

  const handleMsgUpdateStats = function (params) {
    JT.core.processDataMsg(params.s, params.ival_ns);
    JT.charts.setDirty();
  };

  const handleMsgToptalk = function (params) {
    JT.core.processTopTalkMsg(params);
    JT.charts.setDirty();
  };

  const handleMsgDevSelect = function(params) {
    const iface = params.iface;
    console.log("iface: " + iface);
    $('#dev_select').val(iface);
    selectedIface = $('#dev_select').val();
    JT.core.clearAllSeries();
    JT.charts.resetChart();
  };

  const handleMsgIfaces = function(params) {
    const ifaces = params.ifaces;
    $('#dev_select').empty();
    $.each(ifaces,
      function (ix, val) {
        const option = $('<option>').text(val).val(val);
        $('#dev_select').append(option);
      }
    );
  };

  const handleMsgNetemParams = function(params) {
    JT.programsModule.processNetemMsg(params);
  };

  const handleMsgSamplePeriod = function(params) {
    const period = params.period;
    my.core.samplePeriod(period);
    $("#jt-measure-sample-period").html(period / 1000.0 + "ms");
    console.log("sample period: " + period);
    JT.core.clearAllSeries();
    JT.charts.resetChart();
  };


  /**
   * Websocket Sending Functions
   */

  const dev_select = function() {
    const msg = JSON.stringify({'msg':'dev_select',
                              'p': { 'iface': $("#dev_select").val()}});
    sock.send(msg);
  };

  const set_netem = function() {
    const msg = JSON.stringify(
      {'msg': 'set_netem',
       'p': {
         'dev': $("#dev_select").val(),
         'delay': parseInt($("#delay").val()),
         'jitter': parseInt($("#jitter").val()),
         /* convert the float to an integer representing 10ths of percent. */
         'loss': parseInt(Math.round(10 * $("#loss").val()))
       }
      });
    sock.send(msg);
    return false;
  };

  const clear_netem = function() {
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
      const msg = JSON.stringify({'msg': 'hello'});
      sock.send(msg);
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
      let msg;
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
      const msgType = msg.msg;

      if ((msgType === "stats") && msg.p.iface === selectedIface) {
        handleMsgUpdateStats(msg.p);
      } else if (msgType === "toptalk") {
        handleMsgToptalk(msg.p);
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

})(JT);
/* End of jittertrap-websocket.js */
