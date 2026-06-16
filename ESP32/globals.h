#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// --- Pin Definitions ---
#define RADAR_ADC_PIN 34
#define BUZZER_PIN 25
#define LED_PIN 2

// =============================================================================
// System State Machine
// =============================================================================
// The system boots into CALIBRATING for 15 seconds to learn the noise floor,
// then transitions to ACTIVE for normal human detection.
// =============================================================================
enum SystemState {
    STATE_CALIBRATING,   // Measuring environmental noise floor — no detection
    STATE_ACTIVE         // Normal operation — detection enabled
};

// Global system state — written by Detection Task, read by CommsUI Task.
// Safe without a mutex because it is a single atomic enum write.
extern volatile SystemState systemState;

// =============================================================================
// FFT Configuration
// =============================================================================
#define FFT_SIZE 1024              // 1024-point FFT
#define FFT_VITAL_BINS 13          // Bins 0–12 cover 0–3 Hz (vital sign range)

// Duration of the startup calibration window (milliseconds)
#define CALIBRATION_DURATION_MS 15000

// --- Vital Sign Data Structure ---
// Passed from Signal Processing Task to Detection Task via processedDataQueue
struct VitalSignData {
    float breathingFreq;                  // Peak frequency in the 0.2 - 0.6 Hz band
    float heartbeatFreq;                  // Peak frequency in the 1.0 - 2.5 Hz band
    float breathingMag;                   // FFT magnitude of the breathing peak
    float heartbeatMag;                   // FFT magnitude of the heartbeat peak
    float magnitudes[FFT_VITAL_BINS];     // Full FFT magnitude array for bins 0–12
};

// --- FreeRTOS Handles ---
// Use 'extern' so they are declared here but defined in ESP32.ino
extern QueueHandle_t rawDataQueue;
extern QueueHandle_t processedDataQueue;
extern QueueHandle_t dashboardQueue;
extern SemaphoreHandle_t detectionSemaphore;

#endif // GLOBALS_H