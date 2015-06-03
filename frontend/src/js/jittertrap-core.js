/* jittertrap-core.js */

/* global JT:true */

JT = (function (my) {
  'use strict';

  /* module namespace */
  my.core = {};
  var mm = my.core;

  var samplePeriod = 1000;

  /* raw data sample period; microseconds; fixed. */
  mm.samplePeriod = function(sp) {
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

  mm.sampleCount = function (plotPeriod) {
    if (plotPeriod) {
      sampleCount = Math.floor(dataLengthMultiplier * plotPeriod);
    }
    return sampleCount;
  };

  return my;
}(JT));
/* end jittertrap-core.js */
