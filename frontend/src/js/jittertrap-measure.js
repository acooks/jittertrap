/* jittertrap-measure.js */

/* global JT: true */

JT = (function (my) {
  'use strict';
  my.measurementsModule = {};
  var measurements = {};

  measurements.rxRate = {};
  measurements.rxRate.min = 9999999;
  measurements.rxRate.max = 0;
  measurements.rxRate.mean = 9999999;

  measurements.txRate = {};
  measurements.txRate.min = 9999999;
  measurements.txRate.max = 0;
  measurements.txRate.mean = 9999999;

  measurements.rxPacketRate = {};
  measurements.rxPacketRate.min = 9999999;
  measurements.rxPacketRate.max = 0;
  measurements.rxPacketRate.mean = 9999999;

  measurements.txPacketRate = {};
  measurements.txPacketRate.min = 9999999;
  measurements.txPacketRate.max = 0;
  measurements.txPacketRate.mean = 9999999;

  measurements.rxZRun = {};
  measurements.rxZRun.max = 0;
  measurements.rxZRun.mean = "TODO";

  measurements.txZRun = {};
  measurements.txZRun.max = 0;
  measurements.txZRun.mean = "TODO";

  var updateDOM = function () {

    $("#jt-measure-tput-min-rx").html(measurements.rxRate.min);
    $("#jt-measure-tput-max-rx").html(measurements.rxRate.max);
    $("#jt-measure-tput-mean-rx").html(measurements.rxRate.mean);

    $("#jt-measure-tput-min-tx").html(measurements.txRate.min);
    $("#jt-measure-tput-max-tx").html(measurements.txRate.max);
    $("#jt-measure-tput-mean-tx").html(measurements.txRate.mean);

    $("#jt-measure-pktRate-min-rx").html(measurements.rxPacketRate.min);
    $("#jt-measure-pktRate-max-rx").html(measurements.rxPacketRate.max);
    $("#jt-measure-pktRate-mean-rx").html(measurements.rxPacketRate.mean);

    $("#jt-measure-pktRate-min-tx").html(measurements.txPacketRate.min);
    $("#jt-measure-pktRate-max-tx").html(measurements.txPacketRate.max);
    $("#jt-measure-pktRate-mean-tx").html(measurements.txPacketRate.mean);

    $("#jt-measure-zRun-max-rx").html(measurements.rxZRun.max);
    $("#jt-measure-zRun-mean-rx").html(measurements.rxZRun.mean);

    $("#jt-measure-zRun-max-rx").html(measurements.rxRate.maxZ);
    $("#jt-measure-zRun-max-tx").html(measurements.txRate.maxZ);
    $("#jt-measure-zRun-mean-tx").html(measurements.txZRun.mean);
  };

  var drawIntervalID = setInterval(updateDOM, 100);

  my.measurementsModule.updateSeries = function (series, min, max, mean, maxZ) {

    if (!measurements[series]) {
      measurements[series] = {};
    }
    measurements[series].min = min.toFixed(2);
    measurements[series].max = max.toFixed(2);
    measurements[series].mean = mean.toFixed(2);
    measurements[series].maxZ = maxZ;
  };

  return my;
}(JT));
/* end of jittertrap-measure.js */
