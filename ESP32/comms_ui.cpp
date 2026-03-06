#include "comms_ui.h"
#include "globals.h"
#include <WiFi.h>

// --- WiFi Configuration ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

void vCommsUITask(void *pvParameters) {
    // 1. Initialize Hardware Pins
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);

    // 2. Initialize WiFi for the Monitoring Console 
    // (Uncomment this block when you are ready to test the wireless features)
    /*
    Serial.print("SYS_INIT: Connecting to WiFi");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(500)); // Non-blocking RTOS delay
        Serial.print(".");
    }
    Serial.println("\nSYS_INIT: WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    */

    for (;;) {
        // 3. Block and wait FOREVER until the Detection Task finds a human
        // Because it waits here, this task consumes almost 0% CPU while idle
        if (xSemaphoreTake(detectionSemaphore, portMAX_DELAY) == pdTRUE) {
            
            // Turn on the local hardware alarms
            digitalWrite(LED_PIN, HIGH);
            digitalWrite(BUZZER_PIN, HIGH);

            // 4. Construct and transmit the system data packet
            // Using the designated structure for the monitoring console
            String dataPacket = "$TARA,ALERT,HUMAN_DETECTED,TIME:" + String(millis());
            
            // Output to the Serial Monitor (Laptop)
            Serial.println(dataPacket);

            // TODO for Phase 3: Push this packet over WiFi
            // if (WiFi.status() == WL_CONNECTED) {
            //     // Insert UDP or HTTP POST logic here to send dataPacket to the dashboard
            // }

            // 5. Keep the alarms active for 1.5 seconds
            // vTaskDelay allows higher-priority tasks (like Radar) to keep running during the buzz
            vTaskDelay(pdMS_TO_TICKS(1500)); 
            
            // 6. Reset the alarms and go back to waiting
            digitalWrite(LED_PIN, LOW);
            digitalWrite(BUZZER_PIN, LOW);
        }
    }
}