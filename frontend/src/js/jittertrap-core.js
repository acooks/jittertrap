/* jittertrap-core.js */

/* global CBuffer */
/* global JT:true */

JT = (function (my) {
  'use strict';

  /* module namespace */
  my.core = {};

  var xVal = 0; //TODO: rename to indicate reductionFactor purpose.
  var samplePeriod = JT.coreconfig.samplePeriod;

  /* raw data sample period; microseconds; fixed. */
  my.core.samplePeriod = function(sp) {
    if (sp) {
      console.log("sample period set to " + sp + " microseconds");
      samplePeriod = sp;
    }
    return samplePeriod;
  };

  /* scaling factor for number of raw data points */
  var dataLengthMultiplier = samplePeriod;

  /* number of raw data samples. */
  var sampleCount = dataLengthMultiplier * 60;

  my.core.sampleCount = function (plotPeriod) {
    if (plotPeriod) {
      sampleCount = Math.floor(dataLengthMultiplier * plotPeriod);
    }
    return sampleCount;
  };

  /* count must be bytes, samplePeriod is microseconds */
  var byteCountToKbpsRate = function(count) {
    var rate = count / my.core.samplePeriod() * 8000.0 * (my.core.samplePeriod() / 1000);
    return rate;
  };

  var packetDeltaToRate = function(count) {
    return count * (1000000.0 / my.core.samplePeriod()) * (my.core.samplePeriod() / 1000);
  };

  /* a prototype object to encapsulate timeseries data. */
  var Series = function(name, title, ylabel, rateFormatter) {
    this.name = name;
    this.title = title;
    this.ylabel = ylabel;
    this.rateFormatter = rateFormatter;
    this.xlabel = "Time (ms)";
    this.data = []; // raw samples
    this.filteredData = []; // filtered & decimated to chartingPeriod
    this.packetGapData = [];
    this.stats = {min: 99999, max:0, median:0, mean:0, maxZ:0, meanZ:0 };
  };

  var sBin = {};  // a container (Bin) for series.
  sBin.rxRate = new Series("rxRate",
                           "Ingress Bitrate in kbps",
                           "kbps, mean",
                           byteCountToKbpsRate);

  sBin.txRate = new Series("txRate",
                           "Egress Bitrate in kbps",
                           "kbps, mean",
                           byteCountToKbpsRate);

  sBin.txPacketRate = new Series("txPacketRate",
                                 "Egress packet rate",
                                 "pkts per sec, mean",
                                 packetDeltaToRate);

  sBin.rxPacketRate = new Series("rxPacketRate",
                                 "Ingress packet rate",
                                 "pkts per sec, mean",
                                 packetDeltaToRate);

  /* FIXME: this is a stepping stone to nowhere. */
  my.core.getSeriesByName = function (sName) {
    return sBin[sName];
  };

  var resizeCBuf = function(series, len) {
    series.filteredData = [];
    series.packetGapData = [];
    var b = new CBuffer(len);

    var l = (len < series.data.size) ? len : series.data.size;
    while (l--) {
      b.push(series.data.shift());
    }
    series.data = b;
  };

  my.core.resizeDataBufs = function(newlen) {

    resizeCBuf(sBin.rxRate, newlen);
    resizeCBuf(sBin.txRate, newlen);

    resizeCBuf(sBin.txPacketRate, newlen);
    resizeCBuf(sBin.rxPacketRate, newlen);

  };

  var clearSeries = function (s) {
    s.data = new CBuffer(my.core.sampleCount());
    s.filteredData = [];
    s.packetGapData = [];
  };

  my.core.clearAllSeries = function () {
    clearSeries(sBin.txRate);
    clearSeries(sBin.rxRate);
    clearSeries(sBin.txPacketRate);
    clearSeries(sBin.rxPacketRate);
    xVal = 0;
  };

  var numSort = function(a,b) {
    return (a - b)|0;
  };


  /* Takes an Array and counts the consecutive 0 elements.
   * Returns an object with max and mean counts.
   */
  var packetGap = function (data) {
    if (data.length === 0) {
      return;
    }
    var maxGap = 0;
    var meanGap = 0;
    var minGap = 99;
    var runLengths = [ 0 ];
    var i, j = 0;

    for (i = data.length - 1; i >= 0 ; i--) {
      if (data[i] === 0) {
        runLengths[j]++;
        maxGap = (maxGap > runLengths[j]) ? maxGap : runLengths[j];
      } else if (runLengths[j] > 0) {
        meanGap += runLengths[j];
        j++;
        runLengths[j] = 0;
      }
    }
    maxGap *= (my.core.samplePeriod() / 1000);

    if (runLengths.length === 1) {
      meanGap = runLengths[0];
    } else {
      meanGap /= runLengths.length;
    }
    meanGap *= (my.core.samplePeriod() / 1000);


    var s = runLengths.slice(0);
    s.sort(numSort);
    minGap = s[0] * (my.core.samplePeriod() / 1000);

    return { max: maxGap, mean: meanGap, min: minGap } ;
  };

  var updateBasicStatsChartData = function (stats, chartSeries) {
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

  var updatePacketGapChartData = function (packetGapData, mean, minMax) {

    var chartPeriod = my.charts.getChartPeriod();
    var len = packetGapData.length;

    mean.length = 0;
    minMax.length = 0;

    for (var i = 0; i < len; i++) {
      var x = i * chartPeriod;
      mean.push({x: x, y: packetGapData[i].mean});
      minMax.push({x: x, y: [packetGapData[i].min, packetGapData[i].max]});
    }
  };

  var updateStats = function (series) {

    if (! series.filteredData || series.filteredData.length === 0) {
      return;
    }

    var sortedData = series.filteredData.slice(0);
    sortedData.sort(numSort);

    series.stats.max = sortedData[sortedData.length-1];
    series.stats.min = sortedData[0];
    series.stats.median = sortedData[Math.floor(sortedData.length / 2.0)];
    var sum = 0;
    var i = 0;

    for (i = sortedData.length-1; i >=0; i--) {
      sum += sortedData[i];
    }
    series.stats.mean = sum / sortedData.length;

    var pGap = packetGap(series.data.toArray());
    series.stats.maxZ = pGap.max;
    series.stats.meanZ = pGap.mean;
    series.stats.minZ = pGap.min
  };

  var updateHistogram = function(series, chartSeries) {
    var binCnt = 25;
    var sortedData = series.filteredData.slice(0);
    sortedData.sort(numSort);

    var maxY = sortedData[sortedData.length-1];
    var minY = sortedData[0];

    var normBins = new Float32Array(binCnt);
    var range = (maxY - minY);

    /* prevent division by zero */
    range = (range > 0) ? range : 1;

    //console.log("min: " + minY + " maxY: " + maxY + " range: " + range);

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
    for (j = 0; j < series.filteredData.length; j++) {
      var normY = (series.filteredData[j] - minY) / range * (binCnt - 1);
      console.assert((normY >= 0) && (normY < binCnt));
      normBins[Math.round(normY)]++;
    }
    console.assert(normBins.length == binCnt);

    /* convert to logarithmic scale */
    for (i = 0; i < normBins.length; i++) {
      if (normBins[i] > 0) {
        normBins[i] = Math.log(normBins[i]);
      }
    }

    /* write the histogram x,y data */
    chartSeries.length = 0;
    for (i = 0; i < binCnt; i++) {
      var x = Math.round(i * (range / (binCnt-1)));
      x += Math.round(minY);  /* shift x to match original y range */
      chartSeries.push({x: x, y: normBins[i], label: x});
    }
  };

  var updateMainChartData = function(filteredData, formatter, chartSeries) {
    var chartPeriod = my.charts.getChartPeriod();
    var len = filteredData.length;

    chartSeries.length = 0;

    for (var i = 0; i < len; i++) {
      chartSeries.push({x: i * chartPeriod, y: filteredData[i]});
    }
  };

  var updateFilteredSeries = function (series) {

    /* NB: float vs integer is important here! */
    var decimationFactor = Math.floor(my.charts.getChartPeriod());
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
      series.packetGapData.shift();
      filteredDataCount--;
    }

    // calculate any/all missing Y values from raw data
    for (var i = filteredDataCount; i < fseriesLength; i++) {
      series.filteredData[i] = 0.0;
      var subSeriesStartIdx = i * decimationFactor;
      var subSeriesEndIdx = i * decimationFactor + decimationFactor;
      var subSeries = series.data.slice(subSeriesStartIdx, subSeriesEndIdx);
      var pgd = packetGap(subSeries);
      series.packetGapData.push(pgd);

      for (var j = 0; j < decimationFactor; j++) {
        var idx = i * decimationFactor + j;
        if (idx >= series.data.size) {
          break;
        }
        series.filteredData[i] += series.data.get(idx);
      }

      // scale the value to the correct range.
      series.filteredData[i] *= scale;
      series.filteredData[i] = series.rateFormatter(series.filteredData[i]);
    }
  };

  var updateSeries = function (series, yVal, selectedSeries) {
    series.data.push(yVal);

    /* do expensive operations once per charted data point. */
    if ((xVal % my.charts.getChartPeriod() === 0) ) {
      updateFilteredSeries(series);
      updateStats(series);

      JT.measurementsModule.updateSeries(series.name, series.stats);
      JT.trapModule.checkTriggers(series.name, series.stats);

      /* update the charts data */
      if (series === selectedSeries) {

        /* these look at windows the size of chart period */
        updateMainChartData(series.filteredData,
                            series.rateFormatter,
                            JT.charts.getMainChartRef());
        updatePacketGapChartData(series.packetGapData,
                                 JT.charts.getPacketGapMeanRef(),
                                 JT.charts.getPacketGapMinMaxRef());

        /* these look at the whole series */
        updateHistogram(series, JT.charts.getHistogramRef());
        updateBasicStatsChartData(series.stats, JT.charts.getBasicStatsRef());
      }
    }
  };

  var updateData = function (d, sSeries) {
    updateSeries(sBin.txRate, d.txDelta, sSeries);
    updateSeries(sBin.rxRate, d.rxDelta, sSeries);
    updateSeries(sBin.txPacketRate, d.txPktDelta, sSeries);
    updateSeries(sBin.rxPacketRate, d.rxPktDelta, sSeries);
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
  };

  return my;
}(JT));
/* End of jittertrap-core.js */
