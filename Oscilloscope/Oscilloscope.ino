#include <Arduino.h>

// --- HARDWARE PINS ---
#define RADAR_ADC_PIN 34 // Must match the output of your Op-Amp Stage 2

// --- TIMING CONSTANTS ---
#define SAMPLE_RATE_HZ 250
#define SAMPLE_PERIOD_MS (1000 / SAMPLE_RATE_HZ) // 4ms loop time

// --- FREERTOS TASK ---
void TaskOscilloscope(void *pvParameters) {
  // Setup deterministic timer
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(SAMPLE_PERIOD_MS);

  for (;;) {
    // 1. Read the 12-bit ADC (Range: 0 to 4095)
    uint16_t raw_adc = analogRead(RADAR_ADC_PIN);

    // 2. Convert raw ADC to Voltage (Assumes 3.3V reference)
    // We do this so you can visually verify your 1.65V DC Bias
    float voltage = (raw_adc / 4095.0) * 3.3;

    // 3. Print to Serial. 
    // The Arduino Serial Plotter will automatically graph this value.
    Serial.print("Voltage:");
    Serial.println(voltage);

    // 4. Block task until exactly 4ms have passed (250 Hz)
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

void setup() {
  Serial.begin(115200); // High baud rate required for 250Hz streaming
  
  // Explicitly set ESP32 ADC to 12-bit resolution
  analogReadResolution(12);

  Serial.println("Starting ESP32 Oscilloscope...");
  Serial.println("Open the Serial Plotter (Tools -> Serial Plotter)!");

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