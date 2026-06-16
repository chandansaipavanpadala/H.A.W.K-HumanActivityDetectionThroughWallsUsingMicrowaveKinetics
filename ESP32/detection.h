// detection.h — H.A.W.K. Hybrid Calibration Detection Engine
// FreeRTOS task wrapper + core detection API

#ifndef DETECTION_H
#define DETECTION_H

#include <Arduino.h>
#include <stdint.h>

// Result returned by run_detection() each FFT cycle
struct DetectionResult {
    bool  detected;        // true when confidence == CONFIDENCE_MAX
    int   confidence;      // 0–5
    float score_A;         // spectral comparison score (0–1)
    float score_B;         // EMA adaptive floor score  (0–1)
    float score_C;         // differential score        (0–1)
    float fused_score;     // weighted fusion           (0–1)
    bool  calibrating;     // true during Phase 1
    float calib_progress;  // 0.0–1.0 during Phase 1
};

// Call once per processed FFT cycle from vDetectionTask.
// magnitudes: FFT_VITAL_BINS element array of bin magnitudes.
DetectionResult run_detection(const float* magnitudes, int num_bins);

// Full state reset — use on reboot or to force re-calibration.
void reset_detection();

// FreeRTOS task: receives VitalSignData from signal processing,
// runs the hybrid detection engine, and sends telemetry to the dashboard.
void vDetectionTask(void *pvParameters);

#endif // DETECTION_H