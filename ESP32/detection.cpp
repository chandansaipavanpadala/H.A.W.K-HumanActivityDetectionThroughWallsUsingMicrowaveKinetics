#include "detection.h"
#include "globals.h"

void vDetectionTask(void *pvParameters) {
    float incomingFrequency;

    for (;;) {
        // Wait for processed frequency data
        if (xQueueReceive(processedDataQueue, &incomingFrequency, portMAX_DELAY) == pdPASS) {
            
            // Check if frequency is within human breathing/heartbeat range (0.1Hz - 3Hz)
            if (incomingFrequency >= 0.1 && incomingFrequency <= 3.0) {
                // Human signature detected! Trigger the semaphore for the UI task
                xSemaphoreGive(detectionSemaphore);
            }
        }
    }
}