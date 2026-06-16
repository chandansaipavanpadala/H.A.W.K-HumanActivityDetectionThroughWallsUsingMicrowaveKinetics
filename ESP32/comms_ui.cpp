#include "comms_ui.h"
#include "globals.h"
#include <WiFi.h>
#include <WebSocketsServer.h>

// ---------------------------------------------------------------------
// WebSocket Server on port 81
// ---------------------------------------------------------------------
WebSocketsServer webSocket = WebSocketsServer(81);

// Track connected clients (for debug logging only)
static uint8_t connectedClients = 0;

// ---------------------------------------------------------------------
// WebSocket Event Handler
// Called by the library whenever a client connects, disconnects,
// or sends data.  Runs in the context of webSocket.loop().
// ---------------------------------------------------------------------
static void webSocketEvent(uint8_t clientNum, WStype_t type,
                           uint8_t *payload, size_t length) {
    switch (type) {

        case WStype_DISCONNECTED:
            if (connectedClients > 0) connectedClients--;
            // Serial output removed — reduces UART blocking latency
            break;

        case WStype_CONNECTED: {
            connectedClients++;
            IPAddress ip = webSocket.remoteIP(clientNum);
            // Serial output removed — reduces UART blocking latency

            // Send a welcome/handshake string so the dashboard knows
            // it is talking to the correct device
            webSocket.sendTXT(clientNum, "$HAWK,STATUS,CONNECTED");
            break;
        }

        case WStype_TEXT:
            // We don't expect incoming commands yet, but log them
            // Serial output removed — reduces UART blocking latency
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------
// initCommsWiFi()  —  call once from setup()
// Connects to the local WiFi network and starts the WebSocket server.
// This MUST finish before the FreeRTOS scheduler is active, so it is
// safe to use a blocking while-loop here.
// ---------------------------------------------------------------------
void initCommsWiFi() {
    // Serial output removed — WiFi status is shown on dashboard connection
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    // Block until connected (runs before scheduler, so this is fine)
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);   // plain delay — scheduler not started yet
    }

    // Serial output removed — IP address visible in router DHCP table

    // Start the WebSocket server and register the event callback
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    // Serial output removed
}

// ---------------------------------------------------------------------
// FreeRTOS Task 4  —  Communications & UI
// Priority 1 (lowest).  Stack recommendation: 8192 bytes.
//
// Design:
//   - Polls the dashboardQueue (non-blocking) for telemetry updates
//     from the Detection Task. Each struct arrives every FFT cycle
//     (~4.1 seconds) and carries live frequencies + confidence.
//   - When alertTriggered == true, also fires local buzzer/LED.
//   - webSocket.loop() runs every iteration at ~20 Hz to keep
//     the TCP stack alive and handle ping/pong + new connections.
//
// Protocol sent over WebSocket:
//   Telemetry:  $HAWK,DATA,<breathFreq>,<heartFreq>,<breathMag>,<heartMag>,<confidence>,<maxConf>,<timestamp>,<state>,<noiseFloor>
//   Alert:      $HAWK,ALERT,HUMAN_DETECTED,TIME:<timestamp>
//   <state> is either "CALIBRATING" or "ACTIVE"
//   BPM, distance, and detection duration are computed client-side.
// ---------------------------------------------------------------------
void vCommsUITask(void *pvParameters) {
    // 1. Initialize local hardware
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);

    // Alarm state tracking
    bool alarmActive = false;
    TickType_t alarmStartTick = 0;
    const TickType_t alarmDuration = pdMS_TO_TICKS(1500);

    DashboardTelemetry telemetry;

    for (;;) {
        // ---- A.  Service the WebSocket event loop (non-blocking) ----
        webSocket.loop();

        // ---- B.  Check for new telemetry from the Detection Task ----
        if (xQueueReceive(dashboardQueue, &telemetry, pdMS_TO_TICKS(50)) == pdTRUE) {

            // Build the $HAWK,DATA telemetry string (includes hybrid scores)
            // Format: $HAWK,DATA,breathFreq,heartFreq,breathMag,heartMag,confidence,maxConf,timestamp,state,scoreA,scoreB,scoreC,fusedScore
            const char* stateStr = (telemetry.state == STATE_CALIBRATING) ? "CALIBRATING" : "ACTIVE";
            char dataPacket[256];
            snprintf(dataPacket, sizeof(dataPacket),
                     "$HAWK,DATA,%.3f,%.3f,%.1f,%.1f,%d,%d,%lu,%s,%.3f,%.3f,%.3f,%.3f",
                     telemetry.breathingFreq,
                     telemetry.heartbeatFreq,
                     telemetry.breathingMag,
                     telemetry.heartbeatMag,
                     telemetry.confidenceLevel,
                     telemetry.maxConfidence,
                     millis(),
                     stateStr,
                     telemetry.scoreA,
                     telemetry.scoreB,
                     telemetry.scoreC,
                     telemetry.fusedScore);

            // Serial output removed — data goes over WebSocket only

            // Broadcast to all connected WebSocket clients (non-blocking)
            webSocket.broadcastTXT(dataPacket);

            // ---- C.  Handle alert events ----
            if (telemetry.alertTriggered) {
                // Turn on local alarms
                digitalWrite(LED_PIN, HIGH);
                digitalWrite(BUZZER_PIN, HIGH);
                alarmActive    = true;
                alarmStartTick = xTaskGetTickCount();

                // Build and send the alert string
                char alertPacket[80];
                snprintf(alertPacket, sizeof(alertPacket),
                         "$HAWK,ALERT,HUMAN_DETECTED,TIME:%lu", millis());

                // Serial output removed — alert goes over WebSocket only
                webSocket.broadcastTXT(alertPacket);
            }
        }

        // ---- D.  Auto-reset alarms after 1.5 s ---------------------
        if (alarmActive &&
            (xTaskGetTickCount() - alarmStartTick) >= alarmDuration) {
            digitalWrite(LED_PIN, LOW);
            digitalWrite(BUZZER_PIN, LOW);
            alarmActive = false;
        }
    }
}