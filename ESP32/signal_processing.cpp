#include "signal_processing.h"
#include "globals.h"

void vSignalProcessingTask(void *pvParameters) {
    uint16_t rawSample;
    float dominantFrequency = 0.0;

    for (;;) {
        // Wait indefinitely (portMAX_DELAY) until data arrives from the Radar Task
        if (xQueueReceive(rawDataQueue, &rawSample, portMAX_DELAY) == pdPASS) {
            
            // TODO: Accumulate samples into an array and run the FFT algorithm here.
            // For now, we simulate processing by sending a mock frequency.
            
            // Simulated dominant frequency (e.g., 0.3 Hz for breathing)
            dominantFrequency = 0.3; 

            // Send processed frequency to the Detection task
            xQueueSend(processedDataQueue, &dominantFrequency, 0);
        }
    }
}