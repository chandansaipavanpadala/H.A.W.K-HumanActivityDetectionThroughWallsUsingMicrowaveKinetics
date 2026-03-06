#include "radar_sensor.h"
#include "globals.h" 

// --- Radar Sampling Parameters ---
// To satisfy the Nyquist Theorem for human vitals (max 3Hz), 
// a sampling rate of 250 Hz is excellent. 
// 1000ms / 250Hz = 4ms period.
#define SAMPLING_PERIOD_MS 4 

void initRadar() {
    // Set ESP32 ADC to 12-bit resolution (values from 0 to 4095)
    // This high resolution is critical for catching faint breathing ripples
    analogReadResolution(12);
    
    // RADAR_ADC_PIN is defined in globals.h
    pinMode(RADAR_ADC_PIN, INPUT);
    
    Serial.println("SYS_INIT: Radar ADC Ready.");
}

void vRadarAcquisitionTask(void *pvParameters) {
    // 1. Initialize the ADC hardware
    initRadar();

    // 2. Setup deterministic FreeRTOS timing
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(SAMPLING_PERIOD_MS);

    // Get the initial tick count
    xLastWakeTime = xTaskGetTickCount();

    uint16_t adcValue = 0;

    // 3. The Core Real-Time Loop
    for (;;) {
        // Read the analog signal coming from your IF amplifier circuit
        adcValue = analogRead(RADAR_ADC_PIN);

        // Safely push the data to the queue
        if (rawDataQueue != NULL) {
            // The '0' timeout is crucial here. This is the highest priority task.
            // If the queue is full (FFT task is lagging), we DO NOT wait. 
            // We drop the sample and move on to maintain our strict 250Hz timing.
            BaseType_t xStatus = xQueueSend(rawDataQueue, &adcValue, 0);
            
            if (xStatus != pdPASS) {
                // If this prints often, you know your FFT task needs optimizing
                Serial.println("ERR: rawDataQueue FULL! Sample dropped.");
            }
        }

        // 4. Sleep until the exact 4ms mark
        // Unlike delay(), this accounts for the time it took to run analogRead()
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}