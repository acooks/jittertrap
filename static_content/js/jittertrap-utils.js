// interval in milliseconds
var millisecondsToRate = function(ms) {
  if (ms > 0) {
    return Math.ceil(1.0 / ms * 1000.0);
  }
};

var rateToMilliseconds = function(r) {
  if (r > 0) {
    return Math.ceil(1.0 / r * 1000.0);
  }
};

/* count must be bytes, duration must be milliseconds */
var byteCountToKbpsRate = function(count) {
  var rate = count * (1000.0 / samplePeriod) * 8.0 / 1000.0;
  return rate;
};

var packetDeltaToRate = function(count) {
  return count * (1000.0 / samplePeriod);
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
  var normBins = [];
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
  var newdata = [];
  for (i = 0; i < binCnt; i++) {
    var xVal = Math.ceil(i * (series.maxY.y / binCnt));
    xVal += series.minY.y;  /* shift x to match original y range */
    series.histData.push({x: xVal, y: normBins[i], label: xVal});
  }
};

var updateSeries = function (series, xVal, yVal) {
  series.data.push({ x: xVal, y: yVal });
  while (series.data.length > dataLength) {
    series.data.shift();
  }
  updateStats(series);
  updateHistogram(series);
};
