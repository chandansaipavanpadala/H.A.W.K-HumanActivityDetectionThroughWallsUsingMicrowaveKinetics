#include "detection.h"
#include "globals.h"
#include "comms_ui.h"
#include <algorithm>  // for std::sort (percentile calculation)

// =============================================================================
// Detection Tuning Parameters
// =============================================================================
// REQUIRED_CONFIDENCE: How many consecutive valid signals needed for alarm.
// FALLBACK_THRESHOLD:  Used if calibration records zero noise (dead ADC).
// SAFETY_MARGIN:       Multiplier applied to 75th-percentile noise → activeThreshold.
// CONFIDENCE_DECAY:    How much confidence drops per negative reading (was 2, now 1).
// POST_ALERT_CONFIDENCE: Confidence level after an alert fires (was 0, now 3).
// =============================================================================
const int   REQUIRED_CONFIDENCE    = 5;
const float FALLBACK_THRESHOLD     = 50.0f;
const float SAFETY_MARGIN          = 1.3f;   // Tuned for LM358 noise (was 1.5)
const int   CONFIDENCE_DECAY       = 1;
const int   POST_ALERT_CONFIDENCE  = 3;

// Calibration sample buffer for percentile calculation
#define CALIB_MAX_SAMPLES 20  // ~3-4 FFT cycles in 15 seconds

// =============================================================================
// Detection Task — with 15-second Auto-Calibration Startup
// =============================================================================
// Phase 1 (CALIBRATING):
//   Runs for CALIBRATION_DURATION_MS (15 s). Collects noise samples,
//   computes 75th percentile → activeThreshold = P75 * SAFETY_MARGIN.
//
// Phase 2 (ACTIVE):
//   Detection with dynamically calibrated threshold + confidence logic.
//
// NOTE: BPM, distance estimation, and detection duration are computed
// on the Dashboard (client-side) to reduce ESP32 workload.
// =============================================================================

void vDetectionTask(void *pvParameters) {
    VitalSignData vitals;
    int confidenceLevel = 0;

    // --- Calibration state ---
    TickType_t calibrationStartTick = xTaskGetTickCount();
    float calibSamples[CALIB_MAX_SAMPLES];
    int calibSampleCount = 0;
    bool calibrationComplete = false;

    // --- EMA (Exponential Moving Average) smoothing ---
    // Smooths FFT magnitudes across cycles to reduce noise variance.
    // Alpha = 0.3 means each new reading contributes 30%, history 70%.
    // This gives ~√3 effective noise reduction while tracking real signals.
    const float EMA_ALPHA = 0.3f;
    float smoothBreathMag = 0.0f;
    float smoothHeartMag = 0.0f;
    bool emaInitialized = false;

    for (;;) {
        // Wait indefinitely for the FFT task to send processed vital sign data
        if (xQueueReceive(processedDataQueue, &vitals, portMAX_DELAY) == pdPASS) {

            // Apply EMA smoothing to magnitudes
            if (!emaInitialized) {
                smoothBreathMag = vitals.breathingMag;
                smoothHeartMag  = vitals.heartbeatMag;
                emaInitialized = true;
            } else {
                smoothBreathMag = EMA_ALPHA * vitals.breathingMag + (1.0f - EMA_ALPHA) * smoothBreathMag;
                smoothHeartMag  = EMA_ALPHA * vitals.heartbeatMag + (1.0f - EMA_ALPHA) * smoothHeartMag;
            }

            // =============================================================
            //  PHASE 1: CALIBRATION  (first 15 seconds after boot)
            // =============================================================
            if (!calibrationComplete) {

                // Record the max magnitude of both bands for this FFT cycle
                float cyclePeak = max(smoothBreathMag, smoothHeartMag);
                if (calibSampleCount < CALIB_MAX_SAMPLES) {
                    calibSamples[calibSampleCount++] = cyclePeak;
                }

                // Check if the calibration window has elapsed
                TickType_t elapsed = xTaskGetTickCount() - calibrationStartTick;
                if (elapsed >= pdMS_TO_TICKS(CALIBRATION_DURATION_MS)) {

                    // Calculate 75th percentile of calibration samples
                    // (more resilient than peak — outlier spikes from a nearby
                    //  person during boot won't poison the threshold)
                    if (calibSampleCount > 0) {
                        std::sort(calibSamples, calibSamples + calibSampleCount);
                        int p75Index = (int)(calibSampleCount * 0.75f);
                        if (p75Index >= calibSampleCount) p75Index = calibSampleCount - 1;
                        float percentile75 = calibSamples[p75Index];

                        if (percentile75 > 0.0f) {
                            activeThreshold = percentile75 * SAFETY_MARGIN;
                        } else {
                            activeThreshold = FALLBACK_THRESHOLD;
                        }
                    } else {
                        // No samples collected — fallback
                        activeThreshold = FALLBACK_THRESHOLD;
                    }

                    // Transition to ACTIVE state
                    systemState = STATE_ACTIVE;
                    calibrationComplete = true;
                }

                // Send telemetry during calibration (use smoothed values)
                DashboardTelemetry telem;
                telem.breathingFreq   = vitals.breathingFreq;
                telem.heartbeatFreq   = vitals.heartbeatFreq;
                telem.breathingMag    = smoothBreathMag;
                telem.heartbeatMag    = smoothHeartMag;
                telem.confidenceLevel = 0;
                telem.maxConfidence   = REQUIRED_CONFIDENCE;
                telem.alertTriggered  = false;
                telem.state           = STATE_CALIBRATING;
                telem.noiseFloor      = 0.0f;
                xQueueSend(dashboardQueue, &telem, 0);

                // Skip all detection logic during calibration
                continue;
            }

            // =============================================================
            //  PHASE 2: ACTIVE DETECTION  (normal operation)
            // =============================================================

            // Use SMOOTHED magnitudes for detection (less susceptible to
            // single-cycle noise spikes that cause false positives)
            bool breathingDetected = (smoothBreathMag > activeThreshold);
            bool heartbeatDetected = (smoothHeartMag > activeThreshold);
            bool isHumanPresent = breathingDetected || heartbeatDetected;

            if (isHumanPresent) {
                confidenceLevel++;
                if (confidenceLevel > REQUIRED_CONFIDENCE) {
                    confidenceLevel = REQUIRED_CONFIDENCE;
                }
            } else {
                confidenceLevel -= CONFIDENCE_DECAY;
                if (confidenceLevel < 0) confidenceLevel = 0;
            }

            // --- Build and send telemetry (raw data only) ---
            bool alertFired = false;
            if (confidenceLevel >= REQUIRED_CONFIDENCE) {
                alertFired = true;
                xSemaphoreGive(detectionSemaphore);
                confidenceLevel = POST_ALERT_CONFIDENCE;
            }

            DashboardTelemetry telem;
            telem.breathingFreq   = vitals.breathingFreq;
            telem.heartbeatFreq   = vitals.heartbeatFreq;
            telem.breathingMag    = smoothBreathMag;
            telem.heartbeatMag    = smoothHeartMag;
            telem.confidenceLevel = confidenceLevel;
            telem.maxConfidence   = REQUIRED_CONFIDENCE;
            telem.alertTriggered  = alertFired;
            telem.state           = STATE_ACTIVE;
            telem.noiseFloor      = activeThreshold / SAFETY_MARGIN;

            xQueueSend(dashboardQueue, &telem, 0);

            // If an alert fired, enter cooldown
            if (alertFired) {
                vTaskDelay(pdMS_TO_TICKS(5000));

                // Drain stale FFT results that accumulated during cooldown
                VitalSignData discarded;
                while (xQueueReceive(processedDataQueue, &discarded, 0) == pdPASS) {
                    // discard stale readings
                }
            }
        }
    }
}