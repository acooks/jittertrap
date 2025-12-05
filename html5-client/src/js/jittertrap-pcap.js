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

  /* Current state */
  let pcapState = {
    state: PCAP_STATE.DISABLED,
    totalPackets: 0,
    totalBytes: 0,
    droppedPackets: 0,
    bufferPercent: 0,
    oldestAgeSec: 0
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
   * Update capture button state based on pcap state
   */
  const updateCaptureButton = function() {
    const btn = $('#pcap-trigger-btn');

    /* Update tooltip with buffer status */
    let tooltip = '';
    if (pcapState.state === PCAP_STATE.RECORDING) {
      tooltip = 'Buffer: ' + pcapState.oldestAgeSec + 's, ' +
                formatBytes(pcapState.totalBytes) + ', ' +
                formatNumber(pcapState.totalPackets) + ' packets';
    } else if (pcapState.state === PCAP_STATE.DISABLED) {
      tooltip = 'Recording disabled - select an interface';
    } else {
      tooltip = 'Capture in progress...';
    }
    btn.attr('title', tooltip);

    /* Update status badge */
    const statusBadge = $('#pcap-status-badge');
    const statusText = $('#pcap-status');
    switch (pcapState.state) {
    case PCAP_STATE.DISABLED:
      statusBadge.removeClass('badge-success badge-warning').addClass('badge-secondary');
      statusText.text('Disabled');
      break;
    case PCAP_STATE.RECORDING:
      statusBadge.removeClass('badge-secondary badge-warning').addClass('badge-success');
      statusText.text('Recording');
      break;
    case PCAP_STATE.TRIGGERED:
    case PCAP_STATE.WRITING:
      statusBadge.removeClass('badge-secondary badge-success').addClass('badge-warning');
      statusText.text('Capturing...');
      break;
    }

    /* Update buffer time and statistics */
    $('#pcap-buffer-time').text(pcapState.oldestAgeSec);
    $('#pcap-packets').text(formatNumber(pcapState.totalPackets));
    $('#pcap-bytes').text(formatBytes(pcapState.totalBytes));

    switch (pcapState.state) {
    case PCAP_STATE.DISABLED:
      btn.prop('disabled', true)
         .removeClass('btn-warning btn-success')
         .addClass('btn-secondary')
         .html('<i class="fas fa-camera"></i> Capture');
      break;
    case PCAP_STATE.RECORDING:
      btn.prop('disabled', false)
         .removeClass('btn-warning btn-success')
         .addClass('btn-secondary')
         .html('<i class="fas fa-camera"></i> Capture');
      break;
    case PCAP_STATE.TRIGGERED:
      btn.prop('disabled', true)
         .removeClass('btn-secondary btn-success')
         .addClass('btn-warning')
         .html('<i class="fas fa-spinner fa-spin"></i> Capturing...');
      break;
    case PCAP_STATE.WRITING:
      btn.prop('disabled', true)
         .removeClass('btn-secondary btn-success')
         .addClass('btn-warning')
         .html('<i class="fas fa-spinner fa-spin"></i> Writing...');
      break;
    }
  };

  /**
   * Show brief visual feedback on the capture button
   */
  const flashCaptureButton = function() {
    const btn = $('#pcap-trigger-btn');
    btn.addClass('btn-success').removeClass('btn-secondary');
    setTimeout(function() {
      updateCaptureButton();
    }, 200);
  };

  /**
   * Handle pcap_status message from server
   */
  my.pcapModule.updateStatus = function(params) {
    pcapState.state = params.state;
    pcapState.totalPackets = params.total_packets;
    pcapState.totalBytes = params.total_bytes;
    pcapState.droppedPackets = params.dropped_packets;
    pcapState.bufferPercent = params.buffer_percent;
    pcapState.oldestAgeSec = params.oldest_age_sec;

    updateCaptureButton();
  };

  /**
   * Handle pcap_config message from server (unused but keep for protocol)
   */
  my.pcapModule.updateConfig = function(params) {
    /* Config handled server-side with defaults */
    void(params);
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

    /* Brief success indication on button */
    const btn = $('#pcap-trigger-btn');
    btn.removeClass('btn-warning btn-secondary')
       .addClass('btn-success')
       .html('<i class="fas fa-check"></i> ' + formatNumber(params.packet_count) + ' pkts');

    /* Reset after 2 seconds */
    setTimeout(function() {
      updateCaptureButton();
    }, 2000);
  };

  /**
   * Send manual trigger request with visual feedback
   */
  my.pcapModule.triggerManual = function() {
    if (pcapState.state !== PCAP_STATE.RECORDING) {
      console.log('[PCAP] Cannot trigger - not recording');
      return;
    }
    flashCaptureButton();
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
   * Enable pcap recording (called automatically on interface select)
   */
  my.pcapModule.enable = function() {
    const config = {
      enabled: 1,
      max_memory_mb: 256,
      duration_sec: 30,
      pre_trigger_sec: 30,
      post_trigger_sec: 0
    };
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
   * Apply configuration from the settings form
   */
  my.pcapModule.applyConfig = function() {
    const config = {
      enabled: 1,
      max_memory_mb: 256,
      duration_sec: 30,
      pre_trigger_sec: parseInt($('#pcap_pre_trigger').val()) || 30,
      post_trigger_sec: parseInt($('#pcap_post_trigger').val()) || 0
    };

    /* Validate */
    if (config.pre_trigger_sec + config.post_trigger_sec > config.duration_sec) {
      alert('Pre + Post trigger time cannot exceed 30 seconds');
      return;
    }

    JT.ws.pcap_config(config);
  };

  /**
   * Initialize UI event handlers
   */
  my.pcapModule.initUI = function() {
    /* Manual trigger button */
    $('#pcap-trigger-btn').on('click', function() {
      my.pcapModule.triggerManual();
    });

    /* Apply config button */
    $('#pcap-apply-config').on('click', function() {
      my.pcapModule.applyConfig();
    });
  };

})(JT);
