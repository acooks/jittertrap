/* jittertrap-chart-toptalk.js */

/* global d3 */
/* global JT:true */

JT = (function (my) {
  'use strict';

  my.charts.toptalk = {};

  var chartData = [];

  var clearChartData = function () {
    chartData.length = 0;
  };

  /* must return a reference to an array of {x:x, y:y} */
  my.charts.toptalk.getDataRef = function () {
    return chartData;
  };

  my.charts.toptalk.toptalkChart = (function (m) {
    const margin = {
      top: 20,
      right: 20,
      bottom: 440,
      left: 75
    };

    const size = { width: 960, height: 700 };
    let xScale = d3.scaleLinear();
    let yScale = d3.scaleLinear();
    const colorScale = d3.scaleOrdinal(d3.schemeCategory10);
    let xAxis = d3.axisBottom();
    let yAxis = d3.axisLeft();
    let xGrid = d3.axisBottom();
    let yGrid = d3.axisLeft();
    var area = d3.area();

    const stack = d3.stack()
                .order(d3.stackOrderReverse)
                .offset(d3.stackOffsetNone);

    /* Make a displayable title from the flow key */
    var key2legend = function (fkey) {
      var a = fkey.split('/');
      var padsource = " ".repeat(15 - a[1].length);
      var padsport = " ".repeat(6 - a[2].length);
      var paddest = " ".repeat(15 - a[3].length);
      var paddport = " ".repeat(9 - a[4].length);
      var padproto = " ".repeat(8 - a[5].length);
      var padtclass = " ".repeat(13 - a[6].length);
      return a[1] + padsource + " : "
             + a[2] + padsport
             + "   ->   "
             + a[3] + paddest + " : "
             + a[4] + paddport + " │ " + padproto + a[5] + " │ "
             + padtclass + a[6];
    };

    var svg = {};
    var context = {};
    var canvas = {};

    /* Reset and redraw the things that don't change for every redraw() */
    m.reset = function() {

      d3.select("#chartToptalk").selectAll("svg").remove();
      d3.select("#chartToptalk").selectAll("canvas").remove();


      canvas = d3.select("#chartToptalk")
            .append("canvas")
            .style("position", "absolute");

      my.charts.resizeChart("#chartToptalk", size)();
      context = canvas.node().getContext("2d");

      svg = d3.select("#chartToptalk")
            .append("svg")
            .style("position", "relative");

      area.context(context);


      var width = size.width - margin.left - margin.right;
      var height = size.height - margin.top - margin.bottom;

      xScale = d3.scaleLinear().range([0, width]);
      yScale = d3.scaleLinear().range([height, 0]);

      xAxis = d3.axisBottom()
              .scale(xScale)
              .ticks(10);

      yAxis = d3.axisLeft()
              .scale(yScale)
              .ticks(5);

      xGrid = d3.axisBottom()
          .scale(xScale)
           .tickSize(-height)
           .ticks(10)
           .tickFormat("");

      yGrid = d3.axisLeft()
          .scale(yScale)
           .tickSize(-width)
           .ticks(5)
           .tickFormat("");

      svg.attr("width", width + margin.left + margin.right)
         .attr("height", height + margin.top + margin.bottom);

      canvas.attr("width", width)
         .attr("height", height)
         .style("transform", "translate(" + margin.left + "px," + margin.top + "px)");

      var graph = svg.append("g")
         .attr("transform", "translate(" + margin.left + "," + margin.top + ")");

      graph.append("text")
         .attr("class", "title")
         .attr("text-anchor", "middle")
         .attr("x", width/2)
         .attr("y", -margin.top/2)
         .text("Top flows");

      graph.append("g")
         .attr("class", "x axis")
         .attr("transform", "translate(0," + height + ")")
         .call(xAxis);

      graph.append("text")
           .attr("class", "x label")
           .attr("text-anchor", "middle")
           .attr("x", width/2)
           .attr("y", height + 35)
           .text("Time");

      graph.append("g")
         .attr("class", "y axis")
         .call(yAxis)
         .append("text")
         .attr("x", -margin.left)
         .attr("transform", "rotate(-90)")
         .attr("y", -margin.left)
         .attr("dy", ".71em")
         .style("text-anchor", "end")
         .text("Bytes");

      graph.append("g")
        .attr("class", "xGrid")
        .attr("transform", "translate(0," + height + ")")
        .call(xGrid);

      graph.append("g")
        .attr("class", "yGrid")
        .call(yGrid);

      context.clearRect(0, 0, width, height);

      svg.append("g")
         .attr("class", "barsbox")
         .attr("id", "barsbox")
         .append("text")
           .text("Byte Distribution")

      svg.append("g")
         .attr("class", "legendbox")
         .attr("id", "ttlegendbox")
         .append("text")
	   .attr("x", 22)
	   .attr("y", 9)
	   .attr("dy", ".35em")
	   .style("text-anchor", "begin")
	   .style("font-family" ,"monospace")
	   .style("white-space", "pre")
	   .text("Source          : Src Port ->   Destination     : Dst Port  │ Protocol │ Traffic Class")
             .attr("class", "legendheading")
             .attr("transform",
                    function(d, i) {
                       return "translate(" + margin.left + "," + 400 + ")";
                    }
              );


      my.charts.resizeChart("#chartToptalk", size)();
    };

    /* To find the range of the y-axis, find max of the stacked x values */
    var maxBytesSlice = function(chartData) {
      var i, j;
      var flowCount, sampleCount, maxSlice = 0;

      flowCount = chartData.length;
      if (!flowCount) {
        return 0;
      }

      sampleCount = chartData[0].values.length;

      for (i = 0; i < sampleCount; i++) {
        var thisSliceBytes = 0;
        for (j = 0; j < flowCount; j++) {
          thisSliceBytes += chartData[j].values[i].bytes;
        }
        if (thisSliceBytes > maxSlice) {
          maxSlice = thisSliceBytes;
        }
      }
      return maxSlice;
    };

    /* Reformat chartData to work with the new d3 v4 API
     * Ref: https://github.com/d3/d3-shape/blob/master/README.md#stack */
    var formatData = function(chartData) {
      // Use a Map for O(1) indexed lookups, which is much faster than map().indexOf().
      var binsMap = new Map();

      for (let i = 0; i < chartData.length; i++) {
        const row = chartData[i];
        for (let j = 0; j < row.values.length; j++) {
          const o = row.values[j];
          const ts = o.ts;
          const fkey = row.fkey;
          const bytes = o.bytes;

          // Check if we have seen this timestamp before.
          if (!binsMap.has(ts)) {
            // If not, create a new entry for it in the map.
            binsMap.set(ts, { "ts": ts });
          }

          // Get the bin for the current timestamp.
          const bin = binsMap.get(ts);
          bin[fkey] = (bin[fkey] || 0) + bytes;
        }
      }

      // Convert the map's values back into an array for d3.stack().
      return Array.from(binsMap.values());
    }


    /* Update the chart (try to avoid memory allocations here!) */
    m.redraw = function() {

      var width = size.width - margin.left - margin.right;
      var height = size.height - margin.top - margin.bottom;

      xScale = d3.scaleLinear().range([0, width]);
      /* compute the domain of x as the [min,max] extent of timestamps
       * of the first (largest) flow */
      if (chartData && chartData[0]) {
        xScale.domain(d3.extent(chartData[0].values, function(d) {
          return d.ts;
        }));
      }

      var yPow = d3.select('input[name="y-axis-is-log"]:checked').node().value;

      if (yPow == 1) {
        yScale = d3.scalePow().exponent(0.5).clamp(true).range([height, 0]);
      } else {
        yScale = d3.scaleLinear().clamp(true).range([height, 0]);
      }
      yScale.domain([0, maxBytesSlice(chartData)]);

      xAxis.scale(xScale);
      yAxis.scale(yScale);

      xGrid.scale(xScale);
      yGrid.scale(yScale);

      svg = d3.select("#chartToptalk");

      svg.select(".x.axis").transition().duration(20).call(xAxis);
      svg.select(".y.axis").transition().duration(20).call(yAxis);
      svg.select(".xGrid").call(xGrid);
      svg.select(".yGrid").call(yGrid);

      var fkeys = chartData.map(function(f) { return f.fkey; });
      colorScale.domain(fkeys);

      stack.keys(fkeys);

      // Format the data, so they're flat arrays
      var stackedChartData = stack(
        formatData(chartData));

      area = d3.area()
               .curve(d3.curveMonotoneX)
               .context(context)
               .x(function (d) { return xScale(d.data.ts); })
               .y0(function (d) { return yScale(d[0] || 0); })
               .y1(function (d) { return yScale(d[1] || 0); });

      context.clearRect(0, 0, width, height);

      stackedChartData.forEach(function(layer) {
        context.beginPath();
        area(layer);
        context.fillStyle = colorScale(layer.key);
        context.fill();
      });



      // distribution bar
      var contribs = chartData.map(function(f) {
        return { k: f.fkey, b: f.tbytes, p :f.tpackets };
      });

      var tbytes = contribs.reduce(function(a,b) { return a + b.b }, 0 );

      var rangeStop = 0;
      var barData = contribs.map(function(d) {
        var new_d = {
          k: d.k,
          x0: rangeStop,
          x1: (rangeStop + d.b)
        };
        rangeStop = new_d.x1;
        return new_d;
      });

      var x = d3.scaleLinear()
                      .rangeRound([0, width])
                      .domain([0,tbytes]);

      var y = d3.scaleBand()
                      .range([0, 10])
                      .round(.3);

      var barsbox = svg.select("#barsbox");
      barsbox.selectAll(".subbar").remove();
      var bars = barsbox.selectAll("rect")
                    .data(barData)
                    .enter().append("g").attr("class", "subbar");

      bars.append("rect")
          .attr("height", 23)
          .attr("y", 9)
          .attr("x", function(d) { return x(d.x0); })
          .attr("width", function(d) { return x(d.x1) - x(d.x0); })
          .style("fill", function(d) { return colorScale(d.k); });

      barsbox.attr("transform", function(d) {
        return "translate(" + margin.left + "," + 350 + ")";
      });


      // legend box handling
      var legend_tabs = colorScale.domain();
      var legendbox = svg.select("#ttlegendbox");
      legendbox.selectAll(".legend").remove();
      var legend = legendbox.selectAll(".legend")
                   .data(fkeys.slice()).enter()
                   .append("g")
                   .attr("class", "legend")
                   .attr("transform",
                         function(d, i) {
                           return "translate(" + margin.left + ","
                                             + (400 + ((i+1) * 25)) + ")";
                         }
                   );

      legend.append("rect")
	      .attr("x", 0)
	      .attr("width", 18)
	      .attr("height", 18)
	      .style("fill", colorScale);

      legend.append("text")
	      .attr("x", 22)
	      .attr("y", 9)
	      .attr("dy", ".35em")
	      .style("text-anchor", "begin")
	      .style("font-family" ,"monospace")
	      .style("white-space", "pre")
	      .text(function(d) { return key2legend(d); });

    };


    /* Set the callback for resizing the chart */
    d3.select(window).on('resize.chartToptalk',
                         my.charts.resizeChart("#chartToptalk", size));

    return m;

  }({}));

  return my;
}(JT));
/* End of jittertrap-chart-toptalk.js */
