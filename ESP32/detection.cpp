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
const float SAFETY_MARGIN          = 1.5f;
const int   CONFIDENCE_DECAY       = 1;
const int   POST_ALERT_CONFIDENCE  = 3;

// =============================================================================
// Distance Estimation Constants (Empirical Power-Law Fit)
// =============================================================================
// From magdis.png data: magnitude ≈ A / (distance ^ n)
// Fitted from empirical measurements through drywall:
//   0.5m → 0.97,  1.0m → 0.68,  1.5m → 0.33,  2.0m → 0.27,  2.5m → 0.12
// Solving: A ≈ 0.55, n ≈ 1.8
// Inverse: distance ≈ (A / magnitude) ^ (1/n)
// =============================================================================
const float DIST_COEFF_A = 0.55f;    // Amplitude coefficient
const float DIST_EXPONENT_N = 1.8f;  // Decay exponent
const float DIST_MAX_RANGE = 5.0f;   // Clamp maximum reported distance (meters)
const float DIST_MIN_RANGE = 0.2f;   // Clamp minimum reported distance (meters)

// Calibration sample buffer for percentile calculation
#define CALIB_MAX_SAMPLES 20  // ~3-4 FFT cycles in 15 seconds

// =============================================================================
// Detection Task — with 15-second Auto-Calibration Startup
// =============================================================================
// Phase 1 (CALIBRATING):
//   - Runs for exactly CALIBRATION_DURATION_MS (15 s) after boot.
//   - The FFT pipeline runs normally, but the task does NOT look for humans.
//   - Instead it collects magnitude samples into a buffer for statistical
//     analysis (75th percentile), rejecting outlier spikes.
//   - Telemetry is still sent so the dashboard can show live data.
//   - At the end:  activeThreshold = P75(noise) * SAFETY_MARGIN
//
// Phase 2 (ACTIVE):
//   - Normal human detection with the dynamically calibrated threshold.
//   - Confidence ramp-up/decay and alert logic as before.
//
// IMPORTANT: No blocking delay() is used for the 15-second window.
//   We use xTaskGetTickCount() comparisons so the task keeps consuming
//   the processedDataQueue and feeding telemetry to the dashboard.
// =============================================================================
// Helper: estimate distance from FFT magnitude using inverse power law
static float estimateDistance(float magnitude, float noiseFloor) {
    // Only estimate if magnitude is meaningfully above noise floor
    if (magnitude <= noiseFloor || magnitude <= 0.0f) {
        return -1.0f;  // Cannot estimate — signal too weak
    }
    // Normalize magnitude relative to noise floor
    float netMag = magnitude - noiseFloor;
    if (netMag <= 0.01f) return -1.0f;

    // distance = (A / netMag) ^ (1/n)
    float ratio = DIST_COEFF_A / netMag;
    if (ratio <= 0.0f) return DIST_MIN_RANGE;

    float distance = powf(ratio, 1.0f / DIST_EXPONENT_N);

    // Clamp to valid range
    if (distance < DIST_MIN_RANGE) distance = DIST_MIN_RANGE;
    if (distance > DIST_MAX_RANGE) distance = DIST_MAX_RANGE;
    return distance;
}

void vDetectionTask(void *pvParameters) {
    VitalSignData vitals;
    int confidenceLevel = 0;

    // --- Calibration state ---
    TickType_t calibrationStartTick = xTaskGetTickCount();
    float calibSamples[CALIB_MAX_SAMPLES];  // Buffer for percentile calculation
    int calibSampleCount = 0;
    bool calibrationComplete = false;

    // --- Detection duration tracking ---
    bool humanCurrentlyDetected = false;
    TickType_t detectionStartTick = 0;
    unsigned long detectionDurationMs = 0;

    for (;;) {
        // Wait indefinitely for the FFT task to send processed vital sign data
        if (xQueueReceive(processedDataQueue, &vitals, portMAX_DELAY) == pdPASS) {

            // =============================================================
            //  PHASE 1: CALIBRATION  (first 15 seconds after boot)
            // =============================================================
            if (!calibrationComplete) {

                // Record the max magnitude of both bands for this FFT cycle
                float cyclePeak = max(vitals.breathingMag, vitals.heartbeatMag);
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

                // Send telemetry during calibration so the dashboard shows
                // live data (the state field tells the UI we're still calibrating)
                DashboardTelemetry telem;
                telem.breathingFreq    = vitals.breathingFreq;
                telem.heartbeatFreq    = vitals.heartbeatFreq;
                telem.breathingMag     = vitals.breathingMag;
                telem.heartbeatMag     = vitals.heartbeatMag;
                telem.confidenceLevel  = 0;
                telem.maxConfidence    = REQUIRED_CONFIDENCE;
                telem.alertTriggered   = false;
                telem.state            = STATE_CALIBRATING;
                telem.breathingBPM     = 0.0f;
                telem.heartbeatBPM     = 0.0f;
                telem.estimatedRange   = -1.0f;
                telem.noiseFloor       = 0.0f;
                telem.detectionDuration = 0;
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
                // Track detection duration
                if (!humanCurrentlyDetected) {
                    humanCurrentlyDetected = true;
                    detectionStartTick = xTaskGetTickCount();
                }
                detectionDurationMs = (xTaskGetTickCount() - detectionStartTick) * portTICK_PERIOD_MS;
            } else {
                // Decay confidence (reduced from −2 to −1 so slow
                // breathers can still reach confirmation)
                confidenceLevel -= CONFIDENCE_DECAY;
                if (confidenceLevel < 0) {
                    confidenceLevel = 0;
                }
                // Reset detection tracking if confidence drops to zero
                if (confidenceLevel == 0) {
                    humanCurrentlyDetected = false;
                    detectionDurationMs = 0;
                }
            }

            // --- Compute BPM values ---
            float breathBPM = vitals.breathingFreq * 60.0f;  // Breaths per minute
            float heartBPM  = vitals.heartbeatFreq * 60.0f;  // Beats per minute

            // --- Estimate distance from signal strength ---
            float maxVitalMag = max(vitals.breathingMag, vitals.heartbeatMag);
            float estimatedDist = estimateDistance(maxVitalMag, activeThreshold / SAFETY_MARGIN);

            // --- Build and send dashboard telemetry for EVERY FFT cycle ---
            bool alertFired = false;

            if (confidenceLevel >= REQUIRED_CONFIDENCE) {
                alertFired = true;
                xSemaphoreGive(detectionSemaphore);
                // Reset to POST_ALERT_CONFIDENCE (3) instead of 0
                // so re-acquisition only needs 2 more positive cycles (~8s)
                confidenceLevel = POST_ALERT_CONFIDENCE;
            }

            DashboardTelemetry telem;
            telem.breathingFreq    = vitals.breathingFreq;
            telem.heartbeatFreq    = vitals.heartbeatFreq;
            telem.breathingMag     = vitals.breathingMag;
            telem.heartbeatMag     = vitals.heartbeatMag;
            telem.confidenceLevel  = confidenceLevel;
            telem.maxConfidence    = REQUIRED_CONFIDENCE;
            telem.alertTriggered   = alertFired;
            telem.state            = STATE_ACTIVE;
            telem.breathingBPM     = breathBPM;
            telem.heartbeatBPM     = heartBPM;
            telem.estimatedRange   = estimatedDist;
            telem.noiseFloor       = activeThreshold / SAFETY_MARGIN;
            telem.detectionDuration = detectionDurationMs;

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