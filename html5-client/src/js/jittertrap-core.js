/* jittertrap-core.js */

/* global CBuffer */
/* global JT:true */

((my) => {
  'use strict';

  /* module namespace */
  my.core = {};

  let samplePeriod = JT.coreconfig.samplePeriod;

  /* data sample period; microseconds; fixed (backend sampling rate). */
  my.core.samplePeriod = function(sp) {
    if (sp) {
      console.log("sample period set to " + sp + " microseconds");
      samplePeriod = sp;
    }
    return samplePeriod;
  };

  /* number of samples to keep for a complete chart series. */
  const sampleWindowSize = 200;
  /* number of data samples. */
  /* FIXME: see about replacing sampleCount with sampleWindowSize */
  const sampleCount = sampleWindowSize;

  my.core.sampleCount = function () {
    return sampleCount;
  };

  /* count is bytes/sec (from server) */
  const byteCountToBpsRate = function(count) {
    // count is Bps. Multiply by 8 to get bps.
    const rate = count * 8.0;
    return rate;
  };

  /* count is packets/sec (from server) */
  const packetDeltaToPpsRate = function(count) {
    // count is Pps. No conversion needed.
    const rate = count;
    return rate;
  };

  const timeScaleTable = { "5ms": 5, "10ms": 10, "20ms": 20, "50ms": 50, "100ms": 100, "200ms": 200, "500ms": 500, "1000ms": 1000};

  /* a prototype object to encapsulate timeseries data. */
  const Series = function(name, title, ylabel, rateFormatter) {
    this.name = name;
    this.title = title;
    this.ylabel = ylabel;
    this.rateFormatter = rateFormatter;
    this.xlabel = "Time (ms)";
    this.stats = {min: 99999, max:0, median:0, mean:0, maxPG:0, meanPG:0 };
    this.samples = { '5ms': [], '10ms': [], '20ms': [], '50ms': [], '100ms':[], '200ms':[], '500ms': [], '1000ms': []};
    this.pgaps = {};
    for (const ts in timeScaleTable) {
      this.pgaps[ts] = new CBuffer(sampleWindowSize);
    }
 };


  const sBin = {};  // a container (Bin) for series.
  sBin.rxRate = new Series("rxRate",
                           "Ingress Bitrate (bps)",
                           "Bitrate",
                           byteCountToBpsRate);

  sBin.txRate = new Series("txRate",
                           "Egress Bitrate (bps)",
                           "Bitrate",
                           byteCountToBpsRate);

  sBin.txPacketRate = new Series("txPacketRate",
                                 "Egress Packet Rate",
                                 "Packet Rate",
                                 packetDeltaToPpsRate);

  sBin.rxPacketRate = new Series("rxPacketRate",
                                 "Ingress Packet Rate",
                                 "Packet Rate",
                                 packetDeltaToPpsRate);

  let selectedSeriesName = "rxRate";

  my.core.setSelectedSeriesName = function(sName) {
    selectedSeriesName = sName;
  };

  my.core.getSelectedSeries = function () {
    return sBin[selectedSeriesName];
  };

  const resizeCBuf = function(series, len) {

    if (len === sampleCount) {
      return;
    }

    for (const key in timeScaleTable) {
      const b = new CBuffer(len);
      let l = (len < series.samples[key].size) ? len : series.samples[key].size;
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

  const clearSeries = function (s) {

    for (const key in timeScaleTable) {
      s.samples[key] = new CBuffer(sampleWindowSize);
      s.pgaps[key].empty();
    }

  };

  my.core.clearAllSeries = function () {
    clearSeries(sBin.txRate);
    clearSeries(sBin.rxRate);
    clearSeries(sBin.txPacketRate);
    clearSeries(sBin.rxPacketRate);

    clearFlows();
  };

  const numSort = function(a,b) {
    return (a - b)|0;
  };

  const updateBasicStatsChartData = function (stats, chartSeries) {
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

  const updatePacketGapChartData = function (data, mean, minMax) {

    const chartPeriod = my.charts.getChartPeriod();
    const len = data.size;

    mean.length = 0;
    minMax.length = 0;

    for (let i = 0; i < len; i++) {
      const x = i * chartPeriod;
      const pg = data.get(i);
      mean.push({x: x, y: pg.mean});
      minMax.push({x: x, y: [pg.min, pg.max]});
      //console.log(x + " " + pg.min + " " + pg.max);
    }
  };

  const updateStats = function (series, timeScale) {
    const sortedData = series.samples[timeScale].slice(0);
    series.stats.cur = sortedData[sortedData.length-1];
    sortedData.sort(numSort);

    series.stats.max = sortedData[sortedData.length-1];
    series.stats.min = sortedData[0];
    series.stats.median = sortedData[Math.floor(sortedData.length / 2.0)];
    let sum = 0;
    let i = 0;

    for (i = sortedData.length-1; i >=0; i--) {
      sum += sortedData[i];
    }
    series.stats.mean = sum / sortedData.length;

    const pg = series.pgaps[timeScale].last();
    series.stats.maxPG = 1.0 * pg.max;
    series.stats.meanPG = 1.0 * pg.mean;

  };

  const updateMainChartData = function(samples, chartSeries) {
    const chartPeriod = my.charts.getChartPeriod();
    const len = samples.size;

    chartSeries.length = 0;

    for (let i = 0; i < len; i++) {
      chartSeries.push({timestamp: i*chartPeriod, value: samples.get(i)});
    }
  };

  const chartSamples = {};

  const updateSampleCounts = function(interval) {
      if (!chartSamples[interval]) chartSamples[interval] = 1;
      else if (chartSamples[interval] < sampleCount) chartSamples[interval]++;
  };

  const updateTopFlowChartData = function(interval) {
    const chartPeriod = my.charts.getChartPeriod();
    const chartSeries = JT.charts.getTopFlowsRef();
    const fcount = flowRank[interval].length;

    updateSampleCounts(interval);

    console.assert(Number(chartPeriod) > 0);
    console.assert(Number(interval / 1E6) > 0);
    // careful, chartPeriod is a string. interval is in ns, so convert to ms.
    if (Number(chartPeriod) !== Number(interval / 1E6)) {
      // only update chart if selected chartPeriod matches new data message
      return;
    }

    chartSeries.length = 0;

    const slices = flowsTS[interval].size;

    /* get the top 10 from the ranking... */
    for (let j = 0; j < fcount; j++) {
      const fkey = flowRank[interval][j];
      const flow = {"fkey": fkey, "values": []};
      let lastRtt = -1;  /* Track latest RTT for this flow */
      let lastTcpState = -1;  /* Track latest TCP state for this flow */
      let sawSyn = 0;  /* Track if SYN was ever observed for this flow */
      for (let i = 0; i < slices; i++) {
        const slice = flowsTS[interval].get(i);
        /* the data point must exist to keep the series alignment intact */
        const d = {
          "ts": slice.ts, "bytes":0, "packets":0,
          "rtt_us": -1, "tcp_state": -1,
          "rwnd_bytes": -1, "window_scale": -1,
          "zero_window_cnt": 0, "dup_ack_cnt": 0, "retransmit_cnt": 0,
          "ece_cnt": 0, "recent_events": 0
        };
        if (slice[fkey]) {
          d.bytes = slice[fkey].bytes;
          d.packets = slice[fkey].packets;
          d.rtt_us = slice[fkey].rtt_us;
          d.tcp_state = slice[fkey].tcp_state;
          d.rwnd_bytes = slice[fkey].rwnd_bytes;
          d.window_scale = slice[fkey].window_scale;
          d.zero_window_cnt = slice[fkey].zero_window_cnt;
          d.dup_ack_cnt = slice[fkey].dup_ack_cnt;
          d.retransmit_cnt = slice[fkey].retransmit_cnt;
          d.ece_cnt = slice[fkey].ece_cnt;
          d.recent_events = slice[fkey].recent_events;
          if (d.rtt_us >= 0) {
            lastRtt = d.rtt_us;
          }
          if (d.tcp_state >= 0) {
            lastTcpState = d.tcp_state;
          }
          if (slice[fkey].saw_syn) {
            sawSyn = 1;
          }
        }
        console.assert(d.bytes >= 0);
        console.assert(d.packets >= 0);
        flow.values.push(d);
      }
      flow.tbytes = flowsTotals[interval][fkey].tbytes;
      flow.tpackets = flowsTotals[interval][fkey].tpackets;
      flow.rtt_us = lastRtt;  /* Latest RTT value for legend display */
      flow.tcp_state = lastTcpState;  /* Latest TCP state */
      flow.saw_syn = sawSyn;  /* True if SYN was ever observed */
      chartSeries.push(flow);
    }

  };

  const updateSeries = function (series, yVal, selectedSeries, timeScale) {
    const periodMs = timeScaleTable[timeScale];
    series.samples[timeScale].push(series.rateFormatter(yVal));

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

  const updateData = function (d, sSeries, timeScale) {
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
    const selectedSeries = sBin[selectedSeriesName];

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

  /***** Top Flows follows *****/

  let flows = {};
  let flowRank = {}; /* a sortable list of flow keys for each interval */

  let flowsTS = {};
  let flowsTotals = {};

  /* discard all previous flow data, like when changing capture interface */
  const clearFlows = function () {
    flows = {};
    flowRank = {};
    flowsTS = {};
    flowsTotals = {};
  };

  const getFlowKey = function (interval, flow) {
    return interval + '/' + flow.src + '/' + flow.sport + '/' + flow.dst +
           '/' + flow.dport + '/' + flow.proto + '/' + flow.tclass;
  };

  const msgToFlows = function (msg, timestamp) {
    const interval = msg.interval_ns;
    const fcnt = msg.flows.length;

    /* we haven't seen this interval before, initialise it. */
    if (!flowsTS[interval]) {
      flowsTS[interval] = new CBuffer(sampleWindowSize);
      flowsTotals[interval] = {};
      flowRank[interval] = []; /* sortable! */
    }

    const sample_slice = {};
    sample_slice.ts = timestamp;
    flowsTS[interval].push(sample_slice);

    for (let i = 0; i < fcnt; i++) {
      const fkey = getFlowKey(interval, msg.flows[i]);

      /* create new flow entry if we haven't seen it before */
      if (!flowsTotals[interval][fkey]) {
        flowsTotals[interval][fkey] = {
          'ttl': sampleWindowSize,
          'tbytes': 0,
          'tpackets': 0
        };
        flowRank[interval].push(fkey);
      }

      /* set bytes, packets, rtt, tcp_state, window for this (intervalSize,timeSlice,flow)  */
      sample_slice[fkey] = {
        "bytes": msg.flows[i].bytes,
        "packets": msg.flows[i].packets,
        "rtt_us": msg.flows[i].rtt_us,
        "tcp_state": msg.flows[i].tcp_state,
        "saw_syn": msg.flows[i].saw_syn,
        "rwnd_bytes": msg.flows[i].rwnd_bytes,
        "window_scale": msg.flows[i].window_scale,
        "zero_window_cnt": msg.flows[i].zero_window_cnt,
        "dup_ack_cnt": msg.flows[i].dup_ack_cnt,
        "retransmit_cnt": msg.flows[i].retransmit_cnt,
        "ece_cnt": msg.flows[i].ece_cnt,
        "recent_events": msg.flows[i].recent_events
      };

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
      flowRank[interval].sort((a, b) =>
        flowsTotals[interval][b].tbytes - flowsTotals[interval][a].tbytes);
    }
  };


  /* reduce the time-to-live for the flow and expire it when no samples are
   * within the visible chart window */
  const expireOldFlowsAndUpdateRank = function (interval) {
    let fkey;
    let ft = flowsTotals[interval];
    for (fkey in ft) {
      if (ft.hasOwnProperty(fkey)) {
        ft[fkey].ttl -= 1;

        if (ft[fkey].ttl <= 0) {
          delete ft[fkey];
          flowRank[interval] = flowRank[interval].filter(o => o !== fkey);
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
    let interval = msg.interval_ns;
    let tstamp = msg.timestamp.tv_sec + msg.timestamp.tv_nsec / 1E9;

    console.assert(!(Number.isNaN(tstamp)));

    msgToFlows(msg, tstamp);
    expireOldFlowsAndUpdateRank(interval);
    updateTopFlowChartData(interval);

    return;
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

})(JT);
/* End of jittertrap-core.js */
