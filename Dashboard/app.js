/* ====================================================================
   H.A.W.K. TACTICAL DASHBOARD — APPLICATION LOGIC
   Project H.A.W.K. — Through-Wall Life Detection Radar
   ==================================================================== */

// ─────────────────────────────────────────────────
//  CONFIGURATION
// ─────────────────────────────────────────────────
let WEBSOCKET_URL = '';
const MAX_CHART_POINTS = 30;       // Number of data points visible on each chart
const RECONNECT_DELAY_MS = 3000;   // Time before auto-reconnect attempt
const ALERT_DISPLAY_MS = 5000;     // How long RED ALERT stays on screen

// ─────────────────────────────────────────────────
//  DOM REFERENCES
// ─────────────────────────────────────────────────
const dom = {
    // Loading overlay
    overlay: document.getElementById('loading-overlay'),
    loadingStatus: document.getElementById('loading-status'),

    // Calibration overlay
    calibrationOverlay: document.getElementById('calibration-overlay'),
    calibrationProgressBar: document.getElementById('calibration-progress-bar'),
    calibrationTimer: document.getElementById('calibration-timer'),

    // Dashboard
    dashboard: document.getElementById('dashboard'),

    // Header
    connectionDot: document.getElementById('connection-dot'),
    connectionLabel: document.getElementById('connection-label'),
    uptime: document.getElementById('header-uptime'),

    // Status display
    statusDisplay: document.getElementById('status-display'),
    statusIcon: document.getElementById('status-icon'),
    statusLabel: document.getElementById('status-label'),
    statusSublabel: document.getElementById('status-sublabel'),

    // Confidence gauge
    confidenceBars: document.getElementById('confidence-bars'),
    confidenceValue: document.getElementById('confidence-value'),
    confidenceMax: document.getElementById('confidence-max'),
    confidenceLabel: document.getElementById('confidence-label'),

    // Stats
    statAlerts: document.getElementById('stat-alerts'),
    statBreathing: document.getElementById('stat-breathing'),
    statHeartbeat: document.getElementById('stat-heartbeat'),
    statLastAlert: document.getElementById('stat-last-alert'),

    // New v2 stats
    statBreathBPM: document.getElementById('stat-breath-bpm'),
    statHeartBPM: document.getElementById('stat-heart-bpm'),
    statRange: document.getElementById('stat-range'),
    statNoiseFloor: document.getElementById('stat-noise-floor'),
    statDetectDuration: document.getElementById('stat-detect-duration'),
    statSignalStrength: document.getElementById('stat-signal-strength'),

    // Vignette
    vignette: document.getElementById('vignette-overlay'),

    // Log
    logContainer: document.getElementById('log-container'),
    logEmpty: document.getElementById('log-empty'),
};

// ─────────────────────────────────────────────────
//  STATE
// ─────────────────────────────────────────────────
let ws = null;
let alertCount = 0;
let logIndex = 0;
let alertTimeout = null;
let uptimeStart = null;
let uptimeInterval = null;
let reconnectTimer = null;
let isConnected = false;

// Calibration state tracking
let calibrationActive = false;       // True while ESP32 is in CALIBRATING state
let calibrationStartTime = null;     // Client-side timestamp when calibration overlay appeared
let calibrationInterval = null;      // Interval ID for the countdown timer
const CALIBRATION_DURATION_S = 15;   // Must match CALIBRATION_DURATION_MS / 1000 on ESP32

// Client-side detection duration tracking (moved from ESP32)
let detectionStartTime = null;       // When detection started (null = not detecting)

// Distance estimation constants (moved from ESP32 — SNR-based)
const DIST_SCALE = 140.0;
const DIST_EXPONENT = 0.6;
const DIST_MAX_CM = 500;
const DIST_MIN_CM = 20;

// ─────────────────────────────────────────────────
//  WEB AUDIO — Tactical Alert Tone
// ─────────────────────────────────────────────────
let audioCtx = null;

function playAlertTone() {
    try {
        if (!audioCtx) audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        const now = audioCtx.currentTime;

        for (let i = 0; i < 3; i++) {
            const osc = audioCtx.createOscillator();
            const gain = audioCtx.createGain();
            osc.type = 'square';
            osc.frequency.setValueAtTime(800, now + i * 0.35);
            osc.frequency.setValueAtTime(600, now + i * 0.35 + 0.17);
            gain.gain.setValueAtTime(0.06, now + i * 0.35);
            gain.gain.exponentialRampToValueAtTime(0.001, now + i * 0.35 + 0.33);
            osc.connect(gain).connect(audioCtx.destination);
            osc.start(now + i * 0.35);
            osc.stop(now + i * 0.35 + 0.35);
        }
    } catch (e) { /* silent fail — audio is a nice-to-have */ }
}

// ─────────────────────────────────────────────────
//  CHART.JS SETUP
// ─────────────────────────────────────────────────
const chartDefaults = {
    responsive: true,
    maintainAspectRatio: false,
    animation: { duration: 300 },
    interaction: { mode: 'nearest', intersect: false },
    plugins: {
        legend: { display: false },
        tooltip: {
            backgroundColor: 'rgba(10, 14, 23, 0.92)',
            titleFont: { family: "'JetBrains Mono', monospace", size: 11 },
            bodyFont: { family: "'JetBrains Mono', monospace", size: 11 },
            borderColor: 'rgba(34, 211, 238, 0.2)',
            borderWidth: 1,
            padding: 10,
            cornerRadius: 6,
        },
    },
    scales: {
        x: {
            display: true,
            grid: {
                color: 'rgba(34, 211, 238, 0.05)',
                drawTicks: false,
            },
            ticks: {
                font: { family: "'JetBrains Mono', monospace", size: 9 },
                color: '#334155',
                maxRotation: 0,
                maxTicksLimit: 6,
            },
            border: { color: 'rgba(34, 211, 238, 0.1)' },
        },
        y: {
            display: true,
            grid: {
                color: 'rgba(34, 211, 238, 0.05)',
                drawTicks: false,
            },
            ticks: {
                font: { family: "'JetBrains Mono', monospace", size: 10 },
                color: '#475569',
                padding: 8,
            },
            border: { color: 'rgba(34, 211, 238, 0.1)' },
        },
    },
};

// Breathing Chart
const breathingCtx = document.getElementById('breathing-chart').getContext('2d');
const breathingChart = new Chart(breathingCtx, {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: 'Breathing (Hz)',
            data: [],
            borderColor: '#22d3ee',
            backgroundColor: 'rgba(34, 211, 238, 0.08)',
            borderWidth: 2,
            pointRadius: 3,
            pointBackgroundColor: '#22d3ee',
            pointBorderColor: '#22d3ee',
            pointHoverRadius: 5,
            tension: 0.35,
            fill: true,
        }],
    },
    options: {
        ...chartDefaults,
        scales: {
            ...chartDefaults.scales,
            y: {
                ...chartDefaults.scales.y,
                min: 0,
                max: 0.8,
                ticks: {
                    ...chartDefaults.scales.y.ticks,
                    stepSize: 0.1,
                    callback: v => v.toFixed(1),
                },
            },
        },
        plugins: {
            ...chartDefaults.plugins,
            annotation: undefined,
        },
    },
});

// Heartbeat Chart
const heartbeatCtx = document.getElementById('heartbeat-chart').getContext('2d');
const heartbeatChart = new Chart(heartbeatCtx, {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: 'Heartbeat (Hz)',
            data: [],
            borderColor: '#e879f9',
            backgroundColor: 'rgba(232, 121, 249, 0.08)',
            borderWidth: 2,
            pointRadius: 3,
            pointBackgroundColor: '#e879f9',
            pointBorderColor: '#e879f9',
            pointHoverRadius: 5,
            tension: 0.35,
            fill: true,
        }],
    },
    options: {
        ...chartDefaults,
        scales: {
            ...chartDefaults.scales,
            y: {
                ...chartDefaults.scales.y,
                min: 0.5,
                max: 3.0,
                ticks: {
                    ...chartDefaults.scales.y.ticks,
                    stepSize: 0.5,
                    callback: v => v.toFixed(1),
                },
            },
        },
    },
});

// ─────────────────────────────────────────────────
//  CHART DATA PUSH
// ─────────────────────────────────────────────────
function pushChartData(chart, label, value) {
    chart.data.labels.push(label);
    chart.data.datasets[0].data.push(value);

    // Scrolling window: remove oldest point
    if (chart.data.labels.length > MAX_CHART_POINTS) {
        chart.data.labels.shift();
        chart.data.datasets[0].data.shift();
    }

    chart.update('none'); // 'none' mode = no animation for smooth scrolling
}

// ─────────────────────────────────────────────────
//  WEBSOCKET CONNECTION
// ─────────────────────────────────────────────────
function connect() {
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
        return; // Already connecting or connected
    }

    dom.loadingStatus.textContent = `Establishing WebSocket uplink to ${WEBSOCKET_URL.replace('ws://', '')}`;
    showOverlay();

    try {
        ws = new WebSocket(WEBSOCKET_URL);
    } catch (e) {
        dom.loadingStatus.textContent = 'ERROR: Failed to create WebSocket object';
        scheduleReconnect();
        return;
    }

    ws.onopen = () => {
        isConnected = true;
        clearTimeout(reconnectTimer);
        hideOverlay();
        setConnectionStatus(true);

        // Start uptime
        uptimeStart = Date.now();
        uptimeInterval = setInterval(updateUptime, 1000);

        addLogEntry('system', 'WebSocket uplink established — awaiting system state');
    };

    ws.onmessage = (event) => {
        parseHAWKMessage(event.data);
    };

    ws.onerror = () => {
        // onerror is always followed by onclose, so we only update the
        // overlay message here.  The actual reconnect logic lives in onclose.
        dom.loadingStatus.textContent = 'CONNECTION ERROR — STANDBY...';
    };

    ws.onclose = () => {
        if (isConnected) {
            addLogEntry('system', '⚠ WebSocket link severed — attempting recovery');
        }
        isConnected = false;
        setConnectionStatus(false);
        clearInterval(uptimeInterval);

        // Freeze the charts so stale telemetry isn't misread
        breathingChart.options.animation = false;
        heartbeatChart.options.animation = false;

        // Show the full-screen overlay with a clear "CONNECTION LOST" state
        showOverlay();
        dom.loadingStatus.textContent = 'CONNECTION LOST / RECONNECTING...';

        scheduleReconnect();
    };
}

function scheduleReconnect() {
    clearTimeout(reconnectTimer);

    // Show a live countdown on the overlay so the operator knows
    // the system is not frozen
    let countdown = Math.ceil(RECONNECT_DELAY_MS / 1000);
    dom.loadingStatus.textContent = `CONNECTION LOST — RECONNECTING IN ${countdown}s...`;

    const countdownInterval = setInterval(() => {
        countdown--;
        if (countdown <= 0) {
            clearInterval(countdownInterval);
            dom.loadingStatus.textContent = 'ESTABLISHING UPLINK...';
        } else {
            dom.loadingStatus.textContent = `CONNECTION LOST — RECONNECTING IN ${countdown}s...`;
        }
    }, 1000);

    reconnectTimer = setTimeout(() => {
        clearInterval(countdownInterval);
        connect();
    }, RECONNECT_DELAY_MS);
}

// ─────────────────────────────────────────────────
//  OVERLAY CONTROL
// ─────────────────────────────────────────────────
function showOverlay() {
    dom.overlay.classList.remove('hidden');
    dom.dashboard.classList.remove('visible');
}

function hideOverlay() {
    dom.overlay.classList.add('hidden');
    // Don't auto-reveal the dashboard here.
    // The first $HAWK,DATA message will determine whether to show
    // the calibration overlay or the dashboard based on system_state.
    // If we're reconnecting (calibration already done), the first
    // ACTIVE telemetry will reveal the dashboard.
}

// ─────────────────────────────────────────────────
//  $HAWK PROTOCOL PARSER
// ─────────────────────────────────────────────────
function parseHAWKMessage(raw) {
    if (!raw.startsWith('$HAWK')) return;

    const parts = raw.split(',');
    if (parts.length < 3) return;

    const msgType = parts[1]; // DATA, ALERT, or STATUS

    // ── Handshake ──
    if (msgType === 'STATUS' && parts[2] === 'CONNECTED') {
        addLogEntry('system', 'ESP32 handshake confirmed — $HAWK protocol active');
        return;
    }

    // ── Live Telemetry ──
    // Format: $HAWK,DATA,breathFreq,heartFreq,breathMag,heartMag,confidence,maxConf,timestamp,state,scoreA,scoreB,scoreC,fusedScore
    if (msgType === 'DATA' && parts.length >= 10) {
        const breathFreq = parseFloat(parts[2]);
        const heartFreq = parseFloat(parts[3]);
        const breathMag = parseFloat(parts[4]);
        const heartMag = parseFloat(parts[5]);
        const confidence = parseInt(parts[6], 10);
        const maxConf = parseInt(parts[7], 10);
        const espTime = parts[8];
        const systemState = parts[9];  // "CALIBRATING" or "ACTIVE"

        // Parse hybrid detection scores (with fallback for older firmware)
        const scoreA = parts.length > 10 ? parseFloat(parts[10]) : 0;
        const scoreB = parts.length > 11 ? parseFloat(parts[11]) : 0;
        const scoreC = parts.length > 12 ? parseFloat(parts[12]) : 0;
        const fusedScore = parts.length > 13 ? parseFloat(parts[13]) : 0;

        // ── Client-side computed metrics ──
        const breathBPM = breathFreq * 60;
        const heartBPM = heartFreq * 60;

        // Distance estimation (based on fused score as proxy for signal strength)
        let estRange = -1;
        if (fusedScore > 0.1) {
            // Higher fused score = closer target
            estRange = DIST_SCALE / Math.pow(fusedScore * 3.0, DIST_EXPONENT);
            if (estRange < DIST_MIN_CM) estRange = DIST_MIN_CM;
            if (estRange > DIST_MAX_CM) estRange = DIST_MAX_CM;
        }

        // Detection duration tracking (client-side)
        let detectDurMs = 0;
        if (confidence > 0) {
            if (!detectionStartTime) detectionStartTime = Date.now();
            detectDurMs = Date.now() - detectionStartTime;
        } else {
            detectionStartTime = null;
        }

        // ── Handle calibration state ──
        if (systemState === 'CALIBRATING') {
            if (!calibrationActive) {
                showCalibrationOverlay();
            }
            addLogEntry('system',
                `CAL | Breath Mag: ${breathMag.toFixed(1)} ` +
                `| Heart Mag: ${heartMag.toFixed(1)} ` +
                `| Measuring noise floor...`
            );
            return;
        }

        // ── Transition: CALIBRATING → ACTIVE ──
        if (calibrationActive && systemState === 'ACTIVE') {
            hideCalibrationOverlay();
            addLogEntry('system', '✓ Calibration complete — dynamic threshold locked — detection ACTIVE');
        }

        // Ensure the dashboard is visible when receiving ACTIVE data.
        if (!dom.dashboard.classList.contains('visible')) {
            dom.dashboard.classList.add('visible');
        }

        // ── Normal ACTIVE telemetry processing ──
        const now = new Date();
        const timeLabel = now.toLocaleTimeString('en-US', {
            hour12: false,
            hour: '2-digit',
            minute: '2-digit',
            second: '2-digit',
        });

        // Update charts
        pushChartData(breathingChart, timeLabel, breathFreq);
        pushChartData(heartbeatChart, timeLabel, heartFreq);

        // Update original stat cards
        dom.statBreathing.textContent = breathFreq.toFixed(3);
        dom.statHeartbeat.textContent = heartFreq.toFixed(3);

        // Update computed stat cards
        if (dom.statBreathBPM) {
            dom.statBreathBPM.innerHTML = `${breathBPM.toFixed(1)} <span class="stat-card__unit">BPM</span>`;
        }
        if (dom.statHeartBPM) {
            dom.statHeartBPM.innerHTML = `${heartBPM.toFixed(1)} <span class="stat-card__unit">BPM</span>`;
        }
        if (dom.statRange) {
            if (estRange > 0) {
                dom.statRange.innerHTML = `${estRange.toFixed(0)} <span class="stat-card__unit">cm</span>`;
            } else {
                dom.statRange.innerHTML = `N/A <span class="stat-card__unit">cm</span>`;
            }
        }
        if (dom.statNoiseFloor) {
            // Show the fused score (0–1) as the primary detection metric
            dom.statNoiseFloor.textContent = fusedScore.toFixed(3);
        }
        if (dom.statDetectDuration) {
            if (detectDurMs > 0) {
                const durSec = Math.floor(detectDurMs / 1000);
                const mm = String(Math.floor(durSec / 60)).padStart(2, '0');
                const ss = String(durSec % 60).padStart(2, '0');
                dom.statDetectDuration.textContent = `${mm}:${ss}`;
            } else {
                dom.statDetectDuration.textContent = '—';
            }
        }
        if (dom.statSignalStrength) {
            const maxMag = Math.max(breathMag, heartMag);
            dom.statSignalStrength.textContent = maxMag.toFixed(1);
        }

        // Update confidence gauge
        updateConfidence(confidence, maxConf);

        // Log telemetry
        const rangeStr = estRange > 0 ? `${estRange.toFixed(0)}cm` : 'N/A';
        addLogEntry('data',
            `TELEM | Breath: ${breathFreq.toFixed(3)} Hz (${breathBPM.toFixed(0)} BPM) ` +
            `| Heart: ${heartFreq.toFixed(3)} Hz (${heartBPM.toFixed(0)} BPM) ` +
            `| Fused: ${fusedScore.toFixed(2)} [A:${scoreA.toFixed(2)} B:${scoreB.toFixed(2)} C:${scoreC.toFixed(2)}] ` +
            `| Conf: ${confidence}/${maxConf}`
        );
        return;
    }

    // ── Alert ──
    // Format: $HAWK,ALERT,HUMAN_DETECTED,TIME:<timestamp>
    if (msgType === 'ALERT' && parts[2] === 'HUMAN_DETECTED') {
        let espTimestamp = '—';
        if (parts[3] && parts[3].startsWith('TIME:')) {
            espTimestamp = parts[3].substring(5);
        }
        triggerRedAlert(espTimestamp);
        return;
    }
}

// ─────────────────────────────────────────────────
//  CALIBRATION OVERLAY CONTROL
// ─────────────────────────────────────────────────
function showCalibrationOverlay() {
    calibrationActive = true;
    calibrationStartTime = Date.now();

    // Show the calibration overlay (keep dashboard hidden behind it)
    dom.calibrationOverlay.classList.remove('hidden');
    dom.dashboard.classList.remove('visible');

    // Reset progress bar
    dom.calibrationProgressBar.style.width = '0%';

    addLogEntry('system', '⚠ CALIBRATION STARTED — Analyzing environmental noise floor for 15 seconds');

    // Live countdown timer + progress bar
    calibrationInterval = setInterval(() => {
        const elapsedMs = Date.now() - calibrationStartTime;
        const elapsedS = Math.floor(elapsedMs / 1000);
        const remainingS = Math.max(0, CALIBRATION_DURATION_S - elapsedS);
        const progressPct = Math.min(100, (elapsedMs / (CALIBRATION_DURATION_S * 1000)) * 100);

        dom.calibrationProgressBar.style.width = `${progressPct}%`;
        dom.calibrationTimer.textContent = `ANALYZING NOISE FLOOR — ${remainingS}s REMAINING`;

        if (remainingS <= 0) {
            clearInterval(calibrationInterval);
            calibrationInterval = null;
            dom.calibrationTimer.textContent = 'FINALIZING THRESHOLD...';
            dom.calibrationProgressBar.style.width = '100%';
        }
    }, 500);
}

function hideCalibrationOverlay() {
    calibrationActive = false;
    calibrationStartTime = null;

    if (calibrationInterval) {
        clearInterval(calibrationInterval);
        calibrationInterval = null;
    }

    // Fade out the calibration overlay
    dom.calibrationOverlay.classList.add('hidden');

    // Reveal the dashboard with a slight delay for smooth transition
    setTimeout(() => {
        dom.dashboard.classList.add('visible');
    }, 400);
}

// ─────────────────────────────────────────────────
//  RED ALERT TRIGGER
// ─────────────────────────────────────────────────
function triggerRedAlert(espTimestamp) {
    alertCount++;
    dom.statAlerts.textContent = alertCount;

    const now = new Date();
    const timeStr = now.toLocaleTimeString('en-US', { hour12: false });
    dom.statLastAlert.textContent = timeStr;

    addLogEntry('alert', `⚠ HUMAN DETECTED — ESP: ${espTimestamp} ms — Local: ${timeStr}`);

    // Audio
    playAlertTone();

    // Visual: RED ALERT
    dom.statusDisplay.className = 'status-display red-alert';
    dom.statusIcon.textContent = '⚠';
    dom.statusLabel.textContent = 'RED ALERT';
    dom.statusSublabel.textContent = 'HUMAN PRESENCE CONFIRMED';

    // Vignette
    dom.vignette.classList.add('active');

    // Auto-reset
    if (alertTimeout) clearTimeout(alertTimeout);
    alertTimeout = setTimeout(() => {
        dom.statusDisplay.className = 'status-display standby';
        dom.statusIcon.textContent = '';
        dom.statusLabel.textContent = 'MONITORING';
        dom.statusSublabel.textContent = 'System standing by';
        dom.vignette.classList.remove('active');
    }, ALERT_DISPLAY_MS);
}

// ─────────────────────────────────────────────────
//  CONFIDENCE GAUGE
// ─────────────────────────────────────────────────
function updateConfidence(level, max) {
    // Update text
    dom.confidenceValue.textContent = level;
    dom.confidenceMax.textContent = max;

    // Color the value text based on level
    if (level >= max) {
        dom.confidenceValue.style.color = '#ef4444';
    } else if (level >= max - 1) {
        dom.confidenceValue.style.color = '#f97316';
    } else if (level >= Math.ceil(max / 2)) {
        dom.confidenceValue.style.color = '#f59e0b';
    } else {
        dom.confidenceValue.style.color = '#22d3ee';
    }

    // Update label
    const labels = ['NO SIGNAL', 'WEAK', 'TRACKING', 'ACQUIRING', 'LOCKED', 'CONFIRMED'];
    dom.confidenceLabel.textContent = labels[Math.min(level, labels.length - 1)] || 'UNKNOWN';

    // Update bars
    const bars = dom.confidenceBars.children;
    for (let i = 0; i < bars.length; i++) {
        // Remove all filled-* classes
        bars[i].className = 'confidence-bar';
        bars[i].setAttribute('data-index', i);

        if (i < level) {
            bars[i].classList.add(`filled-${level}`);
        }
    }
}

// ─────────────────────────────────────────────────
//  CONNECTION STATUS
// ─────────────────────────────────────────────────
function setConnectionStatus(online) {
    if (online) {
        dom.connectionDot.classList.add('online');
        dom.connectionLabel.textContent = 'ONLINE';
        dom.connectionLabel.style.color = '#34d399';
    } else {
        dom.connectionDot.classList.remove('online');
        dom.connectionLabel.textContent = 'OFFLINE';
        dom.connectionLabel.style.color = '#475569';
    }
}

// ─────────────────────────────────────────────────
//  EVENT LOG
// ─────────────────────────────────────────────────
function addLogEntry(type, message) {
    if (dom.logEmpty) dom.logEmpty.style.display = 'none';

    logIndex++;
    const entry = document.createElement('div');
    entry.className = `log-entry log-entry--${type}`;

    const now = new Date();
    const ts = now.toLocaleTimeString('en-US', {
        hour12: false,
        fractionalSecondDigits: 3,
    });

    entry.innerHTML = `
        <span class="log-entry__index">${String(logIndex).padStart(3, '0')}</span>
        <span class="log-entry__time">${ts}</span>
        <span class="log-entry__msg">${escapeHtml(message)}</span>
    `;

    dom.logContainer.appendChild(entry);

    // Keep max 150 entries
    while (dom.logContainer.children.length > 151) {
        dom.logContainer.removeChild(dom.logContainer.children[1]);
    }

    dom.logContainer.scrollTop = dom.logContainer.scrollHeight;
}

function clearLog() {
    // Keep the empty placeholder, remove everything else
    while (dom.logContainer.children.length > 1) {
        dom.logContainer.removeChild(dom.logContainer.lastChild);
    }
    logIndex = 0;
    if (dom.logEmpty) dom.logEmpty.style.display = 'block';
}

function escapeHtml(str) {
    const div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
}

// ─────────────────────────────────────────────────
//  UPTIME COUNTER
// ─────────────────────────────────────────────────
function updateUptime() {
    if (!uptimeStart) return;
    const elapsed = Math.floor((Date.now() - uptimeStart) / 1000);
    const h = String(Math.floor(elapsed / 3600)).padStart(2, '0');
    const m = String(Math.floor((elapsed % 3600) / 60)).padStart(2, '0');
    const s = String(elapsed % 60).padStart(2, '0');
    dom.uptime.textContent = `${h}:${m}:${s}`;
}

// ─────────────────────────────────────────────────
//  INITIALIZATION
// ─────────────────────────────────────────────────
document.getElementById('connect-btn').addEventListener('click', () => {
    const inputVal = document.getElementById('ip-input').value.trim();
    const userIP = inputVal ? inputVal : "192.168.137.207";
    WEBSOCKET_URL = `ws://${userIP}:81`;
    
    document.getElementById('setup-overlay').classList.add('hidden');
    addLogEntry('system', `Dashboard initialized — establishing uplink to ${userIP}`);
    connect();
});

document.getElementById('ip-input').addEventListener('keypress', (e) => {
    if (e.key === 'Enter') {
        document.getElementById('connect-btn').click();
    }
});
