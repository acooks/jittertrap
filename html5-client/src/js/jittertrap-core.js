/* jittertrap-core.js */

/* global CBuffer */
/* global JT:true */

JT = (function (my) {
  'use strict';

  /* module namespace */
  my.core = {};

  var samplePeriod = JT.coreconfig.samplePeriod;

  /* data sample period; microseconds; fixed. */
  my.core.samplePeriod = function(sp) {
    if (sp) {
      console.log("sample period set to " + sp + " microseconds");
      samplePeriod = sp;
    }
    return samplePeriod;
  };

  /* number of samples to keep for a complete chart series. */
  var sampleWindowSize = 200;
  /* number of data samples. */
  /* FIXME: see about replacing sampleCount with sampleWindowSize */
  var sampleCount = sampleWindowSize;

  my.core.sampleCount = function () {
    return sampleCount;
  };

  /* count must be bytes, samplePeriod is microseconds */
  var byteCountToKbpsRate = function(count) {
    var rate = count / my.core.samplePeriod() * 8000.0 * (my.core.samplePeriod() / 1000);
    return rate;
  };

  var packetDeltaToRate = function(count) {
    return count * (1000000.0 / my.core.samplePeriod()) * (my.core.samplePeriod() / 1000);
  };

  var timeScaleTable = { "5ms": 5, "10ms": 10, "20ms": 20, "50ms": 50, "100ms": 100, "200ms": 200, "500ms": 500, "1000ms": 1000};

  /* a prototype object to encapsulate timeseries data. */
  var Series = function(name, title, ylabel, rateFormatter) {
    this.name = name;
    this.title = title;
    this.ylabel = ylabel;
    this.rateFormatter = rateFormatter;
    this.xlabel = "Time (ms)";
    this.stats = {min: 99999, max:0, median:0, mean:0, maxPG:0, meanPG:0 };
    this.samples = { '5ms': [], '10ms': [], '20ms': [], '50ms': [], '100ms':[], '200ms':[], '500ms': [], '1000ms': []};
    this.pgaps = {};
    for (var ts in timeScaleTable) {
      this.pgaps[ts] = new CBuffer(sampleWindowSize);
    }
 };


  var sBin = {};  // a container (Bin) for series.
  sBin.rxRate = new Series("rxRate",
                           "Ingress Bitrate in kbps",
                           "kbps, mean",
                           byteCountToKbpsRate);

  sBin.txRate = new Series("txRate",
                           "Egress Bitrate in kbps",
                           "kbps, mean",
                           byteCountToKbpsRate);

  sBin.txPacketRate = new Series("txPacketRate",
                                 "Egress packet rate",
                                 "pkts per sec, mean",
                                 packetDeltaToRate);

  sBin.rxPacketRate = new Series("rxPacketRate",
                                 "Ingress packet rate",
                                 "pkts per sec, mean",
                                 packetDeltaToRate);

  var selectedSeriesName = "rxRate";

  my.core.setSelectedSeriesName = function(sName) {
    selectedSeriesName = sName;
  };

  my.core.getSelectedSeries = function () {
    return sBin[selectedSeriesName];
  };

  var resizeCBuf = function(series, len) {

    if (len === sampleCount) {
      return;
    }

    for (var key in timeScaleTable) {
      var b = new CBuffer(len);
      var l = (len < series.samples[key].size) ? len : series.samples[key].size;
      while (l--) {
        b.push(series.samples[key].shift());
      }
      series.samples[key] = b;
      series.pgaps[key] = new CBuffer(len);
    }
  };

  my.core.resizeDataBufs = function(newlen) {

    resizeCBuf(sBin.rxRate, newlen);
    resizeCBuf(sBin.txRate, newlen);

    resizeCBuf(sBin.txPacketRate, newlen);
    resizeCBuf(sBin.rxPacketRate, newlen);

  };

  var clearSeries = function (s) {

    for (var key in timeScaleTable) {
      s.samples[key] = new CBuffer(sampleWindowSize);
      s.pgaps[key].empty();
    }

  };

  my.core.clearAllSeries = function () {
    clearSeries(sBin.txRate);
    clearSeries(sBin.rxRate);
    clearSeries(sBin.txPacketRate);
    clearSeries(sBin.rxPacketRate);
  };

  var numSort = function(a,b) {
    return (a - b)|0;
  };

  var updateBasicStatsChartData = function (stats, chartSeries) {
    if (chartSeries[0]) {
      chartSeries[0].y = stats.min;
      chartSeries[1].y = stats.median;
      chartSeries[2].y = stats.mean;
      chartSeries[3].y = stats.max;
    } else {
      chartSeries.push({x:1, y:stats.min, label:"Min"});
      chartSeries.push({x:2, y:stats.median, label:"Median"});
      chartSeries.push({x:3, y:stats.mean, label:"Mean"});
      chartSeries.push({x:4, y:stats.max, label:"Max"});
    }
  };

  var updatePacketGapChartData = function (data, mean, minMax) {

    var chartPeriod = my.charts.getChartPeriod();
    var len = data.size;

    mean.length = 0;
    minMax.length = 0;

    for (var i = 0; i < len; i++) {
      var x = i * chartPeriod;
      var pg = data.get(i);
      mean.push({x: x, y: pg.mean});
      minMax.push({x: x, y: [pg.min, pg.max]});
      //console.log(x + " " + pg.min + " " + pg.max);
    }
  };

  var updateStats = function (series, timeScale) {
    var sortedData = series.samples[timeScale].slice(0);
    series.stats.cur = sortedData[sortedData.length-1];
    sortedData.sort(numSort);

    series.stats.max = sortedData[sortedData.length-1];
    series.stats.min = sortedData[0];
    series.stats.median = sortedData[Math.floor(sortedData.length / 2.0)];
    var sum = 0;
    var i = 0;

    for (i = sortedData.length-1; i >=0; i--) {
      sum += sortedData[i];
    }
    series.stats.mean = sum / sortedData.length;

    var pg = series.pgaps[timeScale].last();
    series.stats.maxPG = 1.0 * pg.max;
    series.stats.meanPG = 1.0 * pg.mean;

  };

  var updateMainChartData = function(samples, chartSeries) {
    var chartPeriod = my.charts.getChartPeriod();
    var len = samples.size;

    chartSeries.length = 0;

    for (var i = 0; i < len; i++) {
      chartSeries.push({timestamp: i*chartPeriod, value: samples.get(i)});
    }
  };

  var chartSamples = {};

  var updateSampleCounts = function(interval) {
      if (!chartSamples[interval]) chartSamples[interval] = 1;
      else if (chartSamples[interval] < sampleCount) chartSamples[interval]++;
  };

  var updateTopFlowChartData = function(interval) {
    var chartPeriod = my.charts.getChartPeriod();
    var chartSeries = JT.charts.getTopFlowsRef();
    var fcount = (flowRank[interval].length < 10) ?
                 flowRank[interval].length : 10;

    updateSampleCounts(interval);

    console.assert(Number(chartPeriod) > 0);
    console.assert(Number(interval / 1E6) > 0);
    // careful, chartPeriod is a string. interval is in ns, so convert to ms.
    if (Number(chartPeriod) !== Number(interval / 1E6)) {
      // only update chart if selected chartPeriod matches new data message
      return;
    }

    chartSeries.length = 0;

    var slices = flowsTS[interval].size;

    /* get the top 10 from the ranking... */
    for (var j = 0; j < fcount; j++) {
      var fkey = flowRank[interval][j];
      var flow = {"fkey": fkey, "values": []};
      for (var i = 0; i < slices; i++) {
        var slice = flowsTS[interval].get(i);
        /* the data point must exist to keep the series alignment intact */
        var d = {"ts": slice.ts, "bytes":0, "packets":0};
        if (slice[fkey]) {
          d.bytes = slice[fkey].bytes;
          d.packets = slice[fkey].packets;
        }
        console.assert(d.bytes >= 0);
        console.assert(d.packets >= 0);
        flow.values.push(d);
      }
      flow.tbytes = flowsTotals[interval][fkey].tbytes;
      flow.tpackets = flowsTotals[interval][fkey].tpackets;
      chartSeries.push(flow);
    }

  };

  var updateSeries = function (series, yVal, selectedSeries, timeScale) {
    series.samples[timeScale].push(series.rateFormatter(yVal / 1000.0));

    if (my.charts.getChartPeriod() == timeScaleTable[timeScale]) {
      updateStats(series, timeScale);
      JT.measurementsModule.updateSeries(series.name, series.stats);
      JT.trapModule.checkTriggers(series.name, series.stats);

      /* update the charts data */
      if (series.name === selectedSeries.name) {
        updateMainChartData(series.samples[timeScale],
                            JT.charts.getMainChartRef());

        updatePacketGapChartData(series.pgaps[timeScale],
                                 JT.charts.getPacketGapMeanRef(),
                                 JT.charts.getPacketGapMinMaxRef());
      }
    }
  };

  var updateData = function (d, sSeries, timeScale) {
    sBin.rxRate.pgaps[timeScale].push(
      {
        "min"  : d.min_rx_pgap,
        "max"  : d.max_rx_pgap,
        "mean" : d.mean_rx_pgap / 1000.0
      }
    );

    sBin.txRate.pgaps[timeScale].push(
      {
        "min"  : d.min_tx_pgap,
        "max"  : d.max_tx_pgap,
        "mean" : d.mean_tx_pgap / 1000.0
      }
    );

    sBin.rxPacketRate.pgaps[timeScale].push(
      {
        "min"  : d.min_rx_pgap,
        "max"  : d.max_rx_pgap,
        "mean" : d.mean_rx_pgap / 1000.0
      }
    );

    sBin.txPacketRate.pgaps[timeScale].push(
      {
        "min"  : d.min_tx_pgap,
        "max"  : d.max_tx_pgap,
        "mean" : d.mean_tx_pgap / 1000.0
      }
    );

    updateSeries(sBin.txRate, d.tx, sSeries, timeScale);
    updateSeries(sBin.rxRate, d.rx, sSeries, timeScale);
    updateSeries(sBin.txPacketRate, d.txP, sSeries, timeScale);
    updateSeries(sBin.rxPacketRate, d.rxP, sSeries, timeScale);
  };

  my.core.processDataMsg = function (stats, interval) {
    var selectedSeries = sBin[selectedSeriesName];

    switch (interval) {
      case 5000000:
           updateData(stats, selectedSeries, '5ms');
           break;
      case 10000000:
           updateData(stats, selectedSeries, '10ms');
           break;
      case 20000000:
           updateData(stats, selectedSeries, '20ms');
           break;
      case 50000000:
           updateData(stats, selectedSeries, '50ms');
           break;
      case 100000000:
           updateData(stats, selectedSeries, '100ms');
           break;
      case 200000000:
           updateData(stats, selectedSeries, '200ms');
           break;
      case 500000000:
           updateData(stats, selectedSeries, '500ms');
           break;
      case 1000000000:
           updateData(stats, selectedSeries, '1000ms');
           break;
      default:
           console.log("unknown interval: " + interval);
    }
  };

  var flows = {};
  var flowRank = {}; /* a sortable list of flow keys for each interval */

  var flowsTS = {};
  var flowsTotals = {};

  var getFlowKey = function (interval, flow) {
    return interval + '/' + flow.src + '/' + flow.sport + '/' + flow.dst +
           '/' + flow.dport + '/' + flow.proto;
  };

  var msgToFlows = function (msg, timestamp) {
    var interval = msg.interval_ns;
    var fcnt = msg.flows.length;

    /* we haven't seen this interval before, initialise it. */
    if (!flowsTS[interval]) {
      flowsTS[interval] = new CBuffer(sampleWindowSize);
      flowsTotals[interval] = {};
      flowRank[interval] = []; /* sortable! */
    }

    var sample_slice = {};
    sample_slice.ts = timestamp;
    flowsTS[interval].push(sample_slice);

    for (var i = 0; i < fcnt; i++) {
      var fkey = getFlowKey(interval, msg.flows[i]);

      /* create new flow entry if we haven't seen it before */
      if (!flowsTotals[interval][fkey]) {
        flowsTotals[interval][fkey] = {
          'ttl': sampleWindowSize,
          'tbytes': 0,
          'tpackets': 0
        };
        flowRank[interval].push(fkey);
      }

      /* set bytes, packets for this (intervalSize,timeSlice,flow)  */
      sample_slice[fkey] =
        {"bytes": msg.flows[i].bytes, "packets": msg.flows[i].packets};

      /* reset the time-to-live to the chart window length (in samples),
       * so that it can be removed when it ages beyond the window. */
      flowsTotals[interval][fkey].ttl = sampleWindowSize;
      /* update totals for the flow */
      flowsTotals[interval][fkey].tbytes += msg.flows[i].bytes;
      flowsTotals[interval][fkey].tpackets += msg.flows[i].packets;

      console.assert(
        ((flowsTotals[interval][fkey].tbytes === 0)
         && (flowsTotals[interval][fkey].tpackets === 0))
        ||
        ((flowsTotals[interval][fkey].tbytes != 0)
         && (flowsTotals[interval][fkey].tpackets != 0)
        )
      );

      /* update flow ranks table, descending order */
      flowRank[interval].sort(function (a, b) {
        return flowsTotals[interval][b].tbytes -
               flowsTotals[interval][a].tbytes;
      });
    }
  };


  /* reduce the time-to-live for the flow and expire it when no samples are
   * within the visible chart window */
  var expireOldFlowsAndUpdateRank = function (interval) {
    var fkey;
    var ft = flowsTotals[interval];
    for (fkey in ft) {
      if (ft.hasOwnProperty(fkey)) {
        ft[fkey].ttl -= 1;

        if (ft[fkey].ttl <= 0) {
          delete ft[fkey];
          flowRank[interval] = flowRank[interval].filter(function (o) {
             return (o !== fkey);
          });
        }
      }
    }

    /* We must have the same number of flow keys in the flowsTotals and
     * flowRank accounting tables... */
    console.assert(flowRank[interval].length ==
                   Object.keys(flowsTotals[interval]).length);

    /* Remember: each TCP flow has a return flow, but UDP may or may not! */
  };

  my.core.processTopTalkMsg = function (msg) {
    var interval = msg.interval_ns;
    var tstamp = Number(msg.timestamp.tv_sec + "." + msg.timestamp.tv_nsec);

    console.assert(!(Number.isNaN(tstamp)));

    msgToFlows(msg, tstamp);
    expireOldFlowsAndUpdateRank(interval);
    updateTopFlowChartData(interval);

    switch (interval) {
      case 5000000:
      case 10000000:
      case 20000000:
      case 50000000:
      case 100000000:
      case 200000000:
      case 500000000:
           break;
      case 1000000000:
           /* insert debug logging here */
           console.log("[processTopTalkMsg] interval === " + interval +
                       " msg.timestamp:" + msg.timestamp.tv_sec + "." +
                         + msg.timestamp.tv_nsec);
           console.log("flowsTotals["+interval+"]: " +
                       JSON.stringify(flowsTotals[interval]));
           break;
      default:
           console.log("unknown interval: " + interval);
           return;
    }
  };

  return my;
}(JT));
/* End of jittertrap-core.js */
