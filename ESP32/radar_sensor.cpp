#include "radar_sensor.h"
#include "globals.h" // Includes our Queue handles and Pin definitions

// Define sampling parameters
// 250 Hz sampling rate = 4 milliseconds per sample
#define SAMPLING_PERIOD_MS 4 

void initRadar() {
    // ESP32 default ADC resolution is 12-bit (values from 0 to 4095)
    analogReadResolution(12);
    
    // RADAR_ADC_PIN must be defined in globals.h (e.g., GPIO 34)
    pinMode(RADAR_ADC_PIN, INPUT);
    
    Serial.println("Radar ADC initialized.");
}

void vRadarAcquisitionTask(void *pvParameters) {
    // 1. Initialize hardware specific to this task
    initRadar();

    // 2. Setup precise timing variables
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(SAMPLING_PERIOD_MS);

    // Initialize the xLastWakeTime variable with the current FreeRTOS tick count
    xLastWakeTime = xTaskGetTickCount();

    uint16_t adcValue = 0;

    // 3. The infinite RTOS Task Loop
    for (;;) {
        // Read the analog baseband signal from the HB100/CDM324
        adcValue = analogRead(RADAR_ADC_PIN);

        // Send the raw data to the Signal Processing Task via the FreeRTOS Queue
        // We use a timeout of '0' (non-blocking). Since this is the highest priority task, 
        // we NEVER want it to wait if the queue is full. 
        if (rawDataQueue != NULL) {
            xQueueSend(rawDataQueue, &adcValue, 0);
        }

        // Delay until the exact next 4ms interval to guarantee a perfect 250Hz sample rate
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}