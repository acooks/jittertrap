/**
 * Websocket Callback Functions
 * i.e. Referred to in websocket.onmessage
 */

var handleMsgUpdateStats = function (samplePeriod, stats, seriesName) {
   var selectedSeries = chartData[seriesName];
   var len = stats.length;
   for (var i = 0; i < len; i++) {
     var d = stats[i];
     updateSeries(chartData.txDelta, xVal, d.txDelta, selectedSeries);
     updateSeries(chartData.rxDelta, xVal, d.rxDelta, selectedSeries);
     updateSeries(chartData.txRate, xVal, byteCountToKbpsRate(d.txDelta), selectedSeries);
     updateSeries(chartData.rxRate, xVal, byteCountToKbpsRate(d.rxDelta), selectedSeries);
     updateSeries(chartData.txPacketRate, xVal, packetDeltaToRate(d.txPktDelta), selectedSeries);
     updateSeries(chartData.rxPacketRate, xVal, packetDeltaToRate(d.rxPktDelta), selectedSeries);
     updateSeries(chartData.txPacketDelta, xVal, d.txPktDelta, selectedSeries);
     updateSeries(chartData.rxPacketDelta, xVal, d.rxPktDelta, selectedSeries);
     xVal++;
     xVal = xVal % dataLength;
  }

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
  $("#sample_period").html(period / 1000.0 + "ms");
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

var clear_netem = function() {
  $("#delay").val(0);
  $("#jitter").val(0);
  $("#loss").val(0);
  set_netem();
  return false;
}

var get_sample_period = function() {
  var msg = JSON.stringify({'msg': 'get_sample_period'});
  websocket.send(msg);
};

