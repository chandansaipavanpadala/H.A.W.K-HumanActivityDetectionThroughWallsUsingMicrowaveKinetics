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
// Includes raw vital data + hybrid detection engine scores.
struct DashboardTelemetry {
    float breathingFreq;       // Breathing peak frequency (Hz)
    float heartbeatFreq;       // Heartbeat peak frequency (Hz)
    float breathingMag;        // Breathing FFT magnitude
    float heartbeatMag;        // Heartbeat FFT magnitude
    int   confidenceLevel;     // Current detection confidence (0 to maxConfidence)
    int   maxConfidence;       // Maximum confidence scale
    bool  alertTriggered;      // True if this cycle caused a confirmed detection
    SystemState state;         // Current system state (CALIBRATING or ACTIVE)
    float scoreA;              // Method A: spectral comparison score (0–1)
    float scoreB;              // Method B: EMA adaptive floor score (0–1)
    float scoreC;              // Method C: differential score (0–1)
    float fusedScore;          // Weighted fusion of A+B+C (0–1)
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