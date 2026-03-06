#include "detection.h"
#include "globals.h"

// --- Detection Tuning Parameters ---
// Human breathing is typically 0.1 - 0.5 Hz
// Human heartbeat is typically 0.8 - 2.5 Hz
const float MIN_HUMAN_FREQ = 0.1;
const float MAX_HUMAN_FREQ = 3.0;

// How many consecutive valid signals are needed to trigger the alarm
const int REQUIRED_CONFIDENCE = 5; 

void vDetectionTask(void *pvParameters) {
    float incomingFrequency;
    int confidenceLevel = 0;

    for (;;) {
        // Wait indefinitely for the FFT task to send a processed frequency
        if (xQueueReceive(processedDataQueue, &incomingFrequency, portMAX_DELAY) == pdPASS) {
            
            // Check if the dominant frequency matches human vitals
            if (incomingFrequency >= MIN_HUMAN_FREQ && incomingFrequency <= MAX_HUMAN_FREQ) {
                
                confidenceLevel++; // Increase confidence
                
                Serial.print("Target Tracking... Confidence: ");
                Serial.print(confidenceLevel);
                Serial.print("/");
                Serial.println(REQUIRED_CONFIDENCE);

            } else {
                // If a random noise frequency appears, slowly decay the confidence
                // We decay rather than reset to 0 immediately, in case of a single dropped frame
                if (confidenceLevel > 0) {
                    confidenceLevel--; 
                }
            }

            // Once we hit the required threshold, we have a confirmed human
            if (confidenceLevel >= REQUIRED_CONFIDENCE) {
                
                Serial.println("STATUS: CONFIRMED HUMAN PRESENCE!");
                
                // Wake up the UI/Comms Task to trigger the buzzer and WiFi
                xSemaphoreGive(detectionSemaphore);
                
                // Reset confidence so the alarm doesn't get stuck in an infinite loop
                confidenceLevel = 0;
                
                // Cooldown period: Ignore new signals for 5 seconds after an alarm
                vTaskDelay(pdMS_TO_TICKS(5000)); 
            }
        }
    }
}