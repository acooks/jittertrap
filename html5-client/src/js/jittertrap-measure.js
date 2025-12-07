/* jittertrap-measure.js */

/* global JT: true */
/* global d3 */

((my) => {
  'use strict';
  my.measurementsModule = {};
  const measurements = {};

  measurements.rxRate = { min: 0, max: 0, mean: 0, maxZ: 0, meanZ: 0 };
  measurements.txRate = { min: 0, max: 0, mean: 0, maxZ: 0, meanZ: 0 };
  measurements.rxPacketRate = { min: 0, max: 0, mean: 0 };
  measurements.txPacketRate = { min: 0, max: 0, mean: 0 };

  // Format bitrate with SI prefix (e.g., 1.234 Mbps, 678.9 kbps)
  const formatBitrate = function(d) {
    // d is in bps. d3.format('.4s') uses 4 significant figures with SI prefix.
    return d3.format(".4s")(d) + " bps";
  };

  // Format packet rate with SI prefix (e.g., 1.234 Mpps, 678.9 kpps)
  const formatPacketRate = function(d) {
    // d is in pps. d3.format('.4s') uses 4 significant figures with SI prefix.
    return d3.format(".4s")(d) + " pps";
  };

  const updateTputDOM = function () {
    $("#jt-measure-tput-min-rx").html(formatBitrate(measurements.rxRate.min));
    $("#jt-measure-tput-max-rx").html(formatBitrate(measurements.rxRate.max));
    $("#jt-measure-tput-mean-rx").html(formatBitrate(measurements.rxRate.mean));

    $("#jt-measure-tput-min-tx").html(formatBitrate(measurements.txRate.min));
    $("#jt-measure-tput-max-tx").html(formatBitrate(measurements.txRate.max));
    $("#jt-measure-tput-mean-tx").html(formatBitrate(measurements.txRate.mean));
  };

  const updateRateDOM = function () {
    $("#jt-measure-pktRate-min-rx").html(formatPacketRate(measurements.rxPacketRate.min));
    $("#jt-measure-pktRate-max-rx").html(formatPacketRate(measurements.rxPacketRate.max));
    $("#jt-measure-pktRate-mean-rx").html(formatPacketRate(measurements.rxPacketRate.mean));

    $("#jt-measure-pktRate-min-tx").html(formatPacketRate(measurements.txPacketRate.min));
    $("#jt-measure-pktRate-max-tx").html(formatPacketRate(measurements.txPacketRate.max));
    $("#jt-measure-pktRate-mean-tx").html(formatPacketRate(measurements.txPacketRate.mean));
  };

  const updateZRunDOM = function () {
    $("#jt-measure-zRun-max-rx").html(measurements.rxRate.maxZ.toFixed(2) + " ms");
    $("#jt-measure-zRun-mean-rx").html(measurements.rxRate.meanZ.toFixed(2) + " ms");

    $("#jt-measure-zRun-max-tx").html(measurements.txRate.maxZ.toFixed(2) + " ms");
    $("#jt-measure-zRun-mean-tx").html(measurements.txRate.meanZ.toFixed(2) + " ms");
  };

  const updateDOM = function () {
    updateTputDOM();
    updateRateDOM();
    updateZRunDOM();
    $("#jt-measure-sample-period").html(my.charts.getChartPeriod() + "ms");

  };

  const drawIntervalID = setInterval(updateDOM, 100);

  my.measurementsModule.updateSeries = function (series, stats) {

    if (!measurements[series]) {
      measurements[series] = {};
    }
    // Store raw values, formatting will be applied in update functions
    measurements[series].min = stats.min;
    measurements[series].max = stats.max;
    measurements[series].mean = stats.mean;
    measurements[series].maxZ = stats.maxPG;
    measurements[series].meanZ = stats.meanPG;
  };

})(JT);
/* end of jittertrap-measure.js */
