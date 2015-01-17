$(document).ready(function() {
        var chartData = {};
        chartData.txDelta = {
          data:[],
          title:"Tx Bytes per sample period",
          ylabel:"Tx Bytes, delta",
          xlabel:"Time"
        };
        chartData.rxDelta = {
          data:[],
          title:"Rx Bytes per sample period",
          ylabel:"Rx Bytes, delta",
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
          title: "Egress packet Delta",
          ylabel:"packets sent",
          xlabel:"sample number",
        };
        chartData.rxPacketDelta = {
          data:[],
          title: "Ingress packet Delta",
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

        var toggleStopStartGraph = function() {
          var maxUpdatePeriod = 9999999999;
          if (updatePeriod  != maxUpdatePeriod) {
            old_updatePeriod = updatePeriod;
            updatePeriod = maxUpdatePeriod;
          } else {
            updatePeriod = old_updatePeriod;
          }
          setUpdatePeriod();
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
        }
        var setUpdatePeriod = function() {
          var sampleRate = millisecondsToRate(parseInt($("#sample_period").val()));
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
          console.log("updateRate: " + updateRate
                      + " sampleRate: " + sampleRate);
        };
        $('#chopts_stop_start').bind('click', toggleStopStartGraph);
        $('#chopts_refresh').bind('change',
           function() {
             updatePeriod = rateToMilliseconds($("#chopts_refresh").val());
             setUpdatePeriod();
           }
        );
        $("#chopts_refresh").val(millisecondsToRate(updatePeriod));
        
        $("#chopts_dataLen").bind('change',
           function() {
             dataLength = $("#chopts_dataLen").val();
           });
        $("#chopts_dataLen").val(dataLength);

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

        $("#chopts_series").bind('change', resetChart);
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

        /* count must be bytes, duration must be milliseconds */
        var byteCountToKbpsRate = function(count) {
          var period = parseInt($("#sample_period").val());
          var rate = count * (1000.0 / period) * 8.0 / 1000.0;
          return rate;
        };
       var packetDeltaToRate = function(count) {
          var period = parseInt($("#sample_period").val());
          return count * (1000.0 / period);
        };

        var checkTriggers = function() {
          ;
        };

        $("#dev_select").bind('change', clearChart);

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
          var samplePeriod = parseInt($("#sample_period").val());
          if (msg["stats"] && msg.stats.iface == $('#dev_select').val()) {
            updateSeries(chartData.txDelta.data,
                         xVal * samplePeriod,
                         msg.stats["tx-delta"]);
            updateSeries(chartData.rxDelta.data,
                         xVal * samplePeriod,
                         msg.stats["rx-delta"]);
            updateSeries(chartData.txRate.data,
                         xVal,
                         byteCountToKbpsRate(msg.stats["tx-delta"]));
            updateSeries(chartData.rxRate.data,
                         xVal,
                         byteCountToKbpsRate(msg.stats["rx-delta"]));
            updateSeries(chartData.txPacketRate.data,
                         xVal,
                         packetDeltaToRate(msg.stats["tx-pkt-delta"]));
            updateSeries(chartData.rxPacketRate.data,
                         xVal,
                         packetDeltaToRate(msg.stats["rx-pkt-delta"]));
            updateSeries(chartData.txPacketDelta.data,
                         xVal,
                         msg.stats["tx-pkt-delta"]);
            updateSeries(chartData.rxPacketDelta.data,
                         xVal,
                         msg.stats["rx-pkt-delta"]);
            xVal++;
            checkTriggers();
          } else if (msg["ifaces"]) {
            $('#dev_select').empty();
            $.each(msg.ifaces,
              function (ix, val) {
                var option = $('<option>').text(val).val(val);
                $('#dev_select').append(option);
              }
            );
            dev_select();
          } else if (msg["netem_params"]) {
            if (msg.netem_params.delay == -1
                && msg.netem_params.jitter == -1
                && msg.netem_params.loss == -1) {
              $("#netem_status").html("Netem not active on device. Set parameters to activate.");
              $("#delay").val("None");
              $("#jitter").val("None");
              $("#loss").val("None");
            } else {
              $("#netem_status").html("Ready");
              $("#delay").val(msg.netem_params.delay + "ms");
              $("#jitter").val(msg.netem_params.jitter + "ms");
              $("#loss").val(msg.netem_params.loss + "%");
            }
          } else if (msg["sample_period"]) {
            var foo = $("#sample_period").val();
            $("#sample_period").val(msg.sample_period + "ms");
            console.log("sample_period: " + msg.sample_period);
            setUpdatePeriod();
            clearChart();
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
        };
        $('#set_netem_button').bind('click', set_netem);

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
        $('#sample_period').bind('change', set_sample_period);

        $('#dev_select').bind('change', dev_select);

        var updateSeries = function (series, xVal, yVal) {
          series.push({ x: xVal, y: yVal });
          while (series.length > dataLength) {
            series.shift();
          }
        };

        $('#more_chopts_toggle').click(function() {
          $('#more_chopts').toggle("fast");
        });

        $('#trigger_toggle').click(function() {
          $('#trigger_chopts').toggle("fast");
        });

        $('#help_toggle').click(function() {
          $('#help').toggle("fast");
        });

});

