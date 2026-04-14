#include "detection.h"
#include "globals.h"
#include "comms_ui.h"

// =============================================================================
// Detection Tuning Parameters
// =============================================================================
// REQUIRED_CONFIDENCE: How many consecutive valid signals needed for alarm.
// FALLBACK_THRESHOLD:  Used if calibration records zero noise (dead ADC).
// SAFETY_MARGIN:       Multiplier applied to peak noise → activeThreshold.
// =============================================================================
const int   REQUIRED_CONFIDENCE  = 5;
const float FALLBACK_THRESHOLD   = 50.0f;
const float SAFETY_MARGIN        = 1.5f;

// =============================================================================
// Detection Task — with 15-second Auto-Calibration Startup
// =============================================================================
// Phase 1 (CALIBRATING):
//   - Runs for exactly CALIBRATION_DURATION_MS (15 s) after boot.
//   - The FFT pipeline runs normally, but the task does NOT look for humans.
//   - Instead it tracks the highest magnitude seen in either vital band.
//   - Telemetry is still sent so the dashboard can show live data.
//   - At the end:  activeThreshold = peakNoise * SAFETY_MARGIN
//
// Phase 2 (ACTIVE):
//   - Normal human detection with the dynamically calibrated threshold.
//   - Confidence ramp-up/decay and alert logic as before.
//
// IMPORTANT: No blocking delay() is used for the 15-second window.
//   We use xTaskGetTickCount() comparisons so the task keeps consuming
//   the processedDataQueue and feeding telemetry to the dashboard.
// =============================================================================
void vDetectionTask(void *pvParameters) {
    VitalSignData vitals;
    int confidenceLevel = 0;

    // --- Calibration state ---
    TickType_t calibrationStartTick = xTaskGetTickCount();
    float peakNoiseMagnitude = 0.0f;
    bool calibrationComplete = false;

    for (;;) {
        // Wait indefinitely for the FFT task to send processed vital sign data
        if (xQueueReceive(processedDataQueue, &vitals, portMAX_DELAY) == pdPASS) {

            // =============================================================
            //  PHASE 1: CALIBRATION  (first 15 seconds after boot)
            // =============================================================
            if (!calibrationComplete) {

                // Track the highest magnitude across both bands
                if (vitals.breathingMag > peakNoiseMagnitude) {
                    peakNoiseMagnitude = vitals.breathingMag;
                }
                if (vitals.heartbeatMag > peakNoiseMagnitude) {
                    peakNoiseMagnitude = vitals.heartbeatMag;
                }

                // Check if the calibration window has elapsed
                TickType_t elapsed = xTaskGetTickCount() - calibrationStartTick;
                if (elapsed >= pdMS_TO_TICKS(CALIBRATION_DURATION_MS)) {

                    // Calculate dynamic threshold with safety margin
                    if (peakNoiseMagnitude > 0.0f) {
                        activeThreshold = peakNoiseMagnitude * SAFETY_MARGIN;
                    } else {
                        // Fallback: ADC may be disconnected or extremely quiet
                        activeThreshold = FALLBACK_THRESHOLD;
                    }

                    // Transition to ACTIVE state
                    systemState = STATE_ACTIVE;
                    calibrationComplete = true;
                }

                // Send telemetry during calibration so the dashboard shows
                // live data (the state field tells the UI we're still calibrating)
                DashboardTelemetry telem;
                telem.breathingFreq   = vitals.breathingFreq;
                telem.heartbeatFreq   = vitals.heartbeatFreq;
                telem.breathingMag    = vitals.breathingMag;
                telem.heartbeatMag    = vitals.heartbeatMag;
                telem.confidenceLevel = 0;
                telem.maxConfidence   = REQUIRED_CONFIDENCE;
                telem.alertTriggered  = false;
                telem.state           = STATE_CALIBRATING;
                xQueueSend(dashboardQueue, &telem, 0);

                // Skip all detection logic during calibration
                continue;
            }

            // =============================================================
            //  PHASE 2: ACTIVE DETECTION  (normal operation)
            // =============================================================

            // A reading is positive if EITHER vital band exceeds the
            // dynamically calibrated noise threshold
            bool breathingDetected = (vitals.breathingMag > activeThreshold);
            bool heartbeatDetected = (vitals.heartbeatMag > activeThreshold);
            bool isHumanPresent = breathingDetected || heartbeatDetected;

            if (isHumanPresent) {
                confidenceLevel++;
                if (confidenceLevel > REQUIRED_CONFIDENCE) {
                    confidenceLevel = REQUIRED_CONFIDENCE;
                }
            } else {
                // Aggressively decay confidence so noise floor spikes
                // cannot ratchet up the counter over time.
                confidenceLevel -= 2;
                if (confidenceLevel < 0) {
                    confidenceLevel = 0;
                }
            }

            // --- Build and send dashboard telemetry for EVERY FFT cycle ---
            bool alertFired = false;

            if (confidenceLevel >= REQUIRED_CONFIDENCE) {
                alertFired = true;
                xSemaphoreGive(detectionSemaphore);
                confidenceLevel = 0;
            }

            DashboardTelemetry telem;
            telem.breathingFreq   = vitals.breathingFreq;
            telem.heartbeatFreq   = vitals.heartbeatFreq;
            telem.breathingMag    = vitals.breathingMag;
            telem.heartbeatMag    = vitals.heartbeatMag;
            telem.confidenceLevel = confidenceLevel;
            telem.maxConfidence   = REQUIRED_CONFIDENCE;
            telem.alertTriggered  = alertFired;
            telem.state           = STATE_ACTIVE;

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