/* jittertrap-pcap.js */

/* global JT:true */

((my) => {
  'use strict';

  my.pcapModule = {};

  /* PCAP buffer states (must match pcap_buf_state_t in C) */
  const PCAP_STATE = {
    DISABLED: 0,
    RECORDING: 1,
    TRIGGERED: 2,
    WRITING: 3
  };

  const stateNames = {
    [PCAP_STATE.DISABLED]: 'Disabled',
    [PCAP_STATE.RECORDING]: 'Recording',
    [PCAP_STATE.TRIGGERED]: 'Triggered',
    [PCAP_STATE.WRITING]: 'Writing...'
  };

  /* Current state */
  let pcapState = {
    state: PCAP_STATE.DISABLED,
    totalPackets: 0,
    totalBytes: 0,
    droppedPackets: 0,
    memoryUsedMb: 0,
    bufferPercent: 0,
    oldestAgeSec: 0
  };

  /* Current config */
  let pcapConfig = {
    enabled: false,
    maxMemoryMb: 256,
    durationSec: 30,
    preTriggerSec: 25,
    postTriggerSec: 5
  };

  /**
   * Format bytes to human readable string
   */
  const formatBytes = function(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB';
    if (bytes < 1073741824) return (bytes / 1048576).toFixed(1) + ' MB';
    return (bytes / 1073741824).toFixed(2) + ' GB';
  };

  /**
   * Format number with thousands separator
   */
  const formatNumber = function(num) {
    return num.toString().replace(/\B(?=(\d{3})+(?!\d))/g, ",");
  };

  /**
   * Update UI elements based on current state
   */
  const updateUI = function() {
    const statusEl = $('#pcap-status');
    const statusBadge = $('#pcap-status-badge');
    const triggerBtn = $('#pcap-trigger-btn');
    const progressBar = $('#pcap-buffer-progress');
    const enableSwitch = $('#pcap_enabled');

    /* Update status text and badge */
    statusEl.text(stateNames[pcapState.state] || 'Unknown');

    switch (pcapState.state) {
    case PCAP_STATE.DISABLED:
      statusBadge.removeClass('badge-success badge-warning badge-info')
                 .addClass('badge-secondary');
      triggerBtn.prop('disabled', true);
      break;
    case PCAP_STATE.RECORDING:
      statusBadge.removeClass('badge-secondary badge-warning badge-info')
                 .addClass('badge-success');
      triggerBtn.prop('disabled', false);
      break;
    case PCAP_STATE.TRIGGERED:
    case PCAP_STATE.WRITING:
      statusBadge.removeClass('badge-secondary badge-success badge-info')
                 .addClass('badge-warning');
      triggerBtn.prop('disabled', true);
      break;
    }

    /* Update enable switch */
    enableSwitch.prop('checked', pcapState.state !== PCAP_STATE.DISABLED);

    /* Update progress bar */
    progressBar.css('width', pcapState.bufferPercent + '%');
    progressBar.attr('aria-valuenow', pcapState.bufferPercent);
    progressBar.text(pcapState.oldestAgeSec + 's');

    /* Update statistics */
    $('#pcap-packets').text(formatNumber(pcapState.totalPackets));
    $('#pcap-bytes').text(formatBytes(pcapState.totalBytes));
    $('#pcap-memory').text(pcapState.memoryUsedMb + ' MB');
    $('#pcap-dropped').text(formatNumber(pcapState.droppedPackets));
  };

  /**
   * Update config UI elements
   */
  const updateConfigUI = function() {
    $('#pcap_max_memory').val(pcapConfig.maxMemoryMb);
    $('#pcap_duration').val(pcapConfig.durationSec);
    $('#pcap_pre_trigger').val(pcapConfig.preTriggerSec);
    $('#pcap_post_trigger').val(pcapConfig.postTriggerSec);
  };

  /**
   * Handle pcap_status message from server
   */
  my.pcapModule.updateStatus = function(params) {
    pcapState.state = params.state;
    pcapState.totalPackets = params.total_packets;
    pcapState.totalBytes = params.total_bytes;
    pcapState.droppedPackets = params.dropped_packets;
    pcapState.memoryUsedMb = params.current_memory_mb;
    pcapState.bufferPercent = params.buffer_percent;
    pcapState.oldestAgeSec = params.oldest_age_sec;

    updateUI();
  };

  /**
   * Handle pcap_config message from server
   */
  my.pcapModule.updateConfig = function(params) {
    pcapConfig.enabled = params.enabled === 1;
    pcapConfig.maxMemoryMb = params.max_memory_mb;
    pcapConfig.durationSec = params.duration_sec;
    pcapConfig.preTriggerSec = params.pre_trigger_sec;
    pcapConfig.postTriggerSec = params.post_trigger_sec;

    updateConfigUI();
  };

  /**
   * Handle pcap_ready message - trigger file download
   */
  my.pcapModule.handleFileReady = function(params) {
    console.log('[PCAP] File ready:', params.filename,
                'size:', formatBytes(params.file_size),
                'packets:', params.packet_count);

    /* Create a temporary link and click it to download */
    const link = document.createElement('a');
    link.href = params.filename;
    link.download = params.filename.split('/').pop();
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);

    /* Show notification */
    my.pcapModule.showNotification(
      'PCAP ready: ' + formatNumber(params.packet_count) + ' packets, ' +
      formatBytes(params.file_size),
      'success'
    );
  };

  /**
   * Show notification to user
   */
  my.pcapModule.showNotification = function(message, type) {
    type = type || 'info';

    /* Use console for now, could be enhanced with toast notifications */
    console.log('[PCAP] ' + message);

    /* Try to use Bootstrap toast if available */
    const toastContainer = $('#pcap-toast-container');
    if (toastContainer.length) {
      const toastHtml = `
        <div class="toast" role="alert" aria-live="assertive" aria-atomic="true"
             data-delay="5000">
          <div class="toast-header">
            <strong class="mr-auto text-${type}">PCAP Capture</strong>
            <button type="button" class="ml-2 mb-1 close" data-dismiss="toast">
              <span>&times;</span>
            </button>
          </div>
          <div class="toast-body">${message}</div>
        </div>
      `;
      const toast = $(toastHtml);
      toastContainer.append(toast);
      toast.toast('show');
      toast.on('hidden.bs.toast', function() {
        $(this).remove();
      });
    }
  };

  /**
   * Send manual trigger request
   */
  my.pcapModule.triggerManual = function() {
    if (pcapState.state !== PCAP_STATE.RECORDING) {
      console.log('[PCAP] Cannot trigger - not recording');
      return;
    }
    JT.ws.pcap_trigger('Manual trigger');
  };

  /**
   * Send trigger request with custom reason
   */
  my.pcapModule.trigger = function(reason) {
    if (pcapState.state !== PCAP_STATE.RECORDING) {
      console.log('[PCAP] Cannot trigger - not recording');
      return;
    }
    JT.ws.pcap_trigger(reason || 'Programmatic trigger');
  };

  /**
   * Enable or disable pcap recording
   */
  my.pcapModule.setEnabled = function(enabled) {
    const config = {
      enabled: enabled ? 1 : 0,
      max_memory_mb: parseInt($('#pcap_max_memory').val()) || pcapConfig.maxMemoryMb,
      duration_sec: parseInt($('#pcap_duration').val()) || pcapConfig.durationSec,
      pre_trigger_sec: parseInt($('#pcap_pre_trigger').val()) || pcapConfig.preTriggerSec,
      post_trigger_sec: parseInt($('#pcap_post_trigger').val()) || pcapConfig.postTriggerSec
    };
    JT.ws.pcap_config(config);
  };

  /**
   * Apply configuration changes
   */
  my.pcapModule.applyConfig = function() {
    const config = {
      enabled: $('#pcap_enabled').is(':checked') ? 1 : 0,
      max_memory_mb: parseInt($('#pcap_max_memory').val()) || 256,
      duration_sec: parseInt($('#pcap_duration').val()) || 30,
      pre_trigger_sec: parseInt($('#pcap_pre_trigger').val()) || 25,
      post_trigger_sec: parseInt($('#pcap_post_trigger').val()) || 5
    };

    /* Validate */
    if (config.pre_trigger_sec + config.post_trigger_sec > config.duration_sec) {
      my.pcapModule.showNotification(
        'Pre + Post trigger time cannot exceed buffer duration',
        'warning'
      );
      return;
    }

    JT.ws.pcap_config(config);
  };

  /**
   * Get current state for external access
   */
  my.pcapModule.getState = function() {
    return pcapState.state;
  };

  /**
   * Check if currently recording
   */
  my.pcapModule.isRecording = function() {
    return pcapState.state === PCAP_STATE.RECORDING;
  };

  /**
   * Initialize UI event handlers
   */
  my.pcapModule.initUI = function() {
    /* Enable/disable toggle */
    $('#pcap_enabled').on('change', function() {
      my.pcapModule.setEnabled($(this).is(':checked'));
    });

    /* Manual trigger button */
    $('#pcap-trigger-btn').on('click', function() {
      my.pcapModule.triggerManual();
    });

    /* Apply config button */
    $('#pcap-apply-config').on('click', function() {
      my.pcapModule.applyConfig();
    });

    /* Settings toggle */
    $('#pcap-settings-toggle').on('click', function() {
      $('#pcap-settings-panel').collapse('toggle');
    });
  };

})(JT);
