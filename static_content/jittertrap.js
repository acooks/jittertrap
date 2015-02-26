$(document).ready(function() {

        var triggers = {
          maxRxThroughputEnabled: false,
          maxTxThroughputEnabled: false,
          minRxThroughputEnabled: false,
          minTxThroughputEnabled: false,
          maxTxSilenceEnabled:    false,
          maxRxSilenceEnabled:    false,
          maxRxThroughputTriggerVal: 0,
          maxTxThroughputTriggerVal: 0,
          minRxThroughputTriggerVal: 0,
          minTxThroughputTriggerVal: 0,
        };

        var chart = new CanvasJS.Chart("chartContainer", {});
        var histogram = new CanvasJS.Chart("histogramContainer", {});
        var basicStatsGraph = new CanvasJS.Chart("basicStatsContainer", {});

        var old_updatePeriod = updatePeriod;


        $("#chopts_refresh").val(millisecondsToRate(updatePeriod));
        
        $("#chopts_dataLen").val(dataLength);


        var wsUri = "ws://" + document.domain + ":" + location.port;
        websocket = new WebSocket(wsUri);

        websocket.onopen = function(evt) { 
          websocket.send("open!");
          list_ifaces();
          get_sample_period();
        };

        websocket.onclose = function(evt) {
          console.log("unhandled websocket onclose event: " + evt);
        };

        websocket.onerror = function(evt) {
          console.log("unhandled websocket onerror event: " + evt);
        };

        websocket.onmessage = function(evt) {
          var msg = JSON.parse(evt.data);
          if (msg["stats"] && msg.stats.iface == $('#dev_select').val()) {
            handleMsgUpdateStats(samplePeriod, msg.stats);
          } else if (msg["ifaces"]) {
            handleMsgIfaces(msg["ifaces"]);
          } else if (msg["netem_params"]) {
            handleMsgNetemParams(msg["netem_params"]);
          } else if (msg["sample_period"]) {
            handleMsgSamplePeriod(msg["sample_period"]);
          }
        };

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

        var logHistogram = function () {
          var s = $("#chopts_series option:selected").val();
          for (var i = 0; i < chartData[s].histData.length; i++) {
            console.log(chartData[s].histData[i]);
          }
        };

        var handleMsgUpdateStats = function (samplePeriod, stats) {

          updateSeries(chartData.txDelta,
                       xVal * samplePeriod,
                       stats["tx-delta"]);

          updateSeries(chartData.rxDelta,
                       xVal * samplePeriod,
                       stats["rx-delta"]);

          updateSeries(chartData.txRate,
                       xVal,
                       byteCountToKbpsRate(stats["tx-delta"]));

          updateSeries(chartData.rxRate,
                       xVal,
                       byteCountToKbpsRate(stats["rx-delta"]));

          updateSeries(chartData.txPacketRate,
                       xVal,
                       packetDeltaToRate(stats["tx-pkt-delta"]));

          updateSeries(chartData.rxPacketRate,
                       xVal,
                       packetDeltaToRate(stats["rx-pkt-delta"]));

          updateSeries(chartData.txPacketDelta,
                       xVal,
                       stats["tx-pkt-delta"]);

          updateSeries(chartData.rxPacketDelta,
                       xVal,
                       stats["rx-pkt-delta"]);
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

        $("#chopts_series").bind('change', resetChart);
        $("#dev_select").bind('change', clearChart);
        $('#set_netem_button').bind('click', set_netem);
        $('#sample_period').bind('change', set_sample_period);
        $('#dev_select').bind('change', dev_select);
        $('#chopts_stop_start').bind('click', toggleStopStartGraph);
        $('#chopts_refresh').bind('change',
           function() {
             updatePeriod = rateToMilliseconds($("#chopts_refresh").val());
             setUpdatePeriod();
           }
        );

        $("#chopts_dataLen").bind('change',
           function() {
             dataLength = $("#chopts_dataLen").val();
           });

        $('#more_chopts_toggle').click(function() {
          $('#more_chopts').toggle("fast");
          return false;
        });

        $('#trigger_toggle').click(function() {
          $('#trigger_chopts').toggle("fast");
          return false;
        });

        $('#help_toggle').click(function() {
          $('#help').toggle("fast");
        });

});

