$(document).ready(function() {
        var chartData = {};

        chartData.txDelta = {
          data:[],
          title:"Tx Bytes per sample period",
          ylabel:"Tx Bytes per sample",
          xlabel:"Time"
        };

        chartData.rxDelta = {
          data:[],
          title:"Rx Bytes per sample period",
          ylabel:"Rx Bytes per sample",
          xlabel:"Time"
        };

        chartData.rxRate = {
          data:[],
          title: "Ingress throughput in kbps",
          ylabel:"kbps, mean",
          xlabel:"sample number",
        };

        chartData.txRate = {
          data:[],
          title: "Egress throughput in kbps",
          ylabel:"kbps, mean",
          xlabel:"sample number",
        };

        chartData.txPacketRate = {
          data:[],
          title: "Egress packet rate",
          ylabel:"pkts per sec, mean",
          xlabel:"time",
        };

        chartData.rxPacketRate = {
          data:[],
          title: "Ingress packet rate",
          ylabel:"pkts per sec, mean",
          xlabel:"time",
        };

        chartData.txPacketDelta = {
          data:[],
          title: "Egress packets per sample",
          ylabel:"packets sent",
          xlabel:"sample number",
        };

        chartData.rxPacketDelta = {
          data:[],
          title: "Ingress packets per sample",
          ylabel:"packets received",
          xlabel:"sample number",
        };

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

        var xVal = 0;
        var dataLength = 2000; // number of dataPoints visible at any point
        var updatePeriod = 100;
        var samplePeriod = 100;

        var chart = new CanvasJS.Chart("chartContainer", {
                axisY:{
                  includeZero: "false", 
                },
                zoomEnabled: "true",
                panEnabled: "true",
                title : { text: chartData.rxRate.title },
                axisY: { title: chartData.rxRate.ylabel },
                axisX: { title: chartData.rxRate.xlabel },
                data: [{
                        name: "rxRate",
                        type: "line",
                        dataPoints: chartData.rxRate.data
                }]
        });

        var old_updatePeriod = updatePeriod;

        var resetChart = function() {
          var s = $("#chopts_series option:selected").val();
          chart = new CanvasJS.Chart("chartContainer", {
            axisY:{
              includeZero: "false",
            },
            zoomEnabled: "true",
            panEnabled: "true",
            title: { text: chartData[s].title },
            axisY: { title: chartData[s].ylabel },
            axisX: { title: chartData[s].xlabel },
            data: [{
              name: s,
              type: "line",
              dataPoints: chartData[s].data
            }]
          });
          chart.render();
        };

        var clearChart = function() {
          chartData.txDelta.data = [];
          chartData.rxDelta.data = [];
          chartData.rxRate.data = [];
          chartData.txRate.data = [];
          chartData.txPacketRate.data = [];
          chartData.rxPacketRate.data = [];
          chartData.rxPacketDelta.data = [];
          chartData.txPacketDelta.data = [];
          resetChart();
          xVal = 0;
        };

        var toggleStopStartGraph = function() {
          var maxUpdatePeriod = 9999999999;
          if (updatePeriod  != maxUpdatePeriod) {
            old_updatePeriod = updatePeriod;
            updatePeriod = maxUpdatePeriod;
          } else {
            updatePeriod = old_updatePeriod;
          }
          setUpdatePeriod();
          return false;
        };        

        // interval in milliseconds
        var millisecondsToRate = function(ms) {
          if (ms > 0) {
            return Math.ceil(1.0 / ms * 1000.0);
          }
        };

        var rateToMilliseconds = function(r) {
          if (r > 0) {
            return Math.ceil(1.0 / r * 1000.0);
          }
        };

        /* count must be bytes, duration must be milliseconds */
        var byteCountToKbpsRate = function(count) {
          var rate = count * (1000.0 / samplePeriod) * 8.0 / 1000.0;
          return rate;
        };

        var packetDeltaToRate = function(count) {
          return count * (1000.0 / samplePeriod);
        };

        var setUpdatePeriod = function() {
          var sampleRate = millisecondsToRate(samplePeriod);
          var updateRate = millisecondsToRate(updatePeriod);
          if (sampleRate < updateRate) {
            updatePeriod = rateToMilliseconds(sampleRate);
            $("#chopts_refresh").val(sampleRate);
          } else if (updateRate > 30) {
            updatePeriod = rateToMilliseconds(30);
            $("#chopts_refresh").val(30);
          } else {
            $("#chopts_refresh").val(updateRate);
          }
          clearInterval(drawInterval);
          drawInterval = setInterval(function() { chart.render(); },
                                     updatePeriod);
          console.log("updateRate: " + updateRate + " sampleRate: " + sampleRate);
        };

        $("#chopts_refresh").val(millisecondsToRate(updatePeriod));
        
        $("#chopts_dataLen").val(dataLength);

        var checkTriggers = function() {
          ;
        };

        var drawInterval = setInterval(function() { chart.render(); },
                                       updatePeriod);

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
          var msg = JSON.stringify(
            {'msg': 'set_sample_period',
             'period': $("#sample_period").val()
            });
          websocket.send(msg);
        };

        var updateSeries = function (series, xVal, yVal) {
          series.push({ x: xVal, y: yVal });
          while (series.length > dataLength) {
            series.shift();
          }
        };

        var handleMsgUpdateStats = function (samplePeriod, stats) {

          updateSeries(chartData.txDelta.data,
                       xVal * samplePeriod,
                       stats["tx-delta"]);

          updateSeries(chartData.rxDelta.data,
                       xVal * samplePeriod,
                       stats["rx-delta"]);

          updateSeries(chartData.txRate.data,
                       xVal,
                       byteCountToKbpsRate(stats["tx-delta"]));

          updateSeries(chartData.rxRate.data,
                       xVal,
                       byteCountToKbpsRate(stats["rx-delta"]));

          updateSeries(chartData.txPacketRate.data,
                       xVal,
                       packetDeltaToRate(stats["tx-pkt-delta"]));

          updateSeries(chartData.rxPacketRate.data,
                       xVal,
                       packetDeltaToRate(stats["rx-pkt-delta"]));

          updateSeries(chartData.txPacketDelta.data,
                       xVal,
                       stats["tx-pkt-delta"]);

          updateSeries(chartData.rxPacketDelta.data,
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

