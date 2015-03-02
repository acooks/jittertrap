/**
 * Websocket Callback Functions
 * i.e. Referred to in websocket.onmessage
 */
var handleMsgUpdateStats = function (samplePeriod, stats) {
  updateSeries(chartData.txDelta, xVal * samplePeriod, stats["tx-delta"]);
  updateSeries(chartData.rxDelta, xVal * samplePeriod, stats["rx-delta"]);
  updateSeries(chartData.txRate, xVal, byteCountToKbpsRate(stats["tx-delta"]));
  updateSeries(chartData.rxRate, xVal, byteCountToKbpsRate(stats["rx-delta"]));
  updateSeries(chartData.txPacketRate, xVal, packetDeltaToRate(stats["tx-pkt-delta"]));
  updateSeries(chartData.rxPacketRate, xVal, packetDeltaToRate(stats["rx-pkt-delta"]));
  updateSeries(chartData.txPacketDelta, xVal, stats["tx-pkt-delta"]);
  updateSeries(chartData.rxPacketDelta, xVal, stats["rx-pkt-delta"]);

  xVal++;
  checkTriggers();
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
  if (params.delay == -1 && params.jitter == -1 && params.loss == -1) {
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
  samplePeriod = period;
  $("#sample_period").val(period + "ms");
  console.log("sample_period: " + period);
  setUpdatePeriod();
  clearChart();
};


/**
 * Websocket Sending Functions
 */
var list_ifaces = function() {
  var msg = JSON.stringify({'msg':'list_ifaces'});
  websocket.send(msg);
};

var dev_select = function() {
  var msg = JSON.stringify({'msg':'dev_select',
                            'dev': $("#dev_select").val()});
  websocket.send(msg);
  get_netem();
};

var get_netem = function() {
  var msg = JSON.stringify(
    {'msg': 'get_netem', 
      'dev': $("#dev_select").val()
    });
  websocket.send(msg);
};

var set_netem = function() {
  var msg = JSON.stringify(
    {'msg': 'set_netem',
      'dev': $("#dev_select").val(),
      'delay': $("#delay").val(),
      'jitter': $("#jitter").val(),
      'loss': $("#loss").val()
    });
  websocket.send(msg);
  return false;
};

var get_sample_period = function() {
  var msg = JSON.stringify({'msg': 'get_sample_period'});
  websocket.send(msg);
};

var set_sample_period = function() {
  samplePeriod = $("#sample_period").val();
  var msg = JSON.stringify(
    {'msg': 'set_sample_period',
      'period': samplePeriod
    });
  websocket.send(msg);
};
