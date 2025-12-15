/* jittertrap-websocket.js */

/* global JT:true */
/* global pako:false */

((my) => {
  'use strict';

  my.ws = {};

  let selectedIface = {}; // set on dev_select message

  /* the websocket object, see my.ws.init() */
  let sock = {};

  /* Compression header byte used by server */
  const WS_COMPRESS_HEADER = 0x01;

  /* Video segment header byte */
  /* Note: Video segments are now delivered via separate video WebSocket */

  /*
   * Preset dictionary for decompression - must match server's ws_dictionary.
   * Contains common strings in JitterTrap JSON messages, ordered from
   * less common (start) to most common (end) for zlib priority.
   */
  const WS_DICTIONARY = new TextEncoder().encode(
    /* Common IP prefixes and network values */
    '192.168.10.0.172.16.fe80:::ffff:' +
    /* Protocol names */
    'ICMPUDPTCP' +
    /* Traffic class */
    'BULKBECS0CS1' +
    /* Less frequent message types */
    'pcap_readypcap_statuspcap_configpcap_triggersample_period' +
    'netem_paramsdev_selectiface_list' +
    /* Video telemetry fields (less common - only video flows) */
    '"video_codec_source":' +
    '"video_bitrate_kbps":' +
    '"video_gop_frames":' +
    '"video_keyframes":' +
    '"video_frames":' +
    '"video_fps_x100":' +
    '"video_profile":' +
    '"video_level":' +
    '"video_width":' +
    '"video_height":' +
    '"video_jitter_hist":[' +
    '"video_jitter_us":' +
    '"video_seq_loss":' +
    '"video_cc_errors":' +
    '"video_codec":' +
    '"video_ssrc":' +
    '"video_type":' +
    /* Audio telemetry fields (less common - only audio flows) */
    '"audio_bitrate_kbps":' +
    '"audio_sample_rate":' +
    '"audio_jitter_us":' +
    '"audio_seq_loss":' +
    '"audio_codec":' +
    '"audio_ssrc":' +
    '"audio_type":' +
    /* TCP congestion/window fields */
    '"recent_events":' +
    '"retransmit_cnt":' +
    '"zero_window_cnt":' +
    '"dup_ack_cnt":' +
    '"ece_cnt":' +
    '"window_scale":' +
    '"rwnd_bytes":' +
    '"saw_syn":' +
    '"tcp_state":' +
    '"rtt_us":' +
    /* TCP health indicator fields */
    '"health_rtt_hist":[' +
    '"health_rtt_samples":' +
    '"health_status":' +
    '"health_flags":' +
    /* IPG histogram fields (medium frequency) */
    '"ipg_hist":[' +
    '"ipg_samples":' +
    '"ipg_mean_us":' +
    /* Frame size histogram fields (all flows) */
    '"frame_size_hist":[' +
    '"frame_size_samples":' +
    '"frame_size_variance":' +
    '"frame_size_mean":' +
    '"frame_size_min":' +
    '"frame_size_max":' +
    /* PPS histogram fields (all flows) */
    '"pps_hist":[' +
    '"pps_samples":' +
    '"pps_variance":' +
    '"pps_mean":' +
    /* Address fields */
    '"tclass":"' +
    '"proto":"' +
    '"dport":' +
    '"sport":' +
    '"dst":"' +
    '"src":"' +
    /* Stats message fields */
    '"mean_tx_packet_gap":' +
    '"mean_rx_packet_gap":' +
    '"max_tx_packet_gap":' +
    '"max_rx_packet_gap":' +
    '"min_tx_packet_gap":' +
    '"min_rx_packet_gap":' +
    '"sd_whoosh":' +
    '"max_whoosh":' +
    '"mean_whoosh":' +
    '"mean_tx_packets":' +
    '"mean_rx_packets":' +
    '"mean_tx_bytes":' +
    '"mean_rx_bytes":' +
    '"interval_ns":' +
    /* Toptalk aggregate fields */
    '"tpackets":' +
    '"tbytes":' +
    '"tflows":' +
    /* Most common field names */
    '"packets":' +
    '"bytes":' +
    '"flows":[' +
    /* Common histogram patterns (zeros are very frequent) */
    ',0,0,0,0' +
    '[0,0,0,0' +
    /* Most common JSON structure */
    '"iface":"' +
    '","p":{' +
    '"msg":"' +
    /* Most frequent message types (at the very end for priority) */
    'toptalk' +
    'stats' +
    '}}'
  );

  /* Maximum decompressed message size (prevent decompression bombs) */
  const MAX_DECOMPRESSED_SIZE = 64 * 1024;  /* 64 KB */

  /* Pre-allocated pako options to avoid GC pressure
   * Note: Cannot freeze - pako modifies the options object internally */
  const pakoOptions = {
    to: 'string',
    dictionary: WS_DICTIONARY
  };

  /**
   * Decompress deflate-compressed data using pako with preset dictionary
   * @param {Uint8Array} data - Compressed data (including header byte)
   * @returns {string} - Decompressed JSON string
   */
  const decompressMessage = function(data) {
    try {
      /* Skip the header byte and decompress using raw deflate with dictionary */
      const compressed = data.subarray(1);
      const decompressed = pako.inflateRaw(compressed, pakoOptions);

      /* Guard against decompression bombs */
      if (decompressed.length > MAX_DECOMPRESSED_SIZE) {
        console.error("Decompressed message too large:", decompressed.length);
        return null;
      }

      return decompressed;
    } catch (e) {
      console.error("Decompression failed:", e);
      return null;
    }
  };

  /**
   * Websocket Callback Functions
   * i.e. Referred to in websocket.onmessage
   */

  const handleMsgUpdateStats = function (params) {
    // params.s contains network statistics as rates per second (e.g., bytes/sec, packets/sec)
    // params.t contains the timestamp (tv_sec, tv_nsec) from the server
    const timestamp = params.t.tv_sec + params.t.tv_nsec / 1e9;
    JT.core.processDataMsg(params.s, params.ival_ns, timestamp);
    JT.charts.setDirty();
  };

  const handleMsgToptalk = function (params) {
    // params contains top talker data as rates (Bytes/sec),
    // calculated over the interval.
    JT.core.processTopTalkMsg(params);
    JT.charts.setDirty();
  };

  const handleMsgDevSelect = function(params) {
    const iface = params.iface;
    console.log("iface: " + iface);
    $('#dev_select').val(iface);
    selectedIface = $('#dev_select').val();
    JT.core.clearAllSeries();
    JT.charts.resetChart();

    /* Auto-enable PCAP recording when interface is selected */
    if (JT.pcapModule) {
      JT.pcapModule.enable();
    }
  };

  const handleMsgIfaces = function(params) {
    const ifaces = params.ifaces;
    $('#dev_select').empty();
    ifaces.forEach((val) => {
      const option = $('<option>').text(val).val(val);
      $('#dev_select').append(option);
    });
  };

  const handleMsgNetemParams = function(params) {
    JT.programsModule.processNetemMsg(params);
  };

  const handleMsgSamplePeriod = function(params) {
    const period = params.period;
    my.core.samplePeriod(period);
    $("#jt-measure-sample-period").html(period / 1000.0 + "ms");
    console.log("sample period: " + period);
    JT.core.clearAllSeries();
    JT.charts.resetChart();
  };

  const handleMsgPcapConfig = function(params) {
    if (JT.pcapModule) {
      JT.pcapModule.updateConfig(params);
    }
  };

  const handleMsgPcapStatus = function(params) {
    if (JT.pcapModule) {
      JT.pcapModule.updateStatus(params);
    }
  };

  const handleMsgPcapReady = function(params) {
    if (JT.pcapModule) {
      JT.pcapModule.handleFileReady(params);
    }
  };

  /* WebRTC message handlers */
  const handleMsgWebrtcAnswer = function(params) {
    if (JT.webrtc) {
      JT.webrtc.handleAnswer(params);
    }
  };

  const handleMsgWebrtcIce = function(params) {
    if (JT.webrtc) {
      JT.webrtc.handleIce(params);
    }
  };

  const handleMsgWebrtcStatus = function(params) {
    if (JT.webrtc) {
      JT.webrtc.handleStatus(params);
    }
  };

  const handleMsgError = function(params) {
    console.error("Server error:", params.code, "-", params.text);

    const errorMsgElement = $("#error-msg");
    const errorModalElement = $("#error-modal");

    let userMessage = params.text || "Unknown server error";

    /* Add specific guidance for known error codes */
    if (params.code === "max_connections") {
      userMessage += "<br><br><strong>Tip:</strong> Close other JitterTrap tabs or windows and refresh this page.";
    } else if (params.code === "too_slow") {
      userMessage += "<br><br><strong>Tip:</strong> Your connection may be too slow. Try using a faster network or reducing the sample rate.";
    }

    errorMsgElement.html("<p>" + userMessage + "</p>");
    errorModalElement.modal('show');
  };

  /**
   * Handle resolution message from server - disables interval options that
   * are faster than the current minimum supported interval.
   *
   * The server sends this when the client's connection speed requires
   * degrading to a slower sample rate tier.
   */
  const handleMsgResolution = function(params) {
    const minIntervalMs = params.min_interval_ms;
    console.log("Resolution update: min_interval_ms =", minIntervalMs);

    /* Disable interval options faster than current capability */
    $("#chopts_chartPeriod option").each(function() {
      const optionVal = parseInt($(this).val(), 10);
      /* Option value is in ms, same as minIntervalMs */
      $(this).prop('disabled', optionVal < minIntervalMs);
    });

    /* If currently selected interval is now disabled, switch to minimum available */
    const selectedVal = parseInt($("#chopts_chartPeriod").val(), 10);
    if (selectedVal < minIntervalMs) {
      console.log("Switching from disabled interval", selectedVal, "to", minIntervalMs);
      $("#chopts_chartPeriod").val(minIntervalMs).trigger('change');
    }

    /* Update the resolution indicator if present */
    const resolutionIndicator = $("#resolution-indicator");
    if (resolutionIndicator.length) {
      if (minIntervalMs > 5) {
        /* Show indicator when degraded from full resolution */
        resolutionIndicator.text(minIntervalMs + "ms min").show();
      } else {
        /* Hide when at full resolution */
        resolutionIndicator.hide();
      }
    }
  };


  /**
   * Websocket Sending Functions
   */

  const dev_select = function() {
    const msg = JSON.stringify({'msg':'dev_select',
                              'p': { 'iface': $("#dev_select").val()}});
    sock.send(msg);
  };

  const set_netem = function() {
    const msg = JSON.stringify(
      {'msg': 'set_netem',
       'p': {
         'dev': $("#dev_select").val(),
         'delay': parseInt($("#delay").val(), 10),
         'jitter': parseInt($("#jitter").val(), 10),
         /* convert the float to an integer representing 10ths of percent. */
         'loss': parseInt(Math.round(10 * $("#loss").val()), 10)
       }
      });
    sock.send(msg);
    return false;
  };

  const clear_netem = function() {
    $("#delay").val(0);
    $("#jitter").val(0);
    $("#loss").val(0);
    set_netem();
    return false;
  };

  const pcap_config = function(config) {
    const msg = JSON.stringify({
      'msg': 'pcap_config',
      'p': config
    });
    sock.send(msg);
  };

  const pcap_trigger = function(reason) {
    const msg = JSON.stringify({
      'msg': 'pcap_trigger',
      'p': { 'reason': reason || 'Manual trigger' }
    });
    sock.send(msg);
  };

  const messageHandlers = {
    stats: (params) => {
      if (params.iface === selectedIface) handleMsgUpdateStats(params);
    },
    toptalk: handleMsgToptalk,
    dev_select: handleMsgDevSelect,
    iface_list: handleMsgIfaces,
    netem_params: handleMsgNetemParams,
    sample_period: handleMsgSamplePeriod,
    pcap_config: handleMsgPcapConfig,
    pcap_status: handleMsgPcapStatus,
    pcap_ready: handleMsgPcapReady,
    webrtc_answer: handleMsgWebrtcAnswer,
    webrtc_ice: handleMsgWebrtcIce,
    webrtc_status: handleMsgWebrtcStatus,
    error: handleMsgError,
    resolution: handleMsgResolution,
  };

  Object.freeze(messageHandlers); // Prevent modification of messageHandlers

  /* Cache valid message types to avoid Object.keys() allocation on every message */
  const validMessageTypes = new Set(Object.keys(messageHandlers));

  my.ws.init = function(uri) {
    // Initialize WebSocket
    sock = new WebSocket(uri, "jittertrap");

    // Enable binary message support for compressed data
    sock.binaryType = 'arraybuffer';

    sock.onopen = function(evt) {
      const msg = JSON.stringify({'msg': 'hello'});
      sock.send(msg);
    };

    sock.onclose = function(evt) {
      console.log("unhandled websocket onclose event: " + evt);
      const errorMsgElement = $("#error-msg");
      const errorModalElement = $("#error-modal");
      errorMsgElement.html($("#error-msg").html()
                           + "<p>Websocket closed.</p>");
      $("#error-modal").modal('show');
    };

    sock.onerror = function(evt) {
      console.log("unhandled websocket onerror event: " + evt);
      const errorMsgElement = $("#error-msg");
      const errorModalElement = $("#error-modal");
      const escapedErrorMessage = "Websocket error. (Sorry, that's all we know, but the javascript console might contain useful debug information.) Are you connecting through a proxy?";
      errorMsgElement.html("<p>" + escapedErrorMessage + "</p>");
      errorModalElement.modal('show');
    };

    sock.onmessage = function(evt) {
      let msg;
      let jsonStr;

      // Handle binary (compressed) or text messages
      if (evt.data instanceof ArrayBuffer) {
        const data = new Uint8Array(evt.data);
        if (data.length > 0 && data[0] === WS_COMPRESS_HEADER) {
          // Compressed message - decompress it
          jsonStr = decompressMessage(data);
          if (!jsonStr) {
            console.error("Failed to decompress message");
            return;
          }
        } else {
          /* Note: Video segments are now delivered via separate video WebSocket */
          // Binary but not our compression format - unexpected
          console.log("Unexpected binary message format");
          return;
        }
      } else {
        // Text message - use as-is
        jsonStr = evt.data;
      }

      try {
        msg = JSON.parse(jsonStr);
      }
      catch (err) {
        console.log("Error: " + err.message);
        return;
      }

      if (!msg || !msg.msg) {
        console.log("unrecognised message: " + jsonStr);
        return;
      }

      // Validate message type using cached Set (avoids Object.keys allocation)
      if (validMessageTypes.has(msg.msg)) {
        try {
          messageHandlers[msg.msg](msg.p);
        } catch (e) {
          console.error("Error in message handler for", msg.msg, ":", e);
        }
      } else {
        console.log("unhandled message type: " + msg.msg);
      }
    };
  };

  /**

   * Websocket Sending Functions
   */
  my.ws.dev_select = dev_select;
  my.ws.set_netem = set_netem;
  my.ws.clear_netem = clear_netem;
  my.ws.pcap_config = pcap_config;
  my.ws.pcap_trigger = pcap_trigger;
  my.ws.send = (msg) => sock.send(msg);

  /**
   * Debug helper - decode compressed message from console
   * Usage: JT.ws.decode(arrayBuffer) or JT.ws.decode(uint8Array)
   */
  my.ws.decode = function(data) {
    let arr;
    if (data instanceof ArrayBuffer) {
      arr = new Uint8Array(data);
    } else if (data instanceof Uint8Array) {
      arr = data;
    } else {
      console.error("Expected ArrayBuffer or Uint8Array");
      return null;
    }
    if (arr[0] !== WS_COMPRESS_HEADER) {
      console.log("Not compressed (header byte:", arr[0], ")");
      return new TextDecoder().decode(arr);
    }
    const json = decompressMessage(arr);
    if (json) {
      return JSON.parse(json);
    }
    return null;
  };

  /* Expose dictionary for debugging */
  my.ws.dictionary = new TextDecoder().decode(WS_DICTIONARY);


})(JT);
/* End of jittertrap-websocket.js */
