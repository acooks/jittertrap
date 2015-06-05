/* jittertrap-core.js */

/* global CBuffer */
/* global JT:true */

JT = (function (my) {
  'use strict';

  /* module namespace */
  my.core = {};

  var samplePeriod = 1000;

  /* raw data sample period; microseconds; fixed. */
  my.core.samplePeriod = function(sp) {
    if (sp) {
      console.log("sample period set to " + sp + " microseconds");
      samplePeriod = sp;
    }
    return samplePeriod;
  };

  /* scaling factor for number of raw data points */
  var dataLengthMultiplier = 300;

  /* number of raw data samples. */
  var sampleCount = 18000;

  my.core.sampleCount = function (plotPeriod) {
    if (plotPeriod) {
      sampleCount = Math.floor(dataLengthMultiplier * plotPeriod);
    }
    return sampleCount;
  };

  /* a prototype object to encapsulate timeseries data. */
  var Series = function(name, title, ylabel) {
    this.name = name;
    this.title = title;
    this.ylabel = ylabel;
    this.xlabel = "Time (ms)";
    this.data = []; // raw samples
    this.filteredData = []; // filtered & decimated to chartingPeriod
    this.histData = [];
    this.basicStats = [];
    this.minY = {x:0, y:0};
    this.maxY = {x:0, y:0};
  };

  my.core.series = {};
  my.core.series.txDelta = new Series("txDelta",
                                 "Tx Bytes per sample period",
                                 "Tx Bytes per sample");

  my.core.series.rxDelta = new Series("rxDelta",
                                 "Rx Bytes per sample period",
                                 "Rx Bytes per sample");

  my.core.series.rxRate = new Series("rxRate",
                                "Ingress throughput in kbps",
                                "kbps, mean");

  my.core.series.txRate = new Series("txRate",
                                "Egress throughput in kbps",
                                "kbps, mean");

  my.core.series.txPacketRate = new Series("txPacketRate",
                                      "Egress packet rate",
                                      "pkts per sec, mean");

  my.core.series.rxPacketRate = new Series("rxPacketRate",
                                      "Ingress packet rate",
                                      "pkts per sec, mean");

  my.core.series.txPacketDelta = new Series("txPacketDelta",
                                       "Egress packets per sample",
                                       "packets sent");

  my.core.series.rxPacketDelta = new Series("rxPacketDelta",
                                       "Ingress packets per sample",
                                       "packets received");


  var resizeCBuf = function(cbuf, len) {
    cbuf.filteredData = [];
    var b = new CBuffer(len);

    var l = (len < cbuf.data.size) ? len : cbuf.data.size;
    while (l--) {
      b.push(cbuf.data.shift());
    }
    cbuf.data = b;
  };

  my.core.resizeDataBufs = function(newlen) {

    /* local alias for brevity */
    var s = my.core.series;

    resizeCBuf(s.txDelta, newlen);
    resizeCBuf(s.rxDelta, newlen);

    resizeCBuf(s.rxRate, newlen);
    resizeCBuf(s.txRate, newlen);

    resizeCBuf(s.txPacketRate, newlen);
    resizeCBuf(s.rxPacketRate, newlen);

    resizeCBuf(s.txPacketDelta, newlen);
    resizeCBuf(s.rxPacketDelta, newlen);
  };

  var clearSeries = function (s) {
    s.data = new CBuffer(my.core.sampleCount());
    s.filteredData = [];
    s.histData = [];
  };

  my.core.clearAllSeries = function () {
    var s = my.core.series; /* local alias for brevity */

    clearSeries(s.txDelta);
    clearSeries(s.rxDelta);
    clearSeries(s.txRate);
    clearSeries(s.rxRate);
    clearSeries(s.txPacketRate);
    clearSeries(s.rxPacketRate);
    clearSeries(s.txPacketDelta);
    clearSeries(s.rxPacketDelta);
  };

  return my;
}(JT));
/* end jittertrap-core.js */
