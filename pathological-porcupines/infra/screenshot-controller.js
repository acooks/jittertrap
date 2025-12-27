#!/usr/bin/env node
/**
 * screenshot-controller.js - Puppeteer-based screenshot capture for JitterTrap
 *
 * Usage: node screenshot-controller.js <fifo-path> <output-dir> <test-path> <config-json>
 *
 * Listens on named pipe for commands:
 *   INIT                 - Initialize browser, navigate to JitterTrap, wait for data
 *   CAPTURE <view>       - Capture screenshot of specified view
 *   CAPTURE_ALL          - Capture all views from test config
 *   SHUTDOWN             - Close browser, write metadata, exit
 */

const puppeteer = require('puppeteer');
const fs = require('fs');
const path = require('path');
const readline = require('readline');

// JitterTrap URL (observer namespace management IP)
const JITTERTRAP_URL = 'http://10.0.0.2:8080';

// View definitions - selectors and tab navigation
const VIEWS = {
  throughput: {
    tab: '#showTputPanel',
    selector: '#chartThroughput',
    name: 'Throughput Chart',
    fullPage: false
  },
  toptalk: {
    tab: '#showTopTalkPanel',
    selector: '#chartToptalk',
    name: 'Top Talkers Chart',
    fullPage: false
  },
  rtt: {
    tab: '#showTopTalkPanel',
    selector: '#chartRtt',
    name: 'RTT Chart',
    fullPage: false
  },
  window: {
    tab: '#showTopTalkPanel',
    selector: '#chartWindow',
    name: 'TCP Window Chart',
    fullPage: false
  },
  legend: {
    tab: '#showTopTalkPanel',
    selector: '#toptalkLegendContainer',
    name: 'Flow Legend',
    fullPage: false
  },
  pgaps: {
    tab: '#showTputPanel',
    selector: '#packetGapContainer',
    name: 'Inter-Packet Gap Chart',
    fullPage: false
  },
  flowdetails: {
    tab: '#showTopTalkPanel',
    selector: '#toptalkLegendContainer',  // Capture the whole legend container which includes expanded details
    name: 'Flow Details (with IPG Histogram)',
    fullPage: false,
    expandFlow: true  // Special flag to expand flow before capture
  },
  fullpage: {
    tab: null,
    selector: null,
    name: 'Full Page',
    fullPage: true
  }
};

class ScreenshotController {
  constructor(fifoPath, outputDir, testPath, config) {
    this.fifoPath = fifoPath;
    this.outputDir = outputDir;
    this.testPath = testPath;
    this.config = config;
    this.browser = null;
    this.page = null;
    this.captures = [];
    this.viewCounters = {};
    this.initialized = false;
    this.currentTab = null;
    this.commandQueue = [];
    this.processing = false;
  }

  log(message) {
    const timestamp = new Date().toISOString().split('T')[1].slice(0, 12);
    console.log(`[${timestamp}] [screenshot] ${message}`);
  }

  async selectInterface(interfaceName) {
    try {
      // JitterTrap has an interface selector dropdown
      // Look for the select element and choose the interface
      const selector = '#dev_select';  // Interface dropdown ID
      await this.page.waitForSelector(selector, { timeout: 5000 });

      // Get available options
      const options = await this.page.$$eval(`${selector} option`, opts =>
        opts.map(o => ({ value: o.value, text: o.textContent }))
      );
      this.log(`Available interfaces: ${options.map(o => o.value).join(', ')}`);

      // Select the interface
      const found = options.find(o => o.value === interfaceName || o.text.includes(interfaceName));
      if (found) {
        await this.page.select(selector, found.value);

        // Call JitterTrap's dev_select function to send WebSocket message
        await this.page.evaluate(() => {
          if (typeof JT !== 'undefined' && JT.ws && JT.ws.dev_select) {
            JT.ws.dev_select();
          } else {
            // Fallback: trigger change event
            const el = document.querySelector('#dev_select');
            if (el) el.dispatchEvent(new Event('change', { bubbles: true }));
          }
        });

        this.log(`Selected interface: ${found.value}`);
        // Wait for data to start flowing
        await new Promise(r => setTimeout(r, 2000));
      } else {
        this.log(`Interface ${interfaceName} not found, available: ${options.map(o => o.value).join(', ')}`);
      }
    } catch (e) {
      this.log(`Could not select interface: ${e.message}`);
    }
  }

  async init() {
    this.log('Launching browser (headed mode for chart animations)...');

    try {
      // Use headed mode (visible browser) because headless Chrome has issues
      // with requestAnimationFrame which JitterTrap uses for chart updates
      // Match demo-automation's puppeteer options that are known to work
      this.browser = await puppeteer.launch({
        headless: false,
        defaultViewport: null,  // Use browser's natural viewport like demo-automation
        args: [
          '--no-sandbox',
          '--disable-setuid-sandbox',
          '--disable-dev-shm-usage',
          '--start-maximized',
          '--window-size=1920,1080',
          '--window-position=0,0'
        ]
      });

      this.page = await this.browser.newPage();
      // Set viewport after page creation for consistent sizing
      await this.page.setViewport({ width: 1920, height: 1080 });

      // Log browser errors and warnings
      this.page.on('console', msg => {
        const type = msg.type();
        if (type === 'error' || type === 'warning') {
          this.log(`Browser ${type}: ${msg.text()}`);
        }
      });

      this.log(`Navigating to ${JITTERTRAP_URL}...`);
      await this.page.goto(JITTERTRAP_URL, {
        waitUntil: 'networkidle2',
        timeout: 30000
      });

      // Bring page to front to ensure animations run (browsers throttle background tabs)
      await this.page.bringToFront();

      // Wait for JitterTrap UI to be fully loaded
      this.log('Waiting for JitterTrap UI to initialize...');
      try {
        // Wait for the main chart containers to exist
        await this.page.waitForSelector('#chartThroughput', { timeout: 10000 });
        await this.page.waitForSelector('#chartToptalk', { timeout: 5000 });

        // Wait for the WebSocket connection to establish and data to flow
        // Check for SVG elements inside charts (indicates D3 has rendered)
        await this.page.waitForSelector('#chartThroughput svg', { timeout: 10000 });
        await this.page.waitForSelector('#chartToptalk svg', { timeout: 5000 });

        this.log('Chart containers ready');

        // Select veth-src interface (traffic from source namespace)
        await this.selectInterface('veth-src');

      } catch (e) {
        this.log(`Warning: Timeout waiting for charts, continuing anyway: ${e.message}`);
      }

      // Wait for time series data to accumulate
      const waitTime = (this.config.data_accumulation_sec || 10) * 1000;
      this.log(`Waiting ${waitTime / 1000}s for data to accumulate...`);
      await new Promise(r => setTimeout(r, waitTime));

      this.initialized = true;
      this.log('READY');

      // Create ready file to signal the test runner
      const readyFile = path.join(this.outputDir, '.ready');
      fs.writeFileSync(readyFile, new Date().toISOString());
      this.log(`Ready signal written to ${readyFile}`);

    } catch (error) {
      this.log(`Initialization error: ${error.message}`);
      throw error;
    }
  }

  async clickTab(tabSelector) {
    if (this.currentTab === tabSelector) {
      return; // Already on this tab
    }

    try {
      await this.page.waitForSelector(tabSelector, { timeout: 5000 });

      // Use evaluate for more reliable clicking (from demo-automation patterns)
      await this.page.$eval(tabSelector, el => {
        el.click();
        // Force visual state
        el.classList.add('active');
        const siblings = el.closest('.nav')?.querySelectorAll('.nav-link');
        if (siblings) {
          siblings.forEach(sib => {
            if (sib !== el) sib.classList.remove('active');
          });
        }
      });

      this.currentTab = tabSelector;
      // Wait for tab transition
      await new Promise(r => setTimeout(r, 300));

    } catch (error) {
      this.log(`Tab click warning: ${error.message}`);
    }
  }

  async expandFirstFlow() {
    // Expand the first flow's health details
    // The D3 click handler calls stopPropagation, but we need to invoke it
    // without the event bubbling to the parent row's click handler
    try {
      // Wait for legend rows to be present
      await this.page.waitForSelector('.toptalk-legend-row', { timeout: 5000 });

      // Get D3's internal event handlers and call the health icon's click handler directly
      const result = await this.page.evaluate(() => {
        const rows = document.querySelectorAll('.toptalk-legend-row');
        for (const row of rows) {
          const healthIcon = row.querySelector('.health-icon');
          if (healthIcon && window.getComputedStyle(healthIcon).cursor === 'pointer') {
            // Access D3's internal __on property to get the click handler
            const d3Handlers = healthIcon.__on;
            if (d3Handlers) {
              const clickHandler = d3Handlers.find(h => h.type === 'click');
              if (clickHandler && clickHandler.value) {
                const detailsBefore = document.querySelectorAll('.health-details-row-container').length;

                // Create a fake event that won't bubble
                const fakeEvent = {
                  stopPropagation: () => {},
                  preventDefault: () => {},
                  target: healthIcon,
                  currentTarget: healthIcon
                };
                // Call the handler directly with the fake event
                clickHandler.value.call(healthIcon, fakeEvent);

                const detailsAfter = document.querySelectorAll('.health-details-row-container').length;

                return {
                  triggered: true,
                  fkey: row.getAttribute('data-fkey'),
                  method: 'd3-direct',
                  detailsBefore,
                  detailsAfter,
                  iconTextAfter: healthIcon.textContent
                };
              }
            }
            return { triggered: false, reason: 'no D3 click handler found' };
          }
        }
        return { triggered: false, reason: 'no clickable health icon' };
      });

      this.log(`Expand result: ${JSON.stringify(result)}`);

      if (!result.triggered) {
        this.log(`Could not trigger expansion: ${result.reason}`);
        return false;
      }

      // Return true if the handler created the details row (detailsAfter > detailsBefore)
      // Don't wait - the animation loop may remove it
      return result.detailsAfter > result.detailsBefore;
    } catch (e) {
      this.log(`Could not expand flow: ${e.message}`);
      return false;
    }
  }

  async captureView(viewId) {
    if (!this.initialized) {
      this.log('Error: Not initialized, cannot capture');
      return null;
    }

    const view = VIEWS[viewId];
    if (!view) {
      this.log(`Unknown view: ${viewId}`);
      return null;
    }

    this.log(`Capturing view: ${view.name}`);

    try {
      // Navigate to correct tab if needed
      if (view.tab) {
        await this.clickTab(view.tab);
      }

      // Note: flow expansion is now done upfront in captureAll() before any captures

      // Generate filename
      this.viewCounters[viewId] = (this.viewCounters[viewId] || 0) + 1;
      const testSlug = this.testPath.replace(/\//g, '_');
      const filename = `${testSlug}_${viewId}_${this.viewCounters[viewId]}.png`;
      const filepath = path.join(this.outputDir, filename);

      if (view.fullPage) {
        // Full page screenshot
        await this.page.screenshot({ path: filepath, fullPage: false });
      } else {
        // Wait for element - use longer timeout for expanded views
        const waitTimeout = view.expandFlow ? 10000 : 5000;
        await this.page.waitForSelector(view.selector, {
          visible: true,
          timeout: waitTimeout
        });

        // Small delay for chart rendering
        await new Promise(r => setTimeout(r, 200));

        // Element screenshot
        const element = await this.page.$(view.selector);
        if (element) {
          await element.screenshot({ path: filepath });
        } else {
          this.log(`Element not found: ${view.selector}`);
          return null;
        }
      }

      const captureRecord = {
        file: filename,
        view: viewId,
        view_name: view.name,
        captured_at: new Date().toISOString()
      };
      this.captures.push(captureRecord);

      this.log(`Captured: ${filename}`);
      return captureRecord;

    } catch (error) {
      this.log(`Capture error for ${viewId}: ${error.message}`);
      return null;
    }
  }

  async captureAll() {
    const views = this.config.views || ['toptalk', 'throughput'];

    // Wait a moment for charts to update with final data
    this.log('Waiting 2s for charts to update...');
    await new Promise(r => setTimeout(r, 2000));

    // Check if any view needs flow expansion - if so, do it FIRST before any captures
    const needsExpansion = views.some(v => {
      const viewId = typeof v === 'string' ? v : v.id;
      return VIEWS[viewId]?.expandFlow;
    });

    // Reorder views: capture flowdetails FIRST if it needs expansion, to avoid tab switches destroying it
    let reorderedViews = [...views];
    if (needsExpansion) {
      const flowdetailsIdx = reorderedViews.findIndex(v => {
        const viewId = typeof v === 'string' ? v : v.id;
        return VIEWS[viewId]?.expandFlow;
      });
      if (flowdetailsIdx > 0) {
        const [flowdetailsView] = reorderedViews.splice(flowdetailsIdx, 1);
        reorderedViews.unshift(flowdetailsView);
      }

      this.log('Expanding flow before capturing (required for flowdetails view)');
      // Navigate to Top Talkers panel first
      await this.clickTab('#showTopTalkPanel');

      // Stop updates FIRST, then expand
      // Do both in a single evaluate to avoid race conditions
      await this.page.evaluate(() => {
        // Disable WebSocket messages immediately
        if (window.JT && window.JT.ws && window.JT.ws.ws) {
          window.JT.ws.ws.onmessage = () => {};
        }
        // Click pause button to stop animation loop
        const pauseBtn = document.querySelector('#pauseChartsBtn');
        if (pauseBtn && !pauseBtn.textContent.includes('Run')) {
          pauseBtn.click();
        }
      });
      this.log('Paused updates');

      // Small delay to let the animation loop stop
      await new Promise(r => setTimeout(r, 100));

      // Now expand
      await this.expandFirstFlow();
    }

    this.log(`Capturing all configured views: ${reorderedViews.join(', ')}`);

    for (const viewConfig of reorderedViews) {
      const viewId = typeof viewConfig === 'string' ? viewConfig : viewConfig.id;
      await this.captureView(viewId);
    }
  }

  async shutdown() {
    this.log('Shutting down...');

    // Save metadata
    const metadataPath = path.join(this.outputDir, 'metadata.json');
    const metadata = {
      timestamp: new Date().toISOString(),
      test_path: this.testPath,
      jittertrap_url: JITTERTRAP_URL,
      config: this.config,
      captures: this.captures
    };

    try {
      fs.writeFileSync(metadataPath, JSON.stringify(metadata, null, 2));
      this.log(`Metadata saved to ${metadataPath}`);
    } catch (error) {
      this.log(`Error saving metadata: ${error.message}`);
    }

    // Close browser
    if (this.browser) {
      try {
        await this.browser.close();
        this.log('Browser closed');
      } catch (error) {
        this.log(`Error closing browser: ${error.message}`);
      }
    }

    this.log(`Shutdown complete. ${this.captures.length} screenshots captured.`);
    // Use setTimeout to allow stdout to flush before exiting
    setTimeout(() => process.exit(0), 100);
  }

  async processCommand(line) {
    const trimmed = line.trim();
    if (!trimmed) return;

    const [cmd, ...args] = trimmed.split(' ');

    switch (cmd.toUpperCase()) {
      case 'INIT':
        await this.init();
        break;

      case 'CAPTURE':
        if (args.length > 0) {
          await this.captureView(args[0]);
        } else {
          this.log('CAPTURE requires a view argument');
        }
        break;

      case 'CAPTURE_ALL':
        await this.captureAll();
        break;

      case 'SHUTDOWN':
        await this.shutdown();
        break;

      default:
        this.log(`Unknown command: ${cmd}`);
    }
  }

  async processQueue() {
    if (this.processing || this.commandQueue.length === 0) {
      return;
    }

    this.processing = true;

    while (this.commandQueue.length > 0) {
      const line = this.commandQueue.shift();
      try {
        await this.processCommand(line);
      } catch (error) {
        this.log(`Error processing command: ${error.message}`);
      }
    }

    this.processing = false;
  }

  queueCommand(line) {
    this.commandQueue.push(line);
    // Process queue (will no-op if already processing)
    this.processQueue().catch(err => {
      this.log(`Queue error: ${err.message}`);
    });
  }

  async listen() {
    this.log(`Listening on FIFO: ${this.fifoPath}`);

    // Open FIFO for reading (blocks until writer connects)
    const fifoFd = fs.openSync(this.fifoPath, 'r');
    const fifoStream = fs.createReadStream(null, { fd: fifoFd });

    const rl = readline.createInterface({
      input: fifoStream,
      crlfDelay: Infinity
    });

    rl.on('line', (line) => {
      // Queue commands for serial processing
      this.queueCommand(line);
    });

    rl.on('close', () => {
      this.log('FIFO closed, shutting down...');
      // Queue shutdown to process after pending commands
      this.queueCommand('SHUTDOWN');
    });

    rl.on('error', (error) => {
      this.log(`FIFO error: ${error.message}`);
    });
  }
}

// Entry point
async function main() {
  const args = process.argv.slice(2);

  if (args.length < 4) {
    console.error('Usage: screenshot-controller.js <fifo-path> <output-dir> <test-path> <config-json>');
    process.exit(1);
  }

  const [fifoPath, outputDir, testPath, configJson] = args;

  // Parse config
  let config;
  try {
    config = JSON.parse(configJson);
  } catch (error) {
    console.error(`Error parsing config JSON: ${error.message}`);
    process.exit(1);
  }

  // Ensure output directory exists
  if (!fs.existsSync(outputDir)) {
    fs.mkdirSync(outputDir, { recursive: true });
  }

  // Create and start controller
  const controller = new ScreenshotController(fifoPath, outputDir, testPath, config);
  await controller.listen();
}

main().catch(error => {
  console.error(`Fatal error: ${error.message}`);
  process.exit(1);
});
