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
    // params.s contains network statistics as rates per second (e.g., bytes/sec, packets/sec)
    JT.core.processDataMsg(params.s, params.ival_ns);
    JT.charts.setDirty();
  };

  const handleMsgToptalk = function (params) {
    // params contains top talker data as rates (Bytes/sec),
    // calculated over the interval.
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
    ifaces.forEach((val) => {
      const option = $('<option>').text(val).val(val);
      $('#dev_select').append(option);
    });
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
         'delay': parseInt($("#delay").val(), 10),
         'jitter': parseInt($("#jitter").val(), 10),
         /* convert the float to an integer representing 10ths of percent. */
         'loss': parseInt(Math.round(10 * $("#loss").val()), 10)
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

  const messageHandlers = {
    stats: (params) => {
      if (params.iface === selectedIface) handleMsgUpdateStats(params);
    },
    toptalk: handleMsgToptalk,
    dev_select: handleMsgDevSelect,
    iface_list: handleMsgIfaces,
    netem_params: handleMsgNetemParams,
    sample_period: handleMsgSamplePeriod,
  };

  Object.freeze(messageHandlers); // Prevent modification of messageHandlers

  my.ws.init = function(uri) {
    // Initialize WebSocket
    sock = new WebSocket(uri, "jittertrap");

    sock.onopen = function(evt) {
      const msg = JSON.stringify({'msg': 'hello'});
      sock.send(msg);
    };

    sock.onclose = function(evt) {
      console.log("unhandled websocket onclose event: " + evt);
      const errorMsgElement = $("#error-msg");
      const errorModalElement = $("#error-modal");
      errorMsgElement.html($("#error-msg").html()
                           + "<p>Websocket closed.</p>");
      $("#error-modal").modal('show');
    };

    sock.onerror = function(evt) {
      console.log("unhandled websocket onerror event: " + evt);
      const errorMsgElement = $("#error-msg");
      const errorModalElement = $("#error-modal");
      const escapedErrorMessage = "Websocket error. (Sorry, that's all we know, but the javascript console might contain useful debug information.) Are you connecting through a proxy?";
      errorMsgElement.html("<p>" + escapedErrorMessage + "</p>");
      errorModalElement.modal('show');
    };

    sock.onmessage = function(evt) {
      let msg;
      try {
        msg = JSON.parse(evt.data);
      }
      catch (err) {
        console.log("Error: " + err.message);
        return;
      }

      if (!msg || !msg.msg) {
        console.log("unrecognised message: " + evt.data);
        return;
      }

      // Validate message type to prevent potential DoS and prototype pollution
      const isValidMessageType = (Object.keys(messageHandlers).includes(msg.msg)  &&
          Object.prototype.hasOwnProperty.call(messageHandlers, msg.msg) &&
          typeof messageHandlers[msg.msg] === 'function');

      if (isValidMessageType) {
        const handler = messageHandlers[msg.msg];
        try {
          handler(msg.p);
        } catch (e) {
          console.error("Error in message handler for", msg.msg, ":", e);
        }
      } else {
        //It is possible that the message type is not implemented.
        //It is also possible that Object.freeze failed.
        console.log("unhandled message type: " + msg.msg);
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
