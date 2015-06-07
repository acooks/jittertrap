/* jittertrap-core.js */

/* global CBuffer */
/* global JT:true */
JT = {};

JT = (function (my) {
  'use strict';

  /* module namespace */
  my.core = {};

  var xVal = 0; //TODO: rename to indicate reductionFactor purpose.
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
    this.basicStats = [];
    this.minY = {x:0, y:0};
    this.maxY = {x:0, y:0};
  };

  var chartData = {
    mainChart: [],
    histogram: [],
    basicStats: []
  };

  /* must return a reference to an array of {x:x, y:y, label:l} */
  my.core.getHistogramRef = function () {
    return chartData.histogram;
  };

  /* must return a reference to an array of {x:x, y:y, label:l} */
  my.core.getBasicStatsRef = function () {
    return chartData.basicStats;
  };

  /* must return a reference to an array of {x:x, y:y} */
  my.core.getMainChartDataRef = function () {
    return chartData.mainChart;
  };

  var sBin = {};  // a container (Bin) for series.
  sBin.txDelta = new Series("txDelta",
                            "Tx Bytes per sample period",
                            "Tx Bytes per sample");

  sBin.rxDelta = new Series("rxDelta",
                            "Rx Bytes per sample period",
                            "Rx Bytes per sample");

  sBin.rxRate = new Series("rxRate",
                           "Ingress throughput in kbps",
                           "kbps, mean");

  sBin.txRate = new Series("txRate",
                           "Egress throughput in kbps",
                           "kbps, mean");

  sBin.txPacketRate = new Series("txPacketRate",
                                 "Egress packet rate",
                                 "pkts per sec, mean");

  sBin.rxPacketRate = new Series("rxPacketRate",
                                 "Ingress packet rate",
                                 "pkts per sec, mean");

  sBin.txPacketDelta = new Series("txPacketDelta",
                                  "Egress packets per sample",
                                  "packets sent");

  sBin.rxPacketDelta = new Series("rxPacketDelta",
                                  "Ingress packets per sample",
                                  "packets received");

  /* FIXME: this is a stepping stone to nowhere. */
  my.core.getSeriesByName = function (sName) {
    return sBin[sName];
  };

  var resizeCBuf = function(series, len) {
    series.filteredData = [];
    var b = new CBuffer(len);

    var l = (len < series.data.size) ? len : series.data.size;
    while (l--) {
      b.push(series.data.shift());
    }
    series.data = b;
  };

  my.core.resizeDataBufs = function(newlen) {

    resizeCBuf(sBin.txDelta, newlen);
    resizeCBuf(sBin.rxDelta, newlen);

    resizeCBuf(sBin.rxRate, newlen);
    resizeCBuf(sBin.txRate, newlen);

    resizeCBuf(sBin.txPacketRate, newlen);
    resizeCBuf(sBin.rxPacketRate, newlen);

    resizeCBuf(sBin.txPacketDelta, newlen);
    resizeCBuf(sBin.rxPacketDelta, newlen);
  };

  var clearChartData = function () {
    chartData.basicStats.length = 0;
    chartData.histogram.length = 0;
    chartData.mainChart.length = 0;
  };

  var clearSeries = function (s) {
    s.data = new CBuffer(my.core.sampleCount());
    s.filteredData = [];
  };

  my.core.clearAllSeries = function () {
    clearSeries(sBin.txDelta);
    clearSeries(sBin.rxDelta);
    clearSeries(sBin.txRate);
    clearSeries(sBin.rxRate);
    clearSeries(sBin.txPacketRate);
    clearSeries(sBin.rxPacketRate);
    clearSeries(sBin.txPacketDelta);
    clearSeries(sBin.rxPacketDelta);
    clearChartData();
    xVal = 0;
  };


  /* count must be bytes, samplePeriod is microseconds */
  var byteCountToKbpsRate = function(count) {
    var rate = count / my.core.samplePeriod() * 8000.0;
    return rate;
  };

  var packetDeltaToRate = function(count) {
    return count * (1000000.0 / my.core.samplePeriod());
  };

  /* Takes a CBuffer and counts the consecutive 0 elements.
   * Returns an object with max and mean counts.
   */
  var maxZRun = function (data) {
    if (data.size === 0) {
      return;
    }
    var maxRunLen = 0;
    var meanRunLen = 0;
    var runLengths = [ 0 ];
    var i, j = 0;

    for (i = data.size - 1; i >= 0 ; i--) {
      if (data.get(i) === 0) {
        runLengths[j]++;
        maxRunLen = (maxRunLen > runLengths[j]) ? maxRunLen : runLengths[j];
      } else if (runLengths[j] > 0) {
        meanRunLen += runLengths[j];
        j++;
        runLengths[j] = 0;
      }
    }
    meanRunLen /= runLengths.length;

    return { max: maxRunLen, mean: meanRunLen } ;
  };

  var updateStats = function (series) {

    if (! series.filteredData || series.filteredData.length === 0) {
      return;
    }

    var sortedData = series.filteredData.slice(0);
    sortedData.sort(function(a,b) {return (a.y - b.y);});

    var maxY = sortedData[sortedData.length-1].y;
    var minY = sortedData[0].y;
    var median = sortedData[Math.floor(sortedData.length / 2.0)].y;
    var mean = 0;
    var sum = 0;
    var i = 0;

    for (i = sortedData.length-1; i >=0; i--) {
      sum += sortedData[i].y;
    }
    mean = sum / sortedData.length;

    if (series.basicStats[0]) {
      series.basicStats[0].y = minY;
      series.basicStats[1].y = median;
      series.basicStats[2].y = mean;
      series.basicStats[3].y = maxY;
    } else {
      series.basicStats.push({x:1, y:minY, label:"Min"});
      series.basicStats.push({x:2, y:median, label:"Median"});
      series.basicStats.push({x:3, y:mean, label:"Mean"});
      series.basicStats.push({x:4, y:maxY, label:"Max"});
    }

    var maxZ = maxZRun(series.data);
    JT.measurementsModule.updateSeries(series.name, minY, maxY, mean, maxZ);
  };

  var updateHistogram = function(series) {
    var binCnt = 25;
    var normBins = new Float32Array(binCnt);

    var sortedData = series.data.slice(0);
    sortedData.sort();

    var maxY = sortedData[sortedData.length-1];
    var minY = sortedData[0];
    var range = (maxY - minY) * 1.1;

    /* bins must use integer indexes, so we have to normalise the
     * data and then convert it back before display.
     * [0,1) falls into bin[0] */
    var i = 0;
    var j = 0;

    /* initialise the bins */
    for (; i < binCnt; i++) {
      normBins[i] = 0;
    }

    /* bin the normalized data */
    for (j = 0; j < series.data.size; j++) {
      var normY = (series.data.get(j) - minY) / range * binCnt;
      normBins[Math.round(normY)]++;
    }

    /* convert to logarithmic scale */
    for (i = 0; i < normBins.length; i++) {
      if (normBins[i] > 0) {
        normBins[i] = Math.log(normBins[i]);
      }
    }

    /* write the histogram x,y data */
    chartData.histogram.length = 0;
    for (i = 0; i < binCnt; i++) {
      var x = Math.round(i * (maxY / binCnt));
      x += Math.round(minY);  /* shift x to match original y range */
      chartData.histogram.push({x: x, y: normBins[i], label: x});
    }

  };

  var updateFilteredSeries = function (series) {

    /* FIXME: float vs integer is important here! */
    var decimationFactor = Math.floor(my.charts.getChartPeriod() / (my.core.samplePeriod() / 1000.0));
    var fseriesLength = Math.floor(series.data.size / decimationFactor);

    // the downsampled data has to be scaled.
    var scale = 1 / my.charts.getChartPeriod();

    // how many filtered data points have been collected already?
    var filteredDataCount = series.filteredData.length;

    // if there isn't enough data for one filtered sample, return.
    if (fseriesLength === 0) {
      return;
    }

    // if the series is complete, expire the first value.
    if (filteredDataCount === fseriesLength) {
      series.filteredData.shift();
      filteredDataCount--;
    }

    // all the X values will be updated, but save the Y values.
    var filteredY = new Float32Array(fseriesLength);
    for (var i = filteredDataCount - 1; i >= 0; i--) {
      filteredY[i] = series.filteredData[i].y;
    }

    // now, discard all previous values, because all the X values will change.
    series.filteredData.length = 0;

    // calculate any/all missing Y values from raw data
    for (i = filteredDataCount; i < fseriesLength; i++) {
      filteredY[i] = 0.0;
      for (var j = 0; j < decimationFactor; j++) {
        var idx = i * decimationFactor + j;
        if (idx >= series.data.size) {
          break;
        }
        filteredY[i] += series.data.get(idx);
      }

      // scale the value to the correct range.
      filteredY[i] *= scale;
    }

    // finally, update the filteredData
    for (i = 0; i < fseriesLength; i++) {
      series.filteredData.push({x: i * my.charts.getChartPeriod(),
                                y: filteredY[i]});
    }

  };


  var updateSeries = function (series, yVal, selectedSeries) {
    series.data.push(yVal);

    /* do expensive operations once per filtered sample/chartingPeriod. */
    if ((xVal % my.charts.getChartPeriod() === 0) ) {
      updateStats(series);
      if (series === selectedSeries) {
        updateHistogram(series);
      }
      updateFilteredSeries(series);
    }
  };

  var updateData = function (d, sSeries) {
    updateSeries(sBin.txDelta, d.txDelta, sSeries);
    updateSeries(sBin.rxDelta, d.rxDelta, sSeries);
    updateSeries(sBin.txRate, byteCountToKbpsRate(d.txDelta), sSeries);
    updateSeries(sBin.rxRate, byteCountToKbpsRate(d.rxDelta), sSeries);
    updateSeries(sBin.txPacketRate, packetDeltaToRate(d.txPktDelta), sSeries);
    updateSeries(sBin.rxPacketRate, packetDeltaToRate(d.rxPktDelta), sSeries);
    updateSeries(sBin.txPacketDelta, d.txPktDelta, sSeries);
    updateSeries(sBin.rxPacketDelta, d.rxPktDelta, sSeries);
  };

  my.core.processDataMsg = function (stats) {
    var visibleSeries = $("#chopts_series option:selected").val();
    var selectedSeries = sBin[visibleSeries];

    var len = stats.length;
    for (var i = 0; i < len; i++) {
      updateData(stats[i], selectedSeries);
      xVal++;
      xVal = xVal % my.core.sampleCount();
    }

    my.trapModule.checkTriggers();

  };

  return my;
}(JT));
/* End of jittertrap-core.js */
