var xVal = 0;

/**
 * chart updates; milliseconds; 40ms == 25 Hz
 */
var updatePeriod = 50;

/**
 * data samples; microseconds
 */
var samplePeriod = 1000;

/**
 * time (milliseconds) represented by each data point
 */
var chartingPeriod = 20;

/*
 *
 */
var dataLengthMultiplier = 200;

/*
 * number of raw data samples.
 * dataLength = chartingPeriod * dataLengthMultiplier
 */
var dataLength = 4000;
