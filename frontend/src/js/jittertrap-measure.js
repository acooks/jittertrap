/* jittertrap-measure.js */

/* global JT: true */

JT = (function (my) {
  'use strict';
  my.measurementsModule = {};
  var measurements = {};

  measurements.rxRate = {};
  measurements.txRate = {};
  measurements.rxPacketRate = {};
  measurements.txPacketRate = {};

  var updateTputDOM = function () {
    $("#jt-measure-tput-min-rx").html(measurements.rxRate.min);
    $("#jt-measure-tput-max-rx").html(measurements.rxRate.max);
    $("#jt-measure-tput-mean-rx").html(measurements.rxRate.mean);

    $("#jt-measure-tput-min-tx").html(measurements.txRate.min);
    $("#jt-measure-tput-max-tx").html(measurements.txRate.max);
    $("#jt-measure-tput-mean-tx").html(measurements.txRate.mean);
  };

  var updateRateDOM = function () {
    $("#jt-measure-pktRate-min-rx").html(measurements.rxPacketRate.min);
    $("#jt-measure-pktRate-max-rx").html(measurements.rxPacketRate.max);
    $("#jt-measure-pktRate-mean-rx").html(measurements.rxPacketRate.mean);

    $("#jt-measure-pktRate-min-tx").html(measurements.txPacketRate.min);
    $("#jt-measure-pktRate-max-tx").html(measurements.txPacketRate.max);
    $("#jt-measure-pktRate-mean-tx").html(measurements.txPacketRate.mean);
  };

  var updateZRunDOM = function () {
    $("#jt-measure-zRun-max-rx").html(measurements.rxRate.maxZ);
    $("#jt-measure-zRun-mean-rx").html(measurements.rxRate.meanZ);

    $("#jt-measure-zRun-max-tx").html(measurements.txRate.maxZ);
    $("#jt-measure-zRun-mean-tx").html(measurements.txRate.meanZ);
  };

  var updateDOM = function () {
    updateTputDOM();
    updateRateDOM();
    updateZRunDOM();
  };

  var drawIntervalID = setInterval(updateDOM, 100);

  my.measurementsModule.updateSeries = function (series, stats) {

    if (!measurements[series]) {
      measurements[series] = {};
    }
    measurements[series].min = stats.min.toFixed(2);
    measurements[series].max = stats.max.toFixed(2);
    measurements[series].mean = stats.mean.toFixed(2);
    measurements[series].maxZ = stats.maxZ;
    measurements[series].meanZ = stats.meanZ.toFixed(2);
  };

  return my;
}(JT));
/* end of jittertrap-measure.js */
