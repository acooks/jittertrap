/* jittertrap-chart-toptalk.js */

/* global d3 */
/* global JT:true */

((my) => {
  'use strict';

  my.charts.toptalk = {};

  const chartData = [];

  const clearChartData = function () {
    chartData.length = 0;
  };

  /* must return a reference to an array of {x:x, y:y} */
  my.charts.toptalk.getDataRef = function () {
    return chartData;
  };

  const processAndAggregateChartData = function(incomingData) {
    const LEGEND_DISPLAY_LIMIT = 20;

    if (incomingData.length <= LEGEND_DISPLAY_LIMIT) {
      return incomingData;
    }

    const topNFlows = incomingData.slice(0, LEGEND_DISPLAY_LIMIT);
    const remainingFlows = incomingData.slice(LEGEND_DISPLAY_LIMIT);

    const otherFlow = {
      fkey: 'other',
      tbytes: 0,
      values: []
    };

    const otherValuesMap = new Map();

    remainingFlows.forEach(flow => {
      otherFlow.tbytes += flow.tbytes;
      flow.values.forEach(dataPoint => {
        const currentBytes = otherValuesMap.get(dataPoint.ts) || 0;
        otherValuesMap.set(dataPoint.ts, currentBytes + dataPoint.bytes);
      });
    });

    otherValuesMap.forEach((bytes, ts) => {
      otherFlow.values.push({ ts: ts, bytes: bytes });
    });

    // Ensure the values are sorted by timestamp, as d3 expects
    otherFlow.values.sort((a, b) => a.ts - b.ts);

    return topNFlows.concat(otherFlow);
  };

  my.charts.toptalk.toptalkChart = (function (m) {
    // Use smaller margins on mobile devices
    const isMobile = window.innerWidth <= 768;
    const margin = {
      top: isMobile ? 15 : 20,
      right: isMobile ? 10 : 20,
      bottom: isMobile ? 80 : 100,
      left: isMobile ? 50 : 75
    };

    const size = { width: 960, height: 400 };
    let xScale = d3.scaleLinear();
    let yScale = d3.scaleLinear();
    // Use Spectral interpolator for better distinctness with 20+ flows
    const colorScale = d3.scaleOrdinal(d3.quantize(d3.interpolateSpectral, 21).reverse());

    const formatBitrate = function(d) {
        // d is in bps. d3.format('.2s') auto-scales and adds SI prefix.
        return d3.format(".2s")(d) + "bps";
    };
    
    let xAxis = d3.axisBottom();
    let yAxis = d3.axisLeft().tickFormat(formatBitrate);
    let xGrid = d3.axisBottom();
    let yGrid = d3.axisLeft();
    let area = d3.area();

    const stack = d3.stack()
                .order(d3.stackOrderReverse)
                .offset(d3.stackOffsetNone);

    // Bisector to find the closest timestamp index
    const bisectDate = d3.bisector(d => d.data.ts).left;

    let svg = {};
    let context = {};
    let canvas = {};
    let currentStackedData = []; // Store for hit-testing
    let lastMousePosition = null; // Store mouse position for live tooltip updates
    let resizeTimer; // Timer for debounced resize handling
    let cachedFkeys = []; // Cache for legend optimization

    const updateTooltip = function(mousePos) {
        const tooltip = d3.select("#toptalk-tooltip");
        
        if (!currentStackedData || currentStackedData.length === 0 || !mousePos) {
             tooltip.style("opacity", 0);
             return;
        }

        // mousePos is { rel: [rx, ry], page: [px, py] }
        // Use relative coords for chart hit testing
        const [mx, my] = mousePos.rel;
        // Use page coords for tooltip positioning
        const [px, py] = mousePos.page;
        
        // Adjust for margins to get chart area coordinates
        const chartX = mx - margin.left;
        const chartY = my - margin.top;

        // If outside chart area, hide tooltip
        if (chartX < 0 || chartX > (size.width - margin.left - margin.right) ||
            chartY < 0 || chartY > (size.height - margin.top - margin.bottom)) {
          tooltip.style("opacity", 0);
          return;
        }

        // Invert X to get timestamp
        const x0 = xScale.invert(chartX);
        
        // Find index in the first layer's data
        const layerData = currentStackedData[0]; 
        if (!layerData || layerData.length === 0) return; // No data at all

        // Find closest data point index. `i` is the insertion point.
        const i = bisectDate(layerData, x0, 1);
        
        let index = -1; // Default to not found
        
        if (i === 0) {
            // Mouse is before the first data point or at it, consider the first if data exists
            if (layerData.length > 0) index = 0;
        } else if (i === layerData.length) {
            // Mouse is after the last data point, consider the last if data exists
            if (layerData.length > 0) index = layerData.length - 1;
        } else {
            // Mouse is between two points, find the closest
            const d0 = layerData[i - 1];
            const d1 = layerData[i];
            index = x0 - d0.data.ts > d1.data.ts - x0 ? i : i - 1;
        }
        
        // If no valid index found after all checks, hide tooltip.
        if (index === -1) {
            tooltip.style("opacity", 0);
            return;
        }

        // Check Y coordinate against stack layers
        let found = false;
        for (const layer of currentStackedData) {
          const dp = layer[index];
          if (!dp) continue;
          
          const yLower = yScale(dp[0]);
          const yUpper = yScale(dp[1]);
          
          if (chartY >= yUpper && chartY <= yLower) {
             const fkey = layer.key;
             let content = "";
             
             if (fkey === 'other') {
               content = "<strong>Other Flows</strong><br/>";
             } else {
               const flow = parseFlowKey(fkey);
               content = `<strong>${flow.sourceIP}:${flow.sourcePort} &rarr; ${flow.destIP}:${flow.destPort}</strong><br/>` +
                         `${flow.proto} | ${flow.tclass}`;
             }
             
             const bitrate = dp.data[fkey];
             content += `<br/>${formatBitrate(bitrate)}`;

             // Set content and styling first
             tooltip.html(content)
                    .style("opacity", 1)
                    .style("background", getFlowColor(fkey))
                    .style("border", "1px solid #fff");

             // Get tooltip dimensions for boundary detection
             const tooltipNode = tooltip.node();
             const tooltipWidth = tooltipNode.offsetWidth;
             const tooltipHeight = tooltipNode.offsetHeight;
             const viewportWidth = window.innerWidth;
             const viewportHeight = window.innerHeight;

             // Default offsets
             let left = px + 10;
             let top = py - 28;

             // Check right boundary - flip to left if would go off-screen
             if (left + tooltipWidth > viewportWidth) {
               left = px - tooltipWidth - 10;
             }

             // Check top boundary - flip below cursor if would go off-screen
             if (top < 0) {
               top = py + 10;
             }

             // Check bottom boundary
             if (top + tooltipHeight > viewportHeight) {
               top = viewportHeight - tooltipHeight - 10;
             }

             tooltip.style("left", left + "px")
                    .style("top", top + "px");
                    
             found = true;
             break;
          }
        }
        
        if (!found) {
          tooltip.style("opacity", 0);
        }
    };
    
    /* Reset and redraw the things that don't change for every redraw() */
    m.reset = function() {

      let tooltip = d3.select("body").select("#toptalk-tooltip");
      if (tooltip.empty()) {
        tooltip = d3.select("body").append("div")
          .attr("id", "toptalk-tooltip")
          .attr("class", "jt-tooltip");
      }

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

      // Tooltip interaction
      
      svg.on("mousemove", function(event) {
        // Store both relative (for hit test) and page (for display) coords
        lastMousePosition = {
            rel: d3.pointer(event, this),
            page: [event.pageX, event.pageY]
        };

        if (!currentStackedData || currentStackedData.length === 0) return;
        
        updateTooltip(lastMousePosition);
      })
      .on("mouseout", function() {
        lastMousePosition = null;
        tooltip.style("opacity", 0);
      });


      const width = size.width - margin.left - margin.right;
      const height = size.height - margin.top - margin.bottom;

      xScale = d3.scaleLinear().range([0, width]);
      yScale = d3.scaleLinear().range([height, 0]);

      xAxis = d3.axisBottom()
              .scale(xScale)
              .ticks(10);

      yAxis = d3.axisLeft()
              .scale(yScale)
              .ticks(5)
              .tickFormat(formatBitrate);

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
      const graph = svg.append("g")
         .attr("transform", "translate(" + margin.left + "," + margin.top + ")");

      graph.append("text")
         .attr("class", "title")
         .attr("text-anchor", "middle")
         .attr("x", width/2)
         .attr("y", 0 - margin.top / 2)
         .attr("dominant-baseline", "middle")
         .text("Top flows");

      graph.append("g")
         .attr("class", "x axis")
         .attr("transform", "translate(0," + height + ")")
         .call(xAxis);

      graph.append("text")
           .attr("class", "axis-label")
           .attr("text-anchor", "middle")
           .attr("x", width/2)
           .attr("y", height + 35)
           .text("Time");

      graph.append("g")
         .attr("class", "y axis")
         .call(yAxis);

      graph.append("text")
         .attr("class", "axis-label")
         .attr("transform", "rotate(-90)")
         .attr("y", 0 - margin.left)
         .attr("x", 0 - (height / 2))
         .attr("dy", "1em")
         .style("text-anchor", "middle")
         .text("Bitrate");

      graph.append("g")
        .attr("class", "xGrid")
        .attr("transform", "translate(0," + height + ")")
        .call(xGrid);

      graph.append("g")
        .attr("class", "yGrid")
        .call(yGrid);

      context.clearRect(0, 0, width, height);

      // Initialize area generator (reused across redraws)
      area = d3.area()
               .curve(d3.curveMonotoneX)
               .context(context)
               .x(d => xScale(d.data.ts))
               .y0(d => yScale(d[0] || 0))
               .y1(d => yScale(d[1] || 0));

      svg.append("g")
         .attr("class", "barsbox")
         .attr("id", "barsbox")
         .append("text")
           .attr("x", 0)
           .attr("y", 35)
           .style("font-size", "12px")
           .text("Byte Distribution")

      // Initialize the HTML legend header
      const legendContainer = d3.select("#toptalkLegendContainer");
      legendContainer.selectAll("*").remove(); // Clear any existing content

      // Create a table-like structure for the legend
      // (Header is now static in HTML)

      // Invalidate cached fkeys to force legend rebuild on next redraw
      cachedFkeys = [];
    };

    /* Reformat chartData to work with the new d3 v7 API
     * Ref: https://github.com/d3/d3-shape/blob/master/README.md#stack */
    const formatDataAndGetMaxSlice = function(chartData) {
      // Use a Map for O(1) indexed lookups, which is much faster than map().indexOf().
      const binsMap = new Map();
      let maxSlice = 0;
      const periodSec = JT.charts.getChartPeriod() / 1000.0;

      for (let i = 0; i < chartData.length; i++) {
        const row = chartData[i];
        for (let j = 0; j < row.values.length; j++) {
          const o = row.values[j];
          const ts = o.data ? o.data.ts : o.ts; // Handle potential pre-wrapped data
          const fkey = row.fkey;
          const bytes = o.bytes; // bytes is Bytes/sec (rate) from the server
          // Calculate bps: bytes * 8 bits/byte
          const bps = bytes * 8;

          // Check if we have seen this timestamp before.
          if (!binsMap.has(ts)) {
            // If not, create a new entry for it in the map.
            binsMap.set(ts, { "ts": ts });
          }

          // Get the bin for the current timestamp.
          const bin = binsMap.get(ts);
          bin[fkey] = (bin[fkey] || 0) + bps;
        }
      }

      const formattedData = Array.from(binsMap.values());

      // Ensure data is sorted by timestamp for d3.stack and bisect to work correctly
      formattedData.sort((a, b) => a.ts - b.ts);

      // Calculate the sum of each time slice to find the maximum for the Y-axis domain.
      formattedData.forEach(slice => {
        let currentSliceSum = 0;
        for (const key in slice) {
          if (key !== 'ts') {
            currentSliceSum += slice[key];
          }
        }
        if (currentSliceSum > maxSlice) {
          maxSlice = currentSliceSum;
        }
      });

      // Convert the map's values back into an array for d3.stack().
      return { formattedData, maxSlice };
    }

    const getFlowColor = (key) => {
      if (key === 'other') {
        return '#cccccc'; // a neutral grey
      }
      return colorScale(key);
    };

    /* Parse flow key into component parts */
    const parseFlowKey = (fkey) => {
      const parts = fkey.split('/');
      return {
        sourceIP: parts[1],
        sourcePort: parts[2],
        destIP: parts[3],
        destPort: parts[4],
        proto: parts[5],
        tclass: parts[6]
      };
    };

    /* Check if two arrays are equal */
    const arraysEqual = (a, b) => {
      if (a.length !== b.length) return false;
      for (let i = 0; i < a.length; i++) {
        if (a[i] !== b[i]) return false;
      }
      return true;
    };

    /* Update the legend DOM - only called when flows change */
    const updateLegend = (fkeys) => {
      const legendContainer = d3.select("#toptalkLegendContainer");

      // Remove old rows
      legendContainer.selectAll(".legend-row").remove();

      // Data join for new rows
      const rows = legendContainer.selectAll(".legend-row")
        .data(fkeys, d => d);

      const rowsEnter = rows.enter()
        .append("div")
        .attr("class", "legend-row d-flex align-items-center mb-1 legend-text");

      // Color box
      rowsEnter.append("div")
        .classed("legend-color-box flex-shrink-0", true)
        .style("background-color", d => getFlowColor(d));

      // Content
      rowsEnter.each(function(d) {
        const row = d3.select(this);
        if (d === 'other') {
          row.append("div").classed("col", true).style("padding-left", "10px").text("Other Flows");
        } else {
          const flow = parseFlowKey(d);

          row.append("div").style("width", "38%").classed("text-right pr-2", true).style("white-space", "nowrap").text(flow.sourceIP + ":" + flow.sourcePort);
          row.append("div").style("width", "5%").classed("text-center flex-shrink-0", true).text("->");
          row.append("div").style("width", "38%").classed("text-left pl-2", true).style("white-space", "nowrap").text(flow.destIP + ":" + flow.destPort);
          row.append("div").style("width", "9%").classed("flex-shrink-0", true).text(flow.proto);
          row.append("div").style("width", "10%").classed("flex-shrink-0", true).text(flow.tclass);
        }
      });
    };


    /* Update the chart (try to avoid memory allocations here!) */
    m.redraw = function() {

      // Process the raw chartData to aggregate "other" flows before drawing
      const processedChartData = processAndAggregateChartData(chartData);

      const width = size.width - margin.left - margin.right;
      const height = size.height - margin.top - margin.bottom;

      // Update xScale range instead of recreating
      xScale.range([0, width]);
      /* compute the domain of x as the [min,max] extent of timestamps
       * of the first (largest) flow */
      if (processedChartData && processedChartData[0])
        xScale.domain(d3.extent(processedChartData[0].values, d => d.ts));

      const { formattedData, maxSlice } = formatDataAndGetMaxSlice(processedChartData);

      const yPow = d3.select('input[name="y-axis-is-log"]:checked').node().value;

      // Check if we need to switch scale types
      const needsPowerScale = (yPow == 1);
      const isPowerScale = yScale.exponent !== undefined;

      if (needsPowerScale !== isPowerScale) {
        // Only recreate if switching between linear and power
        if (needsPowerScale) {
          yScale = d3.scalePow().exponent(0.5).clamp(true);
        } else {
          yScale = d3.scaleLinear().clamp(true);
        }
      }

      // Update range and domain
      yScale.range([height, 0]).domain([0, maxSlice]);

      xAxis.scale(xScale);
      yAxis.scale(yScale);

      xGrid.scale(xScale);
      yGrid.scale(yScale);

      svg = d3.select("#chartToptalk");

      svg.select(".x.axis").transition().duration(20).call(xAxis);
      svg.select(".y.axis").transition().duration(20).call(yAxis);
      svg.select(".xGrid").call(xGrid);
      svg.select(".yGrid").call(yGrid);

      const fkeys = processedChartData.map(f => f.fkey);
      colorScale.domain(fkeys); // Set the domain for the ordinal scale
      
      stack.keys(fkeys);

      // Format the data, so they're flat arrays
      const stackedChartData = stack(formattedData);
      currentStackedData = stackedChartData; // Expose for hit-testing

      // Area generator is initialized in reset(), just clear and draw
      context.clearRect(0, 0, width, height);

      stackedChartData.forEach(layer => {
        context.beginPath();
        area(layer);
        context.fillStyle = getFlowColor(layer.key);
        context.fill();
      });

      // distribution bar
      const tbytes = processedChartData.reduce((sum, f) => sum + f.tbytes, 0);

      let rangeStop = 0;
      const barData = processedChartData.map(f => {
        const new_d = {
          k: f.fkey,
          x0: rangeStop,
          x1: (rangeStop + f.tbytes)
        };
        rangeStop = new_d.x1;
        return new_d;
      });

      const x = d3.scaleLinear()
                      .rangeRound([0, width])
                      .domain([0,tbytes]);

      const y = d3.scaleBand()
                      .range([0, 10])
                      .round(.3);

      const barsbox = svg.select("#barsbox");
      barsbox.selectAll(".subbar").remove();
      const bars = barsbox.selectAll("rect")
                    .data(barData)
                    .enter().append("g").attr("class", "subbar");

      bars.append("rect")
          .attr("height", 12)
          .attr("y", 9)
          .attr("x", d => x(d.x0))
          .attr("width", d => x(d.x1) - x(d.x0))
          .style("fill", d => getFlowColor(d.k));

      barsbox.attr("transform",
                   "translate(" + margin.left + "," + (height + 55) + ")");

      // Only update legend when flow list changes
      if (!arraysEqual(fkeys, cachedFkeys)) {
        updateLegend(fkeys);
        cachedFkeys = fkeys.slice(); // Cache a copy
      }

      // Update tooltip if active
      if (lastMousePosition) {
          updateTooltip(lastMousePosition);
      }
    };


    /* Set the callback for resizing the chart */
    d3.select(window).on('resize.chartToptalk', function() {
      clearTimeout(resizeTimer);
      resizeTimer = setTimeout(function() {
        m.reset();
        my.charts.setDirty();
      }, 100);
    });

    return m;

  }({}));

})(JT);
/* End of jittertrap-chart-toptalk.js */
