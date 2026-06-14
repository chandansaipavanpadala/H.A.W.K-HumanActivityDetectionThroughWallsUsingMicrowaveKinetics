#include <Arduino.h>
#include <arduinoFFT.h>

// =============================================================================
// H.A.W.K. DIAGNOSTIC TOOL v1.1 (LTspice Custom Circuit Edition)
// =============================================================================
// This sketch runs 4 sequential diagnostic tests to isolate issues on the
// custom LTspice 2-stage active bandpass filter circuit.
// Flash this INSTEAD of the main ESP32.ino firmware.
//
// Open Serial Monitor at 115200 baud and watch the output.
// Each test runs for ~10 seconds and prints a report.
// =============================================================================

#define RADAR_ADC_PIN 34
#define SAMPLE_RATE 250
#define SAMPLE_PERIOD_MS (1000 / SAMPLE_RATE)
#define FFT_SAMPLES 1024

// FFT buffers
static float vReal[FFT_SAMPLES];
static float vImag[FFT_SAMPLES];

ArduinoFFT<float> FFT = ArduinoFFT<float>();

// =============================================================================
// TEST 1: Raw ADC Statistics (10 seconds)
// =============================================================================
// Checks: DC bias level, signal swing, clipping, and ADC noise floor
// Expected: Mean ~2048 (1.65V bias), swing > 20 counts, no clipping
// =============================================================================
void testRawADC() {
    Serial.println("\n╔══════════════════════════════════════════════════════╗");
    Serial.println("║  TEST 1: RAW ADC SIGNAL ANALYSIS (10 seconds)       ║");
    Serial.println("║  Stand AWAY from sensor — measuring noise floor     ║");
    Serial.println("╚══════════════════════════════════════════════════════╝\n");
    delay(2000);  // Give user time to step away

    uint32_t sumADC = 0;
    uint64_t sumSqDev = 0;
    uint16_t minADC = 4095, maxADC = 0;
    uint16_t clipLow = 0, clipHigh = 0;
    const int totalSamples = SAMPLE_RATE * 10;  // 2500 samples over 10s
    uint16_t samples[totalSamples];

    // Collect samples
    for (int i = 0; i < totalSamples; i++) {
        uint16_t val = analogRead(RADAR_ADC_PIN);
        samples[i] = val;
        sumADC += val;
        if (val < minADC) minADC = val;
        if (val > maxADC) maxADC = val;
        if (val <= 5) clipLow++;
        if (val >= 4090) clipHigh++;
        delay(SAMPLE_PERIOD_MS);
    }

    float meanADC = (float)sumADC / totalSamples;
    float meanVoltage = (meanADC / 4095.0) * 3.3;

    // Calculate standard deviation
    for (int i = 0; i < totalSamples; i++) {
        float dev = (float)samples[i] - meanADC;
        sumSqDev += (uint64_t)(dev * dev);
    }
    float stdDev = sqrt((float)sumSqDev / totalSamples);

    float minV = (minADC / 4095.0) * 3.3;
    float maxV = (maxADC / 4095.0) * 3.3;
    float swingV = maxV - minV;
    uint16_t swingADC = maxADC - minADC;

    Serial.println("─── RESULTS ───────────────────────────────────────────");
    Serial.printf("  DC Bias (Mean):    %d counts = %.3f V\n", (int)meanADC, meanVoltage);
    Serial.printf("  Min Value:         %d counts = %.3f V\n", minADC, minV);
    Serial.printf("  Max Value:         %d counts = %.3f V\n", maxADC, maxV);
    Serial.printf("  Peak-to-Peak:      %d counts = %.3f V\n", swingADC, swingV);
    Serial.printf("  Std Deviation:     %.1f counts\n", stdDev);
    Serial.printf("  Clipping (low):    %d samples (%.1f%%)\n", clipLow, (clipLow * 100.0) / totalSamples);
    Serial.printf("  Clipping (high):   %d samples (%.1f%%)\n", clipHigh, (clipHigh * 100.0) / totalSamples);

    Serial.println("\n─── DIAGNOSIS ─────────────────────────────────────────");

    // Check DC bias (Expected ~1.65V from 10k/10k divider from 3.3V)
    if (meanVoltage < 0.5) {
        Serial.println("  ❌ DC BIAS WAY TOO LOW (< 0.5V)");
        Serial.println("     → Op-amp output may be railed to ground.");
        Serial.println("     → Check: Is the 1.65V bias divider (R1/R2 = 10kΩ/10kΩ) connected to V3 (3.3V) and GND?");
        Serial.println("     → Check: Are R1/R2 connected to the non-inverting inputs of U1 and U2?");
        Serial.println("     → Check: Are op-amps U1 & U2 receiving VCC (5V or 3.3V) and GND?");
    } else if (meanVoltage < 1.4) {
        Serial.println("  ⚠  DC BIAS LOW (< 1.4V)");
        Serial.println("     → Expected ~1.65V from R1/R2 divider.");
        Serial.println("     → Check: Verify resistor values of R1 and R2 are identical (10 kΩ).");
        Serial.println("     → Check: Input offset voltage or leakage might be pulling down the bias.");
    } else if (meanVoltage > 1.9) {
        Serial.println("  ⚠  DC BIAS HIGH (> 1.9V)");
        Serial.println("     → Expected ~1.65V from R1/R2 divider.");
        Serial.println("     → Check: Verify resistor values of R1 and R2 are identical (10 kΩ).");
        Serial.println("     → Check: Are coupling capacitors C1 (150 µF) and C3 (150 µF) leaking DC voltage?");
        Serial.println("     → Check: Are feedback resistors R4 (1 MΩ) and R6 (1 MΩ) disconnected? (causes open-loop rail)");
    } else {
        Serial.println("  ✅ DC BIAS LOOKS GOOD (in the 1.4V – 1.9V range)");
    }

    // Check signal swing (Expected gain ~78 dB, i.e., ~8000x)
    if (swingADC < 5) {
        Serial.println("  ❌ NO AC SIGNAL DETECTED (swing < 5 counts)");
        Serial.println("     → The custom bandpass filter is producing no signal output.");
        Serial.println("     → Check: HB100 IF pin connected to Stage 1 input (via C1 + R3)?");
        Serial.println("     → Check: Does the HB100 radar module have 5V power?");
        Serial.println("     → Check: Are feedback resistors R4 (1 MΩ) and R6 (1 MΩ) connected?");
    } else if (swingADC < 30) {
        Serial.println("  ⚠  VERY WEAK SIGNAL (swing < 30 counts)");
        Serial.println("     → Amplification may be insufficient or component values are off.");
        Serial.println("     → Check: Are the input resistors R3 and R5 exactly 10 kΩ?");
        Serial.println("     → Check: Are feedback resistors R4 and R6 exactly 1 MΩ?");
        Serial.println("     → Consider: Increasing Stage 2 feedback resistor R6 (e.g., to 2.2 MΩ).");
    } else if (swingADC > 3800) {
        Serial.println("  ❌ SIGNAL SATURATING (swing > 3800 counts)");
        Serial.println("     → Gain is too high — signal is clipping at the ADC limits.");
        Serial.println("     → Reduce feedback resistors (try 470 kΩ for R4 and/or R6).");
    } else {
        Serial.printf("  ✅ Signal swing looks reasonable (%d counts).\n", swingADC);
    }

    // Check clipping
    if (clipLow > 0 || clipHigh > 0) {
        Serial.println("  ❌ SIGNAL CLIPPING DETECTED!");
        Serial.println("     → The waveform is hitting ADC rails (clipping).");
        Serial.println("     → This causes false harmonics in the FFT.");
        Serial.println("     → Fix: Reduce R4 or R6 to lower the gain, or adjust DC bias.");
    }

    // Check noise floor (StdDev)
    if (stdDev > 50) {
        Serial.println("  ⚠  HIGH NOISE (StdDev > 50 counts)");
        Serial.println("     → Check: Are feedback capacitors C2 (0.047 µF) and C4 (0.047 µF) connected?");
        Serial.println("       If omitted, high-frequency noise is not filtered and bypasses LPF attenuation.");
        Serial.println("     → Check: Are you using the default noisy LM358 op-amp? If so, consider");
        Serial.println("       switching to a low-noise, rail-to-rail op-amp (e.g., MCP6002 or OPA2340).");
        Serial.println("     → Try: Adding a 10µF bypass capacitor across R2 to stabilize the 1.65V bias divider.");
        Serial.println("     → Try: Shielded or shorter wires between HB100, custom PCB, and ESP32.");
    }

    Serial.println("───────────────────────────────────────────────────────\n");
}

// =============================================================================
// TEST 2: FFT Noise Floor Analysis (NO human present)
// =============================================================================
// Runs a full 1024-pt FFT on ambient noise to see what the FFT bins look like
// without any human target. This shows the actual noise floor magnitude.
// =============================================================================
void testFFTNoiseFloor() {
    Serial.println("\n╔══════════════════════════════════════════════════════╗");
    Serial.println("║  TEST 2: FFT NOISE FLOOR (1024-pt, NO human)        ║");
    Serial.println("║  Make sure NO human is within 3m of the sensor!      ║");
    Serial.println("╚══════════════════════════════════════════════════════╝\n");
    delay(3000);

    // Collect 1024 samples
    TickType_t xLastWake = xTaskGetTickCount();
    for (int i = 0; i < FFT_SAMPLES; i++) {
        vReal[i] = (float)analogRead(RADAR_ADC_PIN);
        vImag[i] = 0.0f;
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }

    // Run FFT pipeline
    FFT.dcRemoval(vReal, FFT_SAMPLES);
    FFT.windowing(vReal, FFT_SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT.compute(vReal, vImag, FFT_SAMPLES, FFT_FORWARD);
    FFT.complexToMagnitude(vReal, vImag, FFT_SAMPLES);

    // Print frequency bins in the vital sign range (0 - 3 Hz = bins 0-12)
    float freqRes = (float)SAMPLE_RATE / FFT_SAMPLES;  // 0.2441 Hz

    Serial.println("─── FFT BINS (0 – 3 Hz) WITHOUT HUMAN ─────────────────");
    Serial.println("  Bin | Freq (Hz) | Magnitude | Band");
    Serial.println("  ----|-----------|-----------|------------------");

    float maxBreathMag = 0, maxHeartMag = 0;

    for (int i = 0; i <= 12; i++) {
        float freq = i * freqRes;
        const char* band = "";
        if (i == 0) band = "DC (ignore)";
        else if (i >= 1 && i <= 2) {
            band = "BREATHING (0.2-0.6 Hz)";
            if (vReal[i] > maxBreathMag) maxBreathMag = vReal[i];
        }
        else if (i == 3) band = "gap";
        else if (i >= 4 && i <= 10) {
            band = "HEARTBEAT (1.0-2.5 Hz)";
            if (vReal[i] > maxHeartMag) maxHeartMag = vReal[i];
        }
        else band = "above band";

        Serial.printf("  %3d | %7.3f   | %9.1f | %s\n", i, freq, vReal[i], band);
    }

    Serial.println("\n─── NOISE FLOOR SUMMARY ────────────────────────────────");
    Serial.printf("  Peak Noise in Breathing Band: %.1f\n", maxBreathMag);
    Serial.printf("  Peak Noise in Heartbeat Band: %.1f\n", maxHeartMag);
    Serial.printf("  → Calibrated Threshold would be: %.1f (noise × 1.5)\n",
                  max(maxBreathMag, maxHeartMag) * 1.5);
    Serial.println("───────────────────────────────────────────────────────\n");
}

// =============================================================================
// TEST 3: FFT WITH Human Present
// =============================================================================
// Same as Test 2, but now a human should be standing 0.5–1m from the sensor.
// Compare the magnitudes — the breathing bins should be MUCH higher.
// =============================================================================
void testFFTWithHuman() {
    Serial.println("\n╔══════════════════════════════════════════════════════╗");
    Serial.println("║  TEST 3: FFT WITH HUMAN (1024-pt)                    ║");
    Serial.println("║  Stand 0.5 – 1.0m in front of the sensor.            ║");
    Serial.println("║  Breathe normally. DO NOT move.                       ║");
    Serial.println("║  Starting in 5 seconds...                             ║");
    Serial.println("╚══════════════════════════════════════════════════════╝\n");
    delay(5000);

    Serial.println("  ▶ Collecting 1024 samples (4.1 seconds)...\n");

    // Collect 1024 samples
    TickType_t xLastWake = xTaskGetTickCount();
    for (int i = 0; i < FFT_SAMPLES; i++) {
        vReal[i] = (float)analogRead(RADAR_ADC_PIN);
        vImag[i] = 0.0f;
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }

    // Run FFT pipeline
    FFT.dcRemoval(vReal, FFT_SAMPLES);
    FFT.windowing(vReal, FFT_SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT.compute(vReal, vImag, FFT_SAMPLES, FFT_FORWARD);
    FFT.complexToMagnitude(vReal, vImag, FFT_SAMPLES);

    float freqRes = (float)SAMPLE_RATE / FFT_SAMPLES;

    Serial.println("─── FFT BINS (0 – 3 Hz) WITH HUMAN ────────────────────");
    Serial.println("  Bin | Freq (Hz) | Magnitude | Band");
    Serial.println("  ----|-----------|-----------|------------------");

    float maxBreathMag = 0, maxHeartMag = 0;
    int maxBreathBin = 1, maxHeartBin = 4;

    for (int i = 0; i <= 12; i++) {
        float freq = i * freqRes;
        const char* band = "";
        if (i == 0) band = "DC (ignore)";
        else if (i >= 1 && i <= 2) {
            band = "BREATHING";
            if (vReal[i] > maxBreathMag) { maxBreathMag = vReal[i]; maxBreathBin = i; }
        }
        else if (i == 3) band = "gap";
        else if (i >= 4 && i <= 10) {
            band = "HEARTBEAT";
            if (vReal[i] > maxHeartMag) { maxHeartMag = vReal[i]; maxHeartBin = i; }
        }
        else band = "above band";

        Serial.printf("  %3d | %7.3f   | %9.1f | %s\n", i, freq, vReal[i], band);
    }

    Serial.println("\n─── SIGNAL ANALYSIS ────────────────────────────────────");
    Serial.printf("  Peak Breathing:  Bin %d = %.3f Hz, Magnitude = %.1f\n",
                  maxBreathBin, maxBreathBin * freqRes, maxBreathMag);
    Serial.printf("  Peak Heartbeat:  Bin %d = %.3f Hz, Magnitude = %.1f\n",
                  maxHeartBin, maxHeartBin * freqRes, maxHeartMag);

    Serial.println("\n─── COMPARE WITH TEST 2 ────────────────────────────────");
    Serial.println("  If breathing magnitude is < 2x the noise floor from Test 2,");
    Serial.println("  your custom LTspice filter is not providing enough gain.");
    Serial.println("  Check if R4/R6 feedback resistors are 1MΩ and R3/R5 input resistors");
    Serial.println("  are 10kΩ. The signal MUST be clearly above the noise floor.");
    Serial.println("───────────────────────────────────────────────────────\n");
}

// =============================================================================
// TEST 4: OP-AMP NOISE ISOLATION
// =============================================================================
// Disconnects the HB100 IF pin to measure pure op-amp circuit noise.
// If noise is still high, the issue lies in the op-amp selection or layout.
// =============================================================================
void testOpAmpNoise() {
    Serial.println("\n╔══════════════════════════════════════════════════════╗");
    Serial.println("║  TEST 4: OP-AMP NOISE ISOLATION                      ║");
    Serial.println("║                                                       ║");
    Serial.println("║  ⚡ DISCONNECT the HB100 IF pin from the C1 input     ║");
    Serial.println("║     capacitor. Keep the rest of the circuit powered.   ║");
    Serial.println("║     Tie Stage 1 input (before C1) to the 1.65V bias.   ║");
    Serial.println("║                                                       ║");
    Serial.println("║  This measures PURE OP-AMP NOISE of the custom design ║");
    Serial.println("║  Starting in 10 seconds — disconnect and tie now!     ║");
    Serial.println("╚══════════════════════════════════════════════════════╝\n");
    delay(10000);

    // Collect 1024 samples
    TickType_t xLastWake = xTaskGetTickCount();
    for (int i = 0; i < FFT_SAMPLES; i++) {
        vReal[i] = (float)analogRead(RADAR_ADC_PIN);
        vImag[i] = 0.0f;
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }

    // Raw statistics
    float sum = 0, minV = 4095, maxV = 0;
    for (int i = 0; i < FFT_SAMPLES; i++) {
        sum += vReal[i];
        if (vReal[i] < minV) minV = vReal[i];
        if (vReal[i] > maxV) maxV = vReal[i];
    }
    float mean = sum / FFT_SAMPLES;
    float swing = maxV - minV;

    // Run FFT
    FFT.dcRemoval(vReal, FFT_SAMPLES);
    FFT.windowing(vReal, FFT_SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT.compute(vReal, vImag, FFT_SAMPLES, FFT_FORWARD);
    FFT.complexToMagnitude(vReal, vImag, FFT_SAMPLES);

    float freqRes = (float)SAMPLE_RATE / FFT_SAMPLES;
    float maxNoiseMag = 0;

    Serial.println("─── PURE OP-AMP NOISE (no radar input) ────────────────");
    Serial.printf("  Raw ADC Mean:      %.0f counts (%.3f V)\n", mean, (mean/4095.0)*3.3);
    Serial.printf("  Raw ADC Swing:     %.0f counts (%.3f V)\n", swing, (swing/4095.0)*3.3);
    Serial.println("");
    Serial.println("  Bin | Freq (Hz) | Magnitude");
    Serial.println("  ----|-----------|----------");

    for (int i = 1; i <= 12; i++) {
        Serial.printf("  %3d | %7.3f   | %9.1f\n", i, i * freqRes, vReal[i]);
        if (vReal[i] > maxNoiseMag) maxNoiseMag = vReal[i];
    }

    Serial.println("\n─── OP-AMP NOISE DIAGNOSIS ─────────────────────────────");
    Serial.printf("  Peak op-amp noise magnitude: %.1f\n", maxNoiseMag);

    if (maxNoiseMag > 30) {
        Serial.println("\n  ❌ OP-AMP NOISE IS TOO HIGH!");
        Serial.println("     The op-amp circuit is amplifying its own internal noise");
        Serial.println("     to a level that drowns out the radar signal.");
        Serial.println("");
        Serial.println("  ┌─────────────────────────────────────────────────────┐");
        Serial.println("  │  RECOMMENDED FIXES (in order of effectiveness):     │");
        Serial.println("  │                                                     │");
        Serial.println("  │  1. Replace LM358 with a low-noise, rail-to-rail    │");
        Serial.println("  │     pin-compatible op-amp (e.g. MCP6002 or OPA2340).│");
        Serial.println("  │                                                     │");
        Serial.println("  │  2. Verify feedback capacitors C2 and C4 (0.047 µF) │");
        Serial.println("  │     are connected securely. They limit LPF bandwidth│");
        Serial.println("  │     and are crucial for suppressing high-freq noise.│");
        Serial.println("  │                                                     │");
        Serial.println("  │  3. Add a 10µF bypass capacitor across R2 in the    │");
        Serial.println("  │     1.65V bias divider to filter power supply noise.│");
        Serial.println("  │                                                     │");
        Serial.println("  │  4. Reduce gain: change feedback resistors R4 & R6  │");
        Serial.println("  │     from 1MΩ to 470kΩ to lower noise floor output.  │");
        Serial.println("  └─────────────────────────────────────────────────────┘");
    } else if (maxNoiseMag > 10) {
        Serial.println("\n  ⚠  MODERATE OP-AMP NOISE");
        Serial.println("     This is borderline. It might work but detection will be unreliable.");
        Serial.println("     → Verify feedback capacitors C2 and C4 are active.");
        Serial.println("     → Use shorter, cleaner layout/wires and bypass capacitors.");
    } else {
        Serial.println("\n  ✅ OP-AMP NOISE IS LOW — custom filter circuit is quiet.");
        Serial.println("     If Test 3 still shows poor signal, the problem is:");
        Serial.println("     → HB100 not producing enough IF signal (check orientation)");
        Serial.println("     → C1 or C3 coupling capacitors damaged or incorrect values");
        Serial.println("     → Barrier too thick or metallic");
    }
    Serial.println("───────────────────────────────────────────────────────\n");
}

// =============================================================================
// MAIN
// =============================================================================
void setup() {
    Serial.begin(115200);
    analogReadResolution(12);
    delay(1000);

    Serial.println("\n\n");
    Serial.println("╔══════════════════════════════════════════════════════╗");
    Serial.println("║    H.A.W.K. HARDWARE DIAGNOSTIC TOOL v1.1 (LTspice)  ║");
    Serial.println("║    Custom 2-Stage Bandpass + HB100 Debugger          ║");
    Serial.println("╠══════════════════════════════════════════════════════╣");
    Serial.println("║  This tool runs 4 tests to isolate your noise issue ║");
    Serial.println("║  Total time: ~40 seconds                            ║");
    Serial.println("╚══════════════════════════════════════════════════════╝\n");
    delay(2000);

    // Run all 4 tests sequentially
    testRawADC();
    testFFTNoiseFloor();
    testFFTWithHuman();
    testOpAmpNoise();

    // Final summary
    Serial.println("\n╔══════════════════════════════════════════════════════╗");
    Serial.println("║              ALL TESTS COMPLETE                      ║");
    Serial.println("╠══════════════════════════════════════════════════════╣");
    Serial.println("║  Copy the ENTIRE output above and share it.          ║");
    Serial.println("║                                                      ║");
    Serial.println("║  Key numbers to compare:                             ║");
    Serial.println("║    Test 2 noise magnitude  vs  Test 3 signal mag     ║");
    Serial.println("║    Signal MUST be > 2x noise for reliable detection  ║");
    Serial.println("║                                                      ║");
    Serial.println("║  If Test 4 noise is high → LM358 is the problem     ║");
    Serial.println("║  If Test 4 noise is low  → check HB100 wiring       ║");
    Serial.println("╚══════════════════════════════════════════════════════╝\n");
}

void loop() {
    // Do nothing — tests run once in setup
    delay(10000);
}
