$(document).ready(function() {
        var chartData = {};

        chartData.txDelta = {
          data:[],
          histData:[],
          title:"Tx Bytes per sample period",
          ylabel:"Tx Bytes per sample",
          xlabel:"Time",
          minY: {x: 0, y: 0},
          maxY: {x: 0, y: 0},
          basicStats:[]
        };

        chartData.rxDelta = {
          data:[],
          histData:[],
          title:"Rx Bytes per sample period",
          ylabel:"Rx Bytes per sample",
          xlabel:"Time",
          minY: {x: 0, y: 0},
          maxY: {x: 0, y: 0},
          basicStats:[]
        };

        chartData.rxRate = {
          data:[],
          histData:[],
          title: "Ingress throughput in kbps",
          ylabel:"kbps, mean",
          xlabel:"sample number",
          minY: {x: 0, y: 0},
          maxY: {x: 0, y: 0},
          basicStats:[]
        };

        chartData.txRate = {
          data:[],
          histData:[],
          title: "Egress throughput in kbps",
          ylabel:"kbps, mean",
          xlabel:"sample number",
          minY: {x: 0, y: 0},
          maxY: {x: 0, y: 0},
          basicStats:[]
        };

        chartData.txPacketRate = {
          data:[],
          histData:[],
          title: "Egress packet rate",
          ylabel:"pkts per sec, mean",
          xlabel:"time",
          minY: {x: 0, y: 0},
          maxY: {x: 0, y: 0},
          basicStats:[]
        };

        chartData.rxPacketRate = {
          data:[],
          histData:[],
          title: "Ingress packet rate",
          ylabel:"pkts per sec, mean",
          xlabel:"time",
          minY: {x: 0, y: 0},
          maxY: {x: 0, y: 0},
          basicStats:[]
        };

        chartData.txPacketDelta = {
          data:[],
          histData:[],
          title: "Egress packets per sample",
          ylabel:"packets sent",
          xlabel:"sample number",
          minY: {x: 0, y: 0},
          maxY: {x: 0, y: 0},
          basicStats:[]
        };

        chartData.rxPacketDelta = {
          data:[],
          histData:[],
          title: "Ingress packets per sample",
          ylabel:"packets received",
          xlabel:"sample number",
          minY: {x: 0, y: 0},
          maxY: {x: 0, y: 0},
          basicStats:[]
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
        var dataLength = 1000; // number of dataPoints visible at any point
        var updatePeriod = 100;
        var samplePeriod = 100;

        var chart = new CanvasJS.Chart("chartContainer", {});
        var histogram = new CanvasJS.Chart("histogramContainer", {});
        var basicStatsGraph = new CanvasJS.Chart("basicStatsContainer", {});

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

          histogram = new CanvasJS.Chart("histogramContainer", {
            title: {text: "Distribution" },
            axisY: {
              title: "Count",
              includeZero: "false",
            },
            axisX: {
              title: "Bin",
              includeZero: "false",
            },
            data: [{
              name: s + "_hist",
              type: "column",
              dataPoints: chartData[s].histData
            }]
          });
          histogram.render();

          basicStatsGraph = new CanvasJS.Chart("basicStatsContainer", {
            title: { text: "Basic Stats" },
            axisY: {
              includeZero: "false",
              title: chartData[s].ylabel
            },
            data: [{
              name: s + "_stats",
              type: "column",
              dataPoints: chartData[s].basicStats
            }]
          });
          basicStatsGraph.render();

        };

        var clearChart = function() {
          chartData.txDelta.data = [];
          chartData.txDelta.histData = [];

          chartData.rxDelta.data = [];
          chartData.rxDelta.histData = [];

          chartData.rxRate.data = [];
          chartData.rxRate.histData = [];

          chartData.txRate.data = [];
          chartData.txRate.histData = [];

          chartData.txPacketRate.data = [];
          chartData.txPacketRate.histData = [];

          chartData.rxPacketRate.data = [];
          chartData.rxPacketRate.histData = [];

          chartData.rxPacketDelta.data = [];
          chartData.rxPacketDelta.histData = [];

          chartData.txPacketDelta.data = [];
          chartData.txPacketDelta.histData = [];
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
          drawInterval = setInterval(function() {
                                       histogram.render();
                                       basicStatsGraph.render();
                                       chart.render();
                                     },
                                     updatePeriod);
          console.log("updateRate: " + updateRate + " sampleRate: " + sampleRate);
        };

        $("#chopts_refresh").val(millisecondsToRate(updatePeriod));
        
        $("#chopts_dataLen").val(dataLength);

        var checkTriggers = function() {
          ;
        };

        var drawInterval = setInterval(function() {
                                         histogram.render();
                                         basicStatsGraph.render();
                                         chart.render();
                                       },
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
          samplePeriod = $("#sample_period").val();
          var msg = JSON.stringify(
            {'msg': 'set_sample_period',
             'period': samplePeriod
            });
          websocket.send(msg);
        };

        var updateStats = function (series) {
          var sortedData = series.data.slice(0);
          sortedData.sort(function(a,b) {return (a.y|0) - (b.y|0);});

          /* series.maxY and series.minY must be available to the histogram */
          series.maxY = sortedData[sortedData.length-1];
          series.minY = sortedData[0];

          /* median is a pair */
          var median = sortedData[Math.floor(sortedData.length / 2.0)];

          var mean = 0;
          var sum = 0;
          for (var i = sortedData.length-1; i >=0; i--) {
            sum += sortedData[i].y;
          }
          mean = sum / sortedData.length;

          for (var i = series.basicStats.length; i > 0; i--) {
            series.basicStats.shift();
          }

          series.basicStats.push({x:1, y:series.minY.y, label:"Min"});
          series.basicStats.push({x:2, y:median.y, label:"Median"});
          series.basicStats.push({x:3, y:mean, label:"Mean"});
          series.basicStats.push({x:4, y:series.maxY.y, label:"Max"});
        };

        var updateHistogram = function(series) {
          var binCnt = 20;
          var normBins = [];
          var range = series.maxY.y - series.minY.y;

          /* bins must use integer indexes, so we have to normalise the
           * data and then convert it back before display.
           * [0,1) falls into bin[0] */
          var i = 0;
          var j = 0;

          /* initialise the bins */
          for (; i < binCnt; i++) {
            normBins[i] = 0;
            series.histData.shift();
          }

          /* bin the normalized data */
          for (; j < series.data.length; j++) {
            var normY = (series.data[j].y - series.minY.y) / range * binCnt;
            normBins[Math.floor(normY)]++;
          }

          /* write the histogram x,y data */
          var newdata = [];
          for (i = 0; i < binCnt; i++) {
            var xVal = Math.ceil(i * (series.maxY.y / binCnt));
            xVal += series.minY.y;  /* shift x to match original y range */
            series.histData.push({x: xVal, y: normBins[i], label: xVal});
          }
        };

        var logHistogram = function () {
          var s = $("#chopts_series option:selected").val();
          for (var i = 0; i < chartData[s].histData.length; i++) {
            console.log(chartData[s].histData[i]);
          }
        };

        var updateSeries = function (series, xVal, yVal) {
          series.data.push({ x: xVal, y: yVal });
          while (series.data.length > dataLength) {
            series.data.shift();
          }
          updateStats(series);
          updateHistogram(series);
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

