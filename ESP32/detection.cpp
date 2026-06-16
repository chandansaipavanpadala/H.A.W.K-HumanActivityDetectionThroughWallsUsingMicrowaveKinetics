// =============================================================================
// H.A.W.K. — Hybrid Calibration Detection Engine v2.0
// ESP32/detection.cpp
//
// Three-method fusion:
//   Method A — Frequency-domain per-bin spectral comparison  (weight: 0.40)
//   Method B — EMA adaptive noise floor                       (weight: 0.35)
//   Method C — Differential detection (current vs prev FFT)   (weight: 0.25)
//
// Phase 1 (first CALIB_CYCLES FFT windows): Baseline capture + EMA warm-up
// Phase 2 (ongoing): All three methods active, fused score drives confidence
//
// v2.0 fixes:
//   - Skips bin 0 (DC) in all scoring methods
//   - Fixed coverage denominator for small bin counts
//   - Seeds EMA floor from Phase 1 baseline to eliminate cold-start dead zone
//   - Lowered signal ratio thresholds for microwave Doppler sensitivity
//   - Added per-cycle debug telemetry to Serial for tuning
// =============================================================================

#include "detection.h"
#include "globals.h"
#include "comms_ui.h"
#include <math.h>

// ---------------------------------------------------------------------------
// Tuning constants
// ---------------------------------------------------------------------------

// EMA smoothing factor for Method B.
// 0.01 = adapts over ~100 cycles (~7 min). Good balance between stability
// and responsiveness.
static constexpr float EMA_ALPHA = 0.01f;

// EMA smoothing factor for Method C's reference window.
// Faster than B — captures "recent" baseline, not lifetime average.
static constexpr float DIFF_ALPHA = 0.08f;

// Fusion weights (must sum to 1.0)
// Method A gets less weight because it's a static baseline (can go stale).
// Method C gets more weight because it directly detects motion/change.
static constexpr float WEIGHT_A = 0.40f;
static constexpr float WEIGHT_B = 0.35f;
static constexpr float WEIGHT_C = 0.25f;

// Fused score threshold to count as a "detection hit" each FFT cycle.
// Lowered from 0.30 to 0.20 because individual method scores are small
// for microwave Doppler vital signs.
static constexpr float FUSION_THRESHOLD = 0.20f;

// Signal ratio thresholds for Methods A and B.
// For microwave Doppler through air/walls, the SNR is typically 1.1x–1.3x.
// SIGNAL_RATIO_MIN = 1.15 means 15% above floor starts scoring.
static constexpr float SIGNAL_RATIO_MIN = 1.15f;
static constexpr float SIGNAL_RATIO_MAX = 3.0f;

// Differential change ratio for Method C.
// 0.10 = 10% change from previous window starts scoring.
static constexpr float DIFF_RATIO_MIN = 0.10f;
static constexpr float DIFF_RATIO_MAX = 1.00f;

// Calibration duration — how many FFT cycles to collect for Method A baseline.
// At 4.1 s per FFT cycle, 3 cycles ≈ 12 s.
static constexpr int CALIB_CYCLES = 3;

// First bin to process (skip DC at bin 0)
static constexpr int FIRST_BIN = 1;

// Confidence thresholds
static constexpr int CONFIDENCE_MAX       = 5;
static constexpr int CONFIDENCE_DETECT    = 5;
static constexpr int CONFIDENCE_REACQUIRE = 3;

// ---------------------------------------------------------------------------
// Module-level state
// ---------------------------------------------------------------------------

// Method A: per-bin baseline accumulated during Phase 1
static float noise_floor_A[FFT_VITAL_BINS] = {0};
static float calib_accumulator[FFT_VITAL_BINS] = {0};
static int   calib_cycles_done = 0;
static bool  calibration_complete = false;

// Method B: EMA adaptive floor — updates every cycle forever
static float ema_floor_B[FFT_VITAL_BINS] = {0};
static bool  ema_seeded = false;

// Method C: previous FFT window for differential comparison
static float prev_window_C[FFT_VITAL_BINS] = {0};
static bool  diff_initialized = false;

// Confidence counter (0–5 system)
static int confidence = 0;

// Detection state
static bool detected = false;

// Debug cycle counter
static int cycle_count = 0;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Normalise a ratio to a 0–1 score with a linear ramp.
static inline float ratio_to_score(float ratio, float min_r, float max_r) {
    if (ratio <= min_r) return 0.0f;
    if (ratio >= max_r) return 1.0f;
    return (ratio - min_r) / (max_r - min_r);
}

// ---------------------------------------------------------------------------
// Phase 1: Frequency-domain baseline accumulation
// ---------------------------------------------------------------------------
static void accumulate_baseline(const float* magnitudes, int num_bins) {
    for (int i = FIRST_BIN; i < num_bins; i++) {
        calib_accumulator[i] += magnitudes[i];
    }
    calib_cycles_done++;

    if (calib_cycles_done >= CALIB_CYCLES) {
        for (int i = FIRST_BIN; i < num_bins; i++) {
            noise_floor_A[i] = calib_accumulator[i] / (float)calib_cycles_done;
            if (noise_floor_A[i] < 1.0f) noise_floor_A[i] = 1.0f;

            // BUGFIX: Seed Method B's EMA floor from the Phase 1 baseline.
            // Without this, EMA starts at the first live measurement and the
            // ratio is always ~1.0, making Method B useless for ~14 minutes.
            ema_floor_B[i] = noise_floor_A[i];

            // Also seed Method C's reference from the same baseline
            prev_window_C[i] = noise_floor_A[i];
        }
        ema_seeded = true;
        diff_initialized = true;
        calibration_complete = true;

        // Debug: print the baseline for each bin
        Serial.println("[HAWK] Phase 1 complete — baseline per bin:");
        for (int i = FIRST_BIN; i < num_bins; i++) {
            Serial.print("  bin ");
            Serial.print(i);
            Serial.print(": ");
            Serial.println((int)noise_floor_A[i]);
        }
    }
}

// ---------------------------------------------------------------------------
// Method A: per-bin spectral comparison against Phase 1 baseline.
// Skips bin 0 (DC). Uses a gentler coverage penalty.
// ---------------------------------------------------------------------------
static float score_method_A(const float* magnitudes, int num_bins) {
    if (!calibration_complete) return 0.0f;

    float total_score = 0.0f;
    int   active_bins = 0;
    int   usable_bins = num_bins - FIRST_BIN;  // bins 1..12 = 12 usable

    for (int i = FIRST_BIN; i < num_bins; i++) {
        float floor_val = noise_floor_A[i] > 1.0f ? noise_floor_A[i] : 1.0f;
        float ratio = magnitudes[i] / floor_val;
        float bin_score = ratio_to_score(ratio, SIGNAL_RATIO_MIN, SIGNAL_RATIO_MAX);
        if (bin_score > 0.0f) {
            total_score += bin_score;
            active_bins++;
        }
    }

    if (active_bins == 0) return 0.0f;

    float avg = total_score / (float)active_bins;

    // Coverage: at least 1 bin active gives coverage=1.0 for Doppler signals.
    // For microwave Doppler, it's normal for only 1-2 bins to carry energy.
    // Use a very gentle penalty: only discount if zero bins are active.
    float min_bins = fmaxf((float)usable_bins / 12.0f, 1.0f);
    float coverage = clampf((float)active_bins / min_bins, 0.0f, 1.0f);
    return avg * coverage;
}

// ---------------------------------------------------------------------------
// Method B: EMA adaptive floor.
// Skips bin 0 (DC). Seeded from Phase 1 baseline for immediate readiness.
// ---------------------------------------------------------------------------
static float score_method_B(const float* magnitudes, int num_bins) {
    float total_score = 0.0f;
    int   active_bins = 0;
    int   usable_bins = num_bins - FIRST_BIN;

    for (int i = FIRST_BIN; i < num_bins; i++) {
        if (!ema_seeded) {
            // First time ever — use the raw measurement
            ema_floor_B[i] = magnitudes[i] > 1.0f ? magnitudes[i] : 1.0f;
        } else {
            // Normal EMA update
            ema_floor_B[i] = EMA_ALPHA * magnitudes[i] + (1.0f - EMA_ALPHA) * ema_floor_B[i];
        }

        float floor_val = ema_floor_B[i] > 1.0f ? ema_floor_B[i] : 1.0f;
        float ratio = magnitudes[i] / floor_val;
        float bin_score = ratio_to_score(ratio, SIGNAL_RATIO_MIN, SIGNAL_RATIO_MAX);
        if (bin_score > 0.0f) {
            total_score += bin_score;
            active_bins++;
        }
    }

    if (!ema_seeded) ema_seeded = true;

    if (active_bins == 0) return 0.0f;
    float avg = total_score / (float)active_bins;
    float min_bins = fmaxf((float)usable_bins / 12.0f, 1.0f);
    float coverage = clampf((float)active_bins / min_bins, 0.0f, 1.0f);
    return avg * coverage;
}

// ---------------------------------------------------------------------------
// Method C: differential detection.
// Skips bin 0 (DC). Detects *changes* between FFT windows.
// ---------------------------------------------------------------------------
static float score_method_C(const float* magnitudes, int num_bins) {
    float total_score = 0.0f;
    int   active_bins = 0;
    int   usable_bins = num_bins - FIRST_BIN;

    for (int i = FIRST_BIN; i < num_bins; i++) {
        if (!diff_initialized) {
            prev_window_C[i] = magnitudes[i] > 1.0f ? magnitudes[i] : 1.0f;
        }

        float ref = prev_window_C[i] > 1.0f ? prev_window_C[i] : 1.0f;
        float delta_ratio = fabsf(magnitudes[i] - ref) / ref;
        float bin_score = ratio_to_score(delta_ratio, DIFF_RATIO_MIN, DIFF_RATIO_MAX);
        if (bin_score > 0.0f) {
            total_score += bin_score;
            active_bins++;
        }

        // Update reference with a faster EMA
        prev_window_C[i] = DIFF_ALPHA * magnitudes[i] + (1.0f - DIFF_ALPHA) * prev_window_C[i];
    }

    diff_initialized = true;

    if (active_bins == 0) return 0.0f;
    float avg = total_score / (float)active_bins;
    float min_bins = fmaxf((float)usable_bins / 12.0f, 1.0f);
    float coverage = clampf((float)active_bins / min_bins, 0.0f, 1.0f);
    return avg * coverage;
}

// ---------------------------------------------------------------------------
// Public API: called once per processed FFT cycle.
// ---------------------------------------------------------------------------
DetectionResult run_detection(const float* magnitudes, int num_bins) {
    DetectionResult result;
    result.detected       = false;
    result.confidence     = confidence;
    result.score_A        = 0.0f;
    result.score_B        = 0.0f;
    result.score_C        = 0.0f;
    result.fused_score    = 0.0f;
    result.calibrating    = false;
    result.calib_progress = 0.0f;

    cycle_count++;

    // -- Phase 1: collecting baseline --
    if (!calibration_complete) {
        accumulate_baseline(magnitudes, num_bins);
        // Warm up EMA and differential during calibration
        score_method_B(magnitudes, num_bins);
        score_method_C(magnitudes, num_bins);

        result.calibrating = true;
        result.calib_progress = (float)calib_cycles_done / (float)CALIB_CYCLES;
        return result;
    }

    // -- Phase 2: full fusion --
    float sA = score_method_A(magnitudes, num_bins);
    float sB = score_method_B(magnitudes, num_bins);
    float sC = score_method_C(magnitudes, num_bins);

    float fused = WEIGHT_A * sA + WEIGHT_B * sB + WEIGHT_C * sC;
    fused = clampf(fused, 0.0f, 1.0f);

    // -- Debug: print scores every cycle so you can see what's happening --
    Serial.print("[HAWK] #");
    Serial.print(cycle_count);
    Serial.print(" A=");
    Serial.print((int)(sA * 100));
    Serial.print(" B=");
    Serial.print((int)(sB * 100));
    Serial.print(" C=");
    Serial.print((int)(sC * 100));
    Serial.print(" F=");
    Serial.print((int)(fused * 100));
    Serial.print(" conf=");
    Serial.print(confidence);
    Serial.print("/");
    Serial.print(CONFIDENCE_MAX);

    // Print a few raw bin magnitudes for debugging
    Serial.print(" bins[");
    for (int i = FIRST_BIN; i < num_bins && i <= 5; i++) {
        Serial.print((int)magnitudes[i]);
        if (i < 5 && i < num_bins - 1) Serial.print(",");
    }
    Serial.println("]");

    // -- Update confidence --
    if (fused >= FUSION_THRESHOLD) {
        if (confidence < CONFIDENCE_MAX) confidence++;
    } else {
        if (confidence > 0) confidence--;
    }

    // -- Detection event --
    bool newly_detected = (confidence >= CONFIDENCE_DETECT) && !detected;
    if (newly_detected) {
        detected = true;
        Serial.println("[HAWK] >>> DETECTION CONFIRMED <<<");
    }

    // -- Reset after confidence drops to zero --
    if (confidence == 0 && detected) {
        detected = false;
        confidence = CONFIDENCE_REACQUIRE;
        Serial.println("[HAWK] Signal lost — re-acquisition mode.");
    }

    result.detected       = detected;
    result.confidence     = confidence;
    result.score_A        = sA;
    result.score_B        = sB;
    result.score_C        = sC;
    result.fused_score    = fused;
    result.calibrating    = false;
    result.calib_progress = 1.0f;

    return result;
}

// ---------------------------------------------------------------------------
// Hard reset
// ---------------------------------------------------------------------------
void reset_detection() {
    memset(noise_floor_A,    0, sizeof(noise_floor_A));
    memset(calib_accumulator,0, sizeof(calib_accumulator));
    memset(ema_floor_B,      0, sizeof(ema_floor_B));
    memset(prev_window_C,    0, sizeof(prev_window_C));
    calib_cycles_done    = 0;
    calibration_complete = false;
    ema_seeded           = false;
    diff_initialized     = false;
    confidence           = 0;
    detected             = false;
    cycle_count          = 0;
    Serial.println("[HAWK] Detection engine reset.");
}

// =============================================================================
// FreeRTOS Task Wrapper
// =============================================================================
void vDetectionTask(void *pvParameters) {
    VitalSignData vitals;
    bool lastAlertState = false;

    for (;;) {
        if (xQueueReceive(processedDataQueue, &vitals, portMAX_DELAY) == pdPASS) {

            // Run the hybrid detection engine
            DetectionResult det = run_detection(vitals.magnitudes, FFT_VITAL_BINS);

            // Map system state
            systemState = det.calibrating ? STATE_CALIBRATING : STATE_ACTIVE;

            // Fire alert only on the rising edge
            bool alertFired = false;
            if (det.detected && !lastAlertState) {
                alertFired = true;
                xSemaphoreGive(detectionSemaphore);
            }
            lastAlertState = det.detected;

            // Build and send dashboard telemetry
            DashboardTelemetry telem;
            telem.breathingFreq   = vitals.breathingFreq;
            telem.heartbeatFreq   = vitals.heartbeatFreq;
            telem.breathingMag    = vitals.breathingMag;
            telem.heartbeatMag    = vitals.heartbeatMag;
            telem.confidenceLevel = det.confidence;
            telem.maxConfidence   = CONFIDENCE_MAX;
            telem.alertTriggered  = alertFired;
            telem.state           = det.calibrating ? STATE_CALIBRATING : STATE_ACTIVE;
            telem.scoreA          = det.score_A;
            telem.scoreB          = det.score_B;
            telem.scoreC          = det.score_C;
            telem.fusedScore      = det.fused_score;

            xQueueSend(dashboardQueue, &telem, 0);

            // Cooldown after alert
            if (alertFired) {
                vTaskDelay(pdMS_TO_TICKS(5000));
                VitalSignData discarded;
                while (xQueueReceive(processedDataQueue, &discarded, 0) == pdPASS) {}
            }
        }
    }
}