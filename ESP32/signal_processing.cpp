#include "signal_processing.h"
#include "globals.h"
#include "arduinoFFT.h"
#include <string.h>

// =============================================================================
// FFT Configuration for 1024-Point Transform
// =============================================================================
// 1024 samples at 250 Hz = 4.096 seconds per FFT window
// Frequency resolution: 250 / 1024 = 0.2441 Hz per bin
// Nyquist limit: 125 Hz (well above our 3 Hz target)
//
// IMPORTANT: Using float (not double) because the ESP32's FPU is
// single-precision only. Double-precision is software-emulated and ~10x slower.
// For a 1024-point FFT the speedup is critical (~30ms vs ~300ms compute time).
// =============================================================================
#define SAMPLES 1024
#define SAMPLING_FREQUENCY 250
#define FREQ_RESOLUTION ((float)SAMPLING_FREQUENCY / SAMPLES)  // 0.2441 Hz

// --- Sub-Band Bin Ranges ---
// Breathing band: 0.2 - 0.6 Hz
//   Bin 1 = 0.244 Hz, Bin 2 = 0.488 Hz (covers the core breathing range)
#define BREATHING_BIN_START 1
#define BREATHING_BIN_END   2

// Heartbeat band: 1.0 - 2.5 Hz
//   Bin 4 = 0.977 Hz, Bin 10 = 2.441 Hz
#define HEARTBEAT_BIN_START 4
#define HEARTBEAT_BIN_END   10

// =============================================================================
// findSubBandPeak - Locate the dominant frequency within a specific bin range
// =============================================================================
// Uses parabolic interpolation on the 3 bins surrounding the peak for sub-bin
// frequency accuracy (same technique as arduinoFFT::majorPeak internally).
//
// Parameters:
//   magnitudes  - FFT magnitude array (post complexToMagnitude)
//   startBin    - First bin index to search (inclusive)
//   endBin      - Last bin index to search (inclusive)
//   totalBins   - Total number of bins (SAMPLES/2) for boundary checking
//   outMagnitude- Output: magnitude of the detected peak
//   freqRes     - Frequency resolution (Hz per bin)
//
// Returns: Interpolated frequency in Hz of the strongest peak in the range
// =============================================================================
float findSubBandPeak(float* magnitudes, int startBin, int endBin,
                      int totalBins, float* outMagnitude, float freqRes) {
    // 1. Find the bin with maximum magnitude in the target range
    //    CRITICAL: Always start from at least bin 1 to skip the DC component
    //    (bin 0).  DC leakage from the ADC offset can dominate bin 0 and
    //    corrupt the parabolic interpolation, producing negative frequencies.
    int safeStart = (startBin < 1) ? 1 : startBin;

    float maxMag = 0.0f;
    int peakBin = safeStart;

    for (int i = safeStart; i <= endBin; i++) {
        if (magnitudes[i] > maxMag) {
            maxMag = magnitudes[i];
            peakBin = i;
        }
    }
    *outMagnitude = maxMag;

    // 2. Parabolic interpolation for sub-bin frequency accuracy
    //    Uses bins on either side of the peak.
    //    Guard: peakBin must be > 0 AND < totalBins-1 to have valid neighbors.
    //    Since we already guarantee peakBin >= 1, the left neighbor is always valid.
    float interpolatedBin = (float)peakBin;
    if (peakBin > 0 && peakBin < totalBins - 1) {
        float alpha = magnitudes[peakBin - 1];
        float beta  = magnitudes[peakBin];
        float gamma = magnitudes[peakBin + 1];
        float denom = alpha - 2.0f * beta + gamma;
        if (denom != 0.0f) {
            interpolatedBin = peakBin + 0.5f * (alpha - gamma) / denom;
        }
    }

    // 3. CLAMP: Ensure the interpolated result never goes below the safe start
    //    bin or above the end bin.  This is the final safety net against
    //    negative frequencies caused by extreme spectral leakage.
    if (interpolatedBin < (float)safeStart) {
        interpolatedBin = (float)safeStart;
    }
    if (interpolatedBin > (float)endBin) {
        interpolatedBin = (float)endBin;
    }

    return interpolatedBin * freqRes;
}

// =============================================================================
// Signal Processing Task (Priority 3)
// =============================================================================
// Accumulates 1024 raw ADC samples (~4.1 seconds), then runs:
//   DC Removal → Hamming Window → FFT → Magnitude → Sub-Band Peak Detection
// Outputs a VitalSignData struct with breathing and heartbeat frequencies.
// =============================================================================
void vSignalProcessingTask(void *pvParameters) {
    // --- All state is local to this task (prevents shared-state bugs) ---
    // 'static' puts these in BSS (not on the task stack), avoiding stack overflow.
    // Memory: 2 * 1024 * 4 bytes = 8,192 bytes in BSS.
    static float vReal[SAMPLES];
    static float vImag[SAMPLES];
    int sampleCounter = 0;
    uint16_t rawSample;

    ArduinoFFT<float> FFT = ArduinoFFT<float>();

    // Serial output removed — all telemetry goes over WebSocket only

    for (;;) {
        // 1. Collect samples one-by-one from the Radar Task via the Queue
        if (xQueueReceive(rawDataQueue, &rawSample, portMAX_DELAY) == pdPASS) {

            vReal[sampleCounter] = (float)rawSample;
            vImag[sampleCounter] = 0.0f;
            sampleCounter++;

            // 2. Once we have 1024 samples, run the full FFT pipeline
            if (sampleCounter == SAMPLES) {

                // --- FFT Pipeline ---
                FFT.dcRemoval(vReal, SAMPLES);
                FFT.windowing(vReal, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
                FFT.compute(vReal, vImag, SAMPLES, FFT_FORWARD);
                FFT.complexToMagnitude(vReal, vImag, SAMPLES);

                // Kill residual DC leakage in bin 0.
                // Even after dcRemoval + Hamming, spectral leakage leaves a
                // large value in bin 0 that would poison the detection engine.
                vReal[0] = 0.0f;

                // 3. Extract vital signs from specific frequency sub-bands
                VitalSignData vitals;
                memset(&vitals, 0, sizeof(vitals));  // Zero all fields including magnitudes[]
                int halfBins = SAMPLES / 2;  // Only first half of FFT is meaningful

                vitals.breathingFreq = findSubBandPeak(
                    vReal, BREATHING_BIN_START, BREATHING_BIN_END,
                    halfBins, &vitals.breathingMag, FREQ_RESOLUTION
                );

                vitals.heartbeatFreq = findSubBandPeak(
                    vReal, HEARTBEAT_BIN_START, HEARTBEAT_BIN_END,
                    halfBins, &vitals.heartbeatMag, FREQ_RESOLUTION
                );

                // 3b. Copy the vital-sign FFT bins (0–12) for the hybrid
                //     detection engine's per-bin spectral analysis
                for (int b = 0; b < FFT_VITAL_BINS && b < halfBins; b++) {
                    vitals.magnitudes[b] = vReal[b];
                }

                // 4. Send the dual-band result to the Detection Task
                xQueueSend(processedDataQueue, &vitals, 0);

                // Reset counter for next window
                sampleCounter = 0;
            }
        }
    }
}