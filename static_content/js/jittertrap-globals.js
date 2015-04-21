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
var chartingPeriod = 60;
var chartingPeriodMin = 1;
var chartingPeriodMax = 500;

/*
 *
 */
var dataLengthMultiplier = 200;

/*
 * number of raw data samples.
 * dataLength = chartingPeriod * dataLengthMultiplier
 */
var dataLength = 4000;

/*
 * list of active traps
 */
var traps = {};
