var xVal = 0;

/**
 * chart updates; milliseconds; 40ms == 25 Hz
 */
var updatePeriod = 60;
var updatePeriodMin = 40;
var updatePeriodMax = 100;
var old_updatePeriod; // used for pausing/resuming
/**
 * data samples; microseconds; fixed.
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
var dataLengthMultiplier = 300;

/*
 * number of raw data samples.
 * dataLength = chartingPeriod * dataLengthMultiplier
 */
var dataLength = 18000;

/*
 * list of active traps
 */
var traps = {};


var websocket = {};
var chart = {};
var histogram = {};
var basicStatsGraph = {};
