#include <Arduino.h>

// --- HARDWARE PINS ---
#define RADAR_ADC_PIN 34 // Must match the output of your Op-Amp Stage 2

// --- TIMING CONSTANTS ---
#define SAMPLE_RATE_HZ 250
#define SAMPLE_PERIOD_MS (1000 / SAMPLE_RATE_HZ) // 4ms loop time

// --- VOLTAGE TRACKING ---
// Track min/max/mean to help verify the DC bias from the op-amp circuit.
// The expected DC bias depends on your circuit configuration:
//   - R1=R2=10k voltage divider from 3.3V → 1.65V bias
//   - Actual measured bias may differ (e.g., ~2.12V observed in practice)
//   - The key check: signal should be centered (not clipping at 0V or 3.3V)
static float voltageMin = 3.3f;
static float voltageMax = 0.0f;
static float voltageSum = 0.0f;
static uint32_t voltageSampleCount = 0;
static const uint32_t REPORT_INTERVAL = SAMPLE_RATE_HZ * 5; // Report every 5 seconds

// --- FREERTOS TASK ---
void TaskOscilloscope(void *pvParameters) {
  // Setup deterministic timer
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(SAMPLE_PERIOD_MS);

  for (;;) {
    // 1. Read the 12-bit ADC (Range: 0 to 4095)
    uint16_t raw_adc = analogRead(RADAR_ADC_PIN);

    // 2. Convert raw ADC to Voltage (Assumes 3.3V reference)
    float voltage = (raw_adc / 4095.0) * 3.3;

    // 3. Track voltage statistics for bias verification
    if (voltage < voltageMin) voltageMin = voltage;
    if (voltage > voltageMax) voltageMax = voltage;
    voltageSum += voltage;
    voltageSampleCount++;

    // 4. Print to Serial. 
    // The Arduino Serial Plotter will automatically graph this value.
    Serial.print("Voltage:");
    Serial.println(voltage);

    // 5. Periodically print voltage statistics
    if (voltageSampleCount >= REPORT_INTERVAL) {
      float voltageMean = voltageSum / voltageSampleCount;
      Serial.println("--- VOLTAGE STATS (last 5s) ---");
      Serial.print("  DC Bias (Mean): "); Serial.print(voltageMean, 3); Serial.println(" V");
      Serial.print("  Min: "); Serial.print(voltageMin, 3); Serial.println(" V");
      Serial.print("  Max: "); Serial.print(voltageMax, 3); Serial.println(" V");
      Serial.print("  Swing (pk-pk): "); Serial.print(voltageMax - voltageMin, 3); Serial.println(" V");
      
      // Check for potential issues
      if (voltageMin < 0.1) Serial.println("  ⚠ WARNING: Signal may be clipping at GND!");
      if (voltageMax > 3.2) Serial.println("  ⚠ WARNING: Signal may be clipping at VCC!");
      if (voltageMax - voltageMin < 0.01) Serial.println("  ⚠ WARNING: No AC signal detected — check radar connection.");
      Serial.println("-------------------------------");
      
      // Reset for next interval
      voltageMin = 3.3f;
      voltageMax = 0.0f;
      voltageSum = 0.0f;
      voltageSampleCount = 0;
    }

    // 6. Block task until exactly 4ms have passed (250 Hz)
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

void setup() {
  Serial.begin(115200); // High baud rate required for 250Hz streaming
  
  // Explicitly set ESP32 ADC to 12-bit resolution
  analogReadResolution(12);

  Serial.println("====================================");
  Serial.println("  H.A.W.K. Oscilloscope Utility");
  Serial.println("====================================");
  Serial.println("Open the Serial Plotter (Tools -> Serial Plotter)!");
  Serial.println("");
  Serial.println("NOTE: Expected DC bias depends on your circuit:");
  Serial.println("  - Ideal (R1=R2 divider from 3.3V): ~1.65V");
  Serial.println("  - Actual may vary (e.g., ~2.1V observed)");
  Serial.println("  - Key: signal should NOT clip at 0V or 3.3V");
  Serial.println("");
  Serial.println("Voltage statistics will print every 5 seconds.");
  Serial.println("====================================");

  // Launch the FreeRTOS Task
  xTaskCreatePinnedToCore(
    TaskOscilloscope,   // Function to call
    "OscilloscopeTask", // Name for debugging
    2048,               // Stack size
    NULL,               // Task parameters
    1,                  // Priority (1 is fine for a single task)
    NULL,               // Task handle
    1                   // Pin to Core 1 (Arduino loop core)
  );
}

void loop() {
  // In a FreeRTOS architecture, the standard loop is dead space.
  vTaskDelete(NULL); 
}