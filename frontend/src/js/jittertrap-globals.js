/*
 * ==== The JitterTrap Module ====
 */

/* global CBuffer */
/* global CanvasJS */
/* global Mustache */

/* global JT: true */
JT = {};

/*
 * ==== Raw Data ====
 */
JT = (function (my) {
  'use strict';
  my.rawData = {};
  var rd = my.rawData;

  rd.xVal = 0;

  /* raw data sample period; microseconds; fixed. */
  rd.samplePeriod = 1000;

  /* scaling factor for number of raw data points */
  rd.dataLengthMultiplier = 300;

  /* number of raw data samples.
   * dataLength = chartParams.plotPeriod * dataLengthMultiplier
   */
  rd.dataLength = 18000;

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
 * ==== Measurements ====
 */
JT = (function (my) {
  'use strict';

  my.measurements = {

    /* Throughput */
    minTput:  { tx: 999999, rx: 99999 },
    maxTput:  { tx: 0, rx: 0 },
    meanTput: { tx: 0, rx: 0 },

    /* Packet Rate */
    minPktRate:  { tx: 99999, rx: 99999 },
    maxPktRate:  { tx: 0, rx: 0 },
    meanPktRate: { tx: 0, rx: 0 },

    /* Zero Runs */
    maxZRun:  { tx: 0, rx: 0 },
    meanZRun: { tx: 0, rx: 0 }
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
