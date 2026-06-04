#ifndef COMMS_UI_H
#define COMMS_UI_H

#include <Arduino.h>
#include <WebSocketsServer.h>
#include "globals.h"

// --- WiFi Configuration ---
// Change these to match your local network before flashing.
#define WIFI_SSID     "ChAnDaN"
#define WIFI_PASSWORD "PCSp@112"

// --- Dashboard Telemetry Structure ---
// Sent from the Detection Task to CommsUI on every FFT cycle.
// This carries the FULL system state for the dashboard to render.
struct DashboardTelemetry {
    float breathingFreq;       // Breathing peak frequency (Hz)
    float heartbeatFreq;       // Heartbeat peak frequency (Hz)
    float breathingMag;        // Breathing FFT magnitude
    float heartbeatMag;        // Heartbeat FFT magnitude
    int   confidenceLevel;     // Current detection confidence (0 to REQUIRED_CONFIDENCE)
    int   maxConfidence;       // Value of REQUIRED_CONFIDENCE (so the dashboard knows the scale)
    bool  alertTriggered;      // True if this cycle caused a confirmed detection
    SystemState state;         // Current system state (CALIBRATING or ACTIVE)
    // --- New fields (v2) ---
    float breathingBPM;        // Breathing rate in breaths per minute (freq × 60)
    float heartbeatBPM;        // Heart rate in beats per minute (freq × 60)
    float estimatedRange;      // Estimated distance to target in meters (-1 = unknown)
    float noiseFloor;          // Current noise floor magnitude (for dashboard monitoring)
    unsigned long detectionDuration;  // How long a human has been continuously detected (ms)
};

// --- FreeRTOS Queue for Dashboard Telemetry ---
// Declared here, defined in ESP32.ino alongside the other RTOS handles.
extern QueueHandle_t dashboardQueue;

// WebSocket server instance (shared between init and task)
extern WebSocketsServer webSocket;

// Call this from setup() BEFORE creating the CommsUI task.
// It connects to WiFi and starts the WebSocket server on port 81.
void initCommsWiFi();

// FreeRTOS task: handles local alarms (buzzer/LED) and
// non-blocking WebSocket broadcasts + event loop servicing.
void vCommsUITask(void *pvParameters);

#endif // COMMS_UI_H