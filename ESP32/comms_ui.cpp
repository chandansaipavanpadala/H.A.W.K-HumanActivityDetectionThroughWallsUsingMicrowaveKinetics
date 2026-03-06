#include "comms_ui.h"
#include "globals.h"

void vCommsUITask(void *pvParameters) {
    // Hardware setup for UI
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);

    for (;;) {
        // This task blocks and waits FOREVER until a human is detected
        if (xSemaphoreTake(detectionSemaphore, portMAX_DELAY) == pdTRUE) {
            Serial.println("ALERT: Human Presence Detected!");
            
            // Trigger alarms
            digitalWrite(LED_PIN, HIGH);
            digitalWrite(BUZZER_PIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(1000)); // Buzz for 1 second
            
            digitalWrite(LED_PIN, LOW);
            digitalWrite(BUZZER_PIN, LOW);

            // TODO: Add WiFi/Bluetooth transmission logic here later
        }
    }
}