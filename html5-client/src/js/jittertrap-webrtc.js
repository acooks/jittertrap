/* jittertrap-webrtc.js */

/* global JT:true */

/**
 * WebRTC video playback module for JitterTrap
 * Uses WebRTC to play video streams directly in the browser with sub-second latency.
 *
 * Flow: Browser sends SDP offer -> Server creates PeerConnection -> Server sends answer
 *       Server forwards RTP packets via WebRTC -> Browser plays video in <video> element
 */
((my) => {
  'use strict';

  my.webrtc = my.webrtc || {};

  /* ========== STATE ========== */

  let peerConnection = null;
  let viewerId = null;
  let currentFkey = null;
  let currentSsrc = 0;
  let isActive = false;
  let pendingIceCandidates = [];  /* Queue candidates until we have viewer_id */

  /* ========== CONSTANTS ========== */

  /* WebRTC codec enum values (must match libdatachannel rtcCodec enum) */
  const RTC_CODEC = {
    H264: 0,  /* RTC_CODEC_H264 */
    VP8: 1,   /* RTC_CODEC_VP8 */
    VP9: 2,   /* RTC_CODEC_VP9 */
    H265: 3,  /* RTC_CODEC_H265 */
    AV1: 4    /* RTC_CODEC_AV1 */
  };

  /* Map video_codec from toptalk to RTC codec */
  const codecMap = {
    1: RTC_CODEC.H264,   /* VP_CODEC_H264 */
    2: RTC_CODEC.H265,   /* VP_CODEC_H265 */
    3: RTC_CODEC.VP8,    /* VP_CODEC_VP8 */
    4: RTC_CODEC.VP9,    /* VP_CODEC_VP9 */
    5: RTC_CODEC.AV1     /* VP_CODEC_AV1 */
  };

  /* ========== UI ========== */

  const createOverlay = () => {
    $('#webrtc-overlay').remove();

    const overlay = $(`
      <div id="webrtc-overlay" class="video-overlay">
        <div class="video-container">
          <div class="video-header">
            <span class="video-title">WebRTC Video Stream</span>
            <button type="button" class="close video-close" aria-label="Close">
              <span aria-hidden="true">&times;</span>
            </button>
          </div>
          <video id="webrtc-player" autoplay playsinline muted></video>
          <div class="video-controls">
            <span id="webrtc-status" class="video-status">Connecting...</span>
            <span id="webrtc-resolution" class="video-resolution"></span>
            <button id="webrtc-fullscreen-btn" class="btn btn-sm btn-secondary" title="Fullscreen (F)">&#x26F6;</button>
          </div>
        </div>
      </div>
    `);

    $('body').append(overlay);

    $('.video-close').on('click', () => {
      my.webrtc.stop();
    });

    $('#webrtc-fullscreen-btn').on('click', toggleFullscreen);

    $(document).on('keydown.webrtc', (e) => {
      if (e.key === 'Escape') {
        /* Don't stop if exiting fullscreen - let browser handle it */
        if (document.fullscreenElement) {
          return; /* Let browser exit fullscreen */
        }
        e.preventDefault();
        e.stopPropagation();
        if (isActive) {
          my.webrtc.stop();
        }
      } else if (e.key === 'f' || e.key === 'F') {
        e.preventDefault();
        toggleFullscreen();
      }
    });

    return overlay;
  };

  const toggleFullscreen = () => {
    const container = document.querySelector('#webrtc-overlay .video-container');
    if (!container) return;

    if (document.fullscreenElement) {
      document.exitFullscreen();
    } else {
      container.requestFullscreen().catch(() => {
        /* Fullscreen not supported or denied */
      });
    }
  };

  let lastFrameCount = 0;
  let lastFrameTime = 0;
  let currentFps = 0;
  let statsInterval = null;

  const updateResolution = (width, height) => {
    if (width && height && width > 0 && height > 0) {
      const fpsText = currentFps > 0 ? ` @ ${currentFps} fps` : '';
      $('#webrtc-resolution').text(`${width}x${height}${fpsText}`);
    }
  };


  const startStatsPolling = () => {
    if (statsInterval) return;

    statsInterval = setInterval(async () => {
      if (!peerConnection) return;

      try {
        const stats = await peerConnection.getStats();
        stats.forEach((report) => {
          if (report.type === 'inbound-rtp' && report.kind === 'video') {
            const now = performance.now();
            const frameCount = report.framesDecoded || 0;

            if (lastFrameTime > 0 && frameCount > lastFrameCount) {
              const elapsed = (now - lastFrameTime) / 1000;
              if (elapsed > 0) {
                currentFps = Math.round((frameCount - lastFrameCount) / elapsed);
                /* Update resolution display with new fps */
                const video = document.getElementById('webrtc-player');
                if (video && video.videoWidth > 0) {
                  updateResolution(video.videoWidth, video.videoHeight);
                }
              }
            }

            lastFrameCount = frameCount;
            lastFrameTime = now;
          }
        });
      } catch (e) {
        /* Stats not available */
      }
    }, 1000);
  };

  const stopStatsPolling = () => {
    if (statsInterval) {
      clearInterval(statsInterval);
      statsInterval = null;
    }
    lastFrameCount = 0;
    lastFrameTime = 0;
    currentFps = 0;
  };

  const updateStatus = (text) => {
    $('#webrtc-status').text(text);
  };

  /* ========== WEBRTC ========== */

  const createPeerConnection = () => {
    /* No ICE servers needed for LAN */
    const config = {
      iceServers: []
    };

    const pc = new RTCPeerConnection(config);

    pc.ontrack = (event) => {
      const video = document.getElementById('webrtc-player');
      if (video && event.streams[0]) {
        video.srcObject = event.streams[0];
        updateStatus('Playing');

        /* Update resolution when metadata is available */
        video.onloadedmetadata = () => {
          updateResolution(video.videoWidth, video.videoHeight);
        };
        video.onerror = () => {
          updateStatus('Playback error');
        };
        video.onstalled = () => {
          updateStatus('Buffering...');
        };
        video.onplaying = () => {
          updateStatus('Playing');
          startStatsPolling();
        };
      }
    };

    pc.onicecandidate = (event) => {
      if (event.candidate && event.candidate.candidate) {
        /* Filter out empty candidates (end-of-candidates signal) */
        if (event.candidate.candidate === '') {
          return;
        }

        const candidateInfo = {
          candidate: event.candidate.candidate,
          mid: event.candidate.sdpMid || '0'
        };

        if (viewerId !== null) {
          /* Have viewer_id, send immediately */
          const msg = {
            msg: 'webrtc_ice',
            p: {
              viewer_id: viewerId,
              candidate: candidateInfo.candidate,
              mid: candidateInfo.mid
            }
          };
          if (my.ws && my.ws.send) {
            my.ws.send(JSON.stringify(msg));
          }
        } else {
          /* Queue until we have viewer_id */
          pendingIceCandidates.push(candidateInfo);
        }
      }
    };

    pc.oniceconnectionstatechange = () => {
      switch (pc.iceConnectionState) {
        case 'connected':
        case 'completed':
          updateStatus('Connected');
          break;
        case 'disconnected':
          updateStatus('Disconnected');
          break;
        case 'failed':
          updateStatus('Connection failed');
          break;
        case 'closed':
          updateStatus('Closed');
          break;
      }
    };

    pc.onconnectionstatechange = () => {
      if (pc.connectionState === 'failed') {
        updateStatus('Connection failed');
        my.webrtc.stop();
      }
    };

    return pc;
  };

  /* ========== MESSAGE HANDLERS ========== */

  /**
   * Handle webrtc_answer message from server
   */
  my.webrtc.handleAnswer = async (params) => {
    if (!peerConnection) {
      return;
    }

    viewerId = params.viewer_id;

    try {
      await peerConnection.setRemoteDescription({
        type: 'answer',
        sdp: params.sdp
      });
      updateStatus('Connected');

      /* Flush any queued ICE candidates now that we have viewer_id */
      if (pendingIceCandidates.length > 0) {
        for (const candidateInfo of pendingIceCandidates) {
          const msg = {
            msg: 'webrtc_ice',
            p: {
              viewer_id: viewerId,
              candidate: candidateInfo.candidate,
              mid: candidateInfo.mid
            }
          };
          if (my.ws && my.ws.send) {
            my.ws.send(JSON.stringify(msg));
          }
        }
        pendingIceCandidates = [];
      }
    } catch (e) {
      updateStatus('Error: ' + e.message);
    }
  };

  /**
   * Handle webrtc_ice message from server (ICE candidate)
   */
  my.webrtc.handleIce = async (params) => {
    if (!peerConnection) {
      return;
    }

    try {
      await peerConnection.addIceCandidate({
        candidate: params.candidate,
        sdpMid: params.mid
      });
    } catch (e) {
      /* ICE candidate failures are usually non-fatal */
    }
  };

  /**
   * Handle webrtc_status message from server
   */
  my.webrtc.handleStatus = (params) => {
    if (params.viewer_id !== viewerId) return;

    if (params.waiting_for_keyframe) {
      updateStatus('Waiting for keyframe...');
    } else if (params.active) {
      updateStatus('Playing');
    }
  };

  /**
   * Handle video_error message (shared with MSE)
   */
  my.webrtc.handleError = (params) => {
    updateStatus('Error: ' + params.message);

    /* Show alert for errors that require user action */
    switch (params.code) {
      case 'not_available':
        alert('WebRTC playback is not available. The server may not have been compiled with ENABLE_WEBRTC_PLAYBACK=1.');
        break;
      case 'no_slots':
        alert('Maximum concurrent viewers reached.\n\nPlease close another video stream and try again.');
        break;
      case 'codec_unsupported':
        alert('H.265/HEVC WebRTC Playback Not Supported\n\n' +
              'Your browser does not support H.265 (HEVC) video in WebRTC.\n\n' +
              'Options:\n' +
              '- Use Chrome 136+ (has native H.265 WebRTC support)\n' +
              '- Use Safari (supports H.265 WebRTC)\n' +
              '- View an H.264 stream instead');
        break;
      case 'webrtc_failed':
      case 'not_initialized':
      case 'bad_params':
        alert('WebRTC error: ' + params.message);
        break;
    }

    /* Auto-close the overlay on error */
    my.webrtc.stop();
  };

  /* ========== PUBLIC API ========== */

  /**
   * Start WebRTC video playback for a flow
   * @param {string} fkey - Flow key
   * @param {object} flowData - Flow data with video info
   */
  my.webrtc.startPlayback = async (fkey, flowData) => {
    if (isActive) {
      my.webrtc.stop();
    }

    currentFkey = fkey;
    currentSsrc = flowData.video_ssrc || 0;
    isActive = true;

    createOverlay();
    updateStatus('Creating offer...');

    /* Create peer connection */
    peerConnection = createPeerConnection();

    /* Add transceiver to receive video (recvonly) */
    peerConnection.addTransceiver('video', { direction: 'recvonly' });

    try {
      /* Create offer */
      const offer = await peerConnection.createOffer();
      await peerConnection.setLocalDescription(offer);

      updateStatus('Sending offer...');

      /* Map codec to RTC enum */
      const rtcCodec = codecMap[flowData.video_codec] || RTC_CODEC.H264;

      /* Send offer to server via main WebSocket */
      const msg = {
        msg: 'webrtc_offer',
        p: {
          fkey: fkey,
          ssrc: currentSsrc,
          codec: rtcCodec,
          sdp: offer.sdp
        }
      };

      if (my.ws && my.ws.send) {
        my.ws.send(JSON.stringify(msg));
        updateStatus('Waiting for answer...');
      } else {
        updateStatus('Error: WebSocket not connected');
        my.webrtc.stop();
      }
    } catch (e) {
      updateStatus('Error: ' + e.message);
      my.webrtc.stop();
    }
  };

  /**
   * Stop WebRTC playback
   */
  my.webrtc.stop = () => {
    /* Unbind keyboard handler first to prevent re-entry */
    $(document).off('keydown.webrtc');

    /* Save state we need before resetting */
    const savedViewerId = viewerId;
    const savedPeerConnection = peerConnection;

    /* Reset all state immediately */
    isActive = false;
    viewerId = null;
    currentFkey = null;
    currentSsrc = 0;
    pendingIceCandidates = [];
    peerConnection = null;

    /* Stop stats polling */
    stopStatsPolling();

    /* Remove overlay */
    $('#webrtc-overlay').remove();

    /* Send stop message to server if we had a viewer */
    if (savedViewerId !== null && my.ws && my.ws.send) {
      const msg = {
        msg: 'webrtc_stop',
        p: { viewer_id: savedViewerId }
      };
      try {
        my.ws.send(JSON.stringify(msg));
      } catch (e) {
        /* Ignore send errors */
      }
    }

    /* Close peer connection after state is reset */
    if (savedPeerConnection) {
      try {
        savedPeerConnection.close();
      } catch (e) {
        /* Ignore close errors */
      }
    }
  };

  /**
   * Check if WebRTC is supported
   * @returns {boolean}
   */
  my.webrtc.isSupported = () => {
    return 'RTCPeerConnection' in window;
  };

  /**
   * Check if playback is active
   * @returns {boolean}
   */
  my.webrtc.isPlaying = () => isActive;

})(JT);
/* End of jittertrap-webrtc.js */
