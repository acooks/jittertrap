// interval in milliseconds
var microsecondsToRate = function(us) {
  if (us > 0) {
    return Math.ceil(1.0 / us * 1000000.0);
  }
};

var rateToMicroseconds = function(r) {
  if (r > 0) {
    return Math.ceil(1.0 / r * 1000000.0);
  }
};

/* count must be bytes, samplePeriod is microseconds */
var byteCountToKbpsRate = function(count) {
  var rate = count * 8.0 / samplePeriod * 1000.0;
  return rate;
};

var packetDeltaToRate = function(count) {
  return count * (1000000.0 / samplePeriod);
};

var updateStats = function (series) {
  var sortedData = series.data.slice(0);
  sortedData.sort(function(a,b) {return (a.y|0) - (b.y|0);});

  /* series.maxY and series.minY must be available to the histogram */
  series.maxY = sortedData[sortedData.length-1];
  series.minY = sortedData[0];

  /* median is a pair */
  var median = sortedData[Math.floor(sortedData.length / 2.0)];

  var mean = 0;
  var sum = 0;
  for (var i = sortedData.length-1; i >=0; i--) {
    sum += sortedData[i].y;
  }
  mean = sum / sortedData.length;

  for (var i = series.basicStats.length; i > 0; i--) {
    series.basicStats.shift();
  }

  series.basicStats.push({x:1, y:series.minY.y, label:"Min"});
  series.basicStats.push({x:2, y:median.y, label:"Median"});
  series.basicStats.push({x:3, y:mean, label:"Mean"});
  series.basicStats.push({x:4, y:series.maxY.y, label:"Max"});
};

var updateHistogram = function(series) {
  var binCnt = 20;
  var normBins = new Float32Array(binCnt);
  var range = series.maxY.y - series.minY.y;

  /* bins must use integer indexes, so we have to normalise the
    * data and then convert it back before display.
    * [0,1) falls into bin[0] */
  var i = 0;
  var j = 0;

  /* initialise the bins */
  for (; i < binCnt; i++) {
    normBins[i] = 0;
    series.histData.shift();
  }

  /* bin the normalized data */
  for (; j < series.data.length; j++) {
    var normY = (series.data[j].y - series.minY.y) / range * binCnt;
    normBins[Math.floor(normY)]++;
  }

  /* write the histogram x,y data */
  for (i = 0; i < binCnt; i++) {
    var xVal = Math.ceil(i * (series.maxY.y / binCnt));
    xVal += series.minY.y;  /* shift x to match original y range */
    series.histData.push({x: xVal, y: normBins[i], label: xVal});
  }
};

var updateFilteredSeries = function (series) {

  /* FIXME: float vs integer is important here! */
  var decimationFactor = Math.floor(chartingPeriod / (samplePeriod / 1000.0));
  var fseriesLength = Math.floor(series.data.length / decimationFactor);

  // the downsampled data has to be scaled.
  var scale = 1/chartingPeriod;

  // how many filtered data points have been collected already?
  var filteredDataCount = series.filteredData.length;

  // if the series is complete, expire the first value.
  if (filteredDataCount == fseriesLength) {
    series.filteredData.shift();
    filteredDataCount--;
  }

  // all the X values will be updated, but save the Y values.
  var filteredY = new Float32Array(fseriesLength);
  for (var i = filteredDataCount - 1; i >= 0; i--) {
    filteredY[i] = series.filteredData[i].y;
  }

  // now, discard all previous values, because all the X values will change.
  for (var i = filteredDataCount; i > 0; i--) {
    series.filteredData.shift();
  }

  // calculate any/all missing Y values from raw data
  for (var i = filteredDataCount; i < fseriesLength; i++) {
    filteredY[i] = 0.0;
    for (var j = 0; j < decimationFactor; j++) {
      var idx = i * decimationFactor + j;
      if (idx > series.data.length) {
        break;
      }
      filteredY[i] += series.data[idx].y;
    }

    // scale the value to the correct range.
    filteredY[i] *= scale;
  }

  // finally, update the filteredData
  for (var i = 0; i < fseriesLength; i++) {
    series.filteredData.push({x: i * chartingPeriod, y: filteredY[i]});
  }

};

var updateSeries = function (series, xVal, yVal, selectedSeries) {
  series.data.push({ x: xVal, y: yVal });
  while (series.data.length > dataLength) {
    series.data.shift();
  }

  /* do expensive operations once per filtered sample/chartingPeriod. */
  if ((xVal % chartingPeriod == 0) && (series == selectedSeries)) {
    updateStats(series);
    updateHistogram(series);
    updateFilteredSeries(series);
  }
};
