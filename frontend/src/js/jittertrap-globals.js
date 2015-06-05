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


/* end of jittertrap-globals.js */
