/* jittertrap-websocket.js */
JT = (function (my) {
  'use strict';

  my.ws = {};

  /* the websocket object, see my.ws.init() */
  var sock = {};

  /**
   * Websocket Callback Functions
   * i.e. Referred to in websocket.onmessage
   */

  var handleMsgUpdateStats = function (stats, seriesName) {
    var s = my.charts.series;
    var sSeries = s[seriesName];
    var len = stats.length;
    var x = my.rawData.xVal; /* careful! copy, not alias */
    for (var i = 0; i < len; i++) {
      var d = stats[i];
      my.utils.updateSeries(s.txDelta, x, d.txDelta, sSeries);
      my.utils.updateSeries(s.rxDelta, x, d.rxDelta, sSeries);
      my.utils.updateSeries(s.txRate, x, my.utils.byteCountToKbpsRate(d.txDelta), sSeries);
      my.utils.updateSeries(s.rxRate, x, my.utils.byteCountToKbpsRate(d.rxDelta), sSeries);
      my.utils.updateSeries(s.txPacketRate, x, my.utils.packetDeltaToRate(d.txPktDelta), sSeries);
      my.utils.updateSeries(s.rxPacketRate, x, my.utils.packetDeltaToRate(d.rxPktDelta), sSeries);
      my.utils.updateSeries(s.txPacketDelta, x, d.txPktDelta, sSeries);
      my.utils.updateSeries(s.rxPacketDelta, x, d.rxPktDelta, sSeries);
      x++;
      x = x % my.rawData.dataLength;
    }
    my.rawData.xVal = x; /* update global, because x is local, not a pointer */

    my.trapModule.checkTriggers();

  };

  var handleMsgIfaces = function(ifaces) {
    $('#dev_select').empty();
    $.each(ifaces,
      function (ix, val) {
        var option = $('<option>').text(val).val(val);
        $('#dev_select').append(option);
      }
    );
    dev_select();
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

  var handleMsgSamplePeriod = function(period) {
    my.rawData.samplePeriod = period;
    $("#jt-measure-sample-period").html(period / 1000.0 + "ms");
    console.log("sample period: " + period);
    my.charts.setUpdatePeriod();
    my.charts.clearChart();
  };


  /**
   * Websocket Sending Functions
   */
  var list_ifaces = function() {
    var msg = JSON.stringify({'msg':'list_ifaces'});
    sock.send(msg);
  };

  var dev_select = function() {
    var msg = JSON.stringify({'msg':'dev_select',
                              'dev': $("#dev_select").val()});
    sock.send(msg);
    get_netem();
  };

  var get_netem = function() {
    var msg = JSON.stringify(
      {'msg': 'get_netem', 
       'dev': $("#dev_select").val()
      });
    sock.send(msg);
  };

  var set_netem = function() {
    var msg = JSON.stringify(
      {'msg': 'set_netem',
       'dev': $("#dev_select").val(),
       'delay': $("#delay").val(),
       'jitter': $("#jitter").val(),
       'loss': $("#loss").val()
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

  var get_sample_period = function() {
    var msg = JSON.stringify({'msg': 'get_sample_period'});
    sock.send(msg);
  };

  my.ws.init = function(uri) {
    // Initialize WebSocket
    sock = new WebSocket(uri);

    sock.onopen = function(evt) {
      sock.send("open!");
      list_ifaces();
      get_sample_period();
    };

    sock.onclose = function(evt) {
      console.log("unhandled websocket onclose event: " + evt);
    };

    sock.onerror = function(evt) {
      console.log("unhandled websocket onerror event: " + evt);
    };

    sock.onmessage = function(evt) {
      var msg = JSON.parse(evt.data);
      var selectedIface = $('#dev_select').val();

      if (msg.stats && msg.stats.iface === selectedIface) {
        var visibleSeries = $("#chopts_series option:selected").val();
        handleMsgUpdateStats(msg.stats.s, visibleSeries);
      } else if (msg.ifaces) {
        handleMsgIfaces(msg.ifaces);
      } else if (msg.netem_params) {
        handleMsgNetemParams(msg.netem_params);
      } else if (msg.sample_period) {
        handleMsgSamplePeriod(msg.sample_period);
      }
    };
  };

  /**
   * Websocket Sending Functions
   */
  my.ws.list_ifaces = list_ifaces;
  my.ws.dev_select = dev_select;
  my.ws.get_netem = get_netem;
  my.ws.set_netem = set_netem;
  my.ws.clear_netem = clear_netem;
  my.ws.get_sample_period = get_sample_period;

  return my;
}(JT));
/* End of jittertrap-websocket.js */
