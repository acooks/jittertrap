/*
 * ==== The JitterTrap Module ====
 */

/* global JT: true */
JT = {};

/*
 * ==== Raw Data ====
 */
JT = (function (my) {
  'use strict';
  my.rawData = {};
  var rd = my.rawData;

  var samplePeriod = 1000;

  /* raw data sample period; microseconds; fixed. */
  rd.samplePeriod = function(sp) {
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

  rd.sampleCount = function (plotPeriod) {
    if (plotPeriod) {
      sampleCount = Math.floor(dataLengthMultiplier * plotPeriod);
    }
    return sampleCount;
  };


  rd.byteDelta = {
    tx: [],
    rx: []
  };

  rd.byteRate = {
    tx: [],
    rx: []
  };

  rd.pktDelta = {
    tx: [],
    rx: []
  };

  rd.pktRate = {
    tx: [],
    rx: []
  };

  return my;
}(JT));


/*
 * ==== Charts ====
 */
JT = (function (my) {
  'use strict';
  my.charts = {};

    /* Add a container for charting parameters */
  my.charts.params = {};

  /* time (milliseconds) represented by each data point */
  my.charts.params.plotPeriod        = 60;
  my.charts.params.plotPeriodMin     = 1;
  my.charts.params.plotPeriodMax     = 500;

  /* chart redraw/refresh/updates; milliseconds; 40ms == 25 Hz */
  my.charts.params.redrawPeriod      = 60;
  my.charts.params.redrawPeriodMin   = 40;
  my.charts.params.redrawPeriodMax   = 100;
  my.charts.params.redrawPeriodSaved = 0;

  return my;
}(JT));
/* end of jittertrap-globals.js */
