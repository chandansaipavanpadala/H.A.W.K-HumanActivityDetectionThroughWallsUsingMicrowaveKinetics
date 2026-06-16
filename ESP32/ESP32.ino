#include "globals.h"
#include "radar_sensor.h"
#include "signal_processing.h"
#include "detection.h"
#include "comms_ui.h"

// Define the RTOS handles
QueueHandle_t rawDataQueue;
QueueHandle_t processedDataQueue;
QueueHandle_t dashboardQueue;
SemaphoreHandle_t detectionSemaphore;

// Define the global system state
// (declared as extern in globals.h)
volatile SystemState systemState = STATE_CALIBRATING;

void setup() {
    Serial.begin(115200);
    delay(1000);  // Wait for serial port to stabilize
    Serial.println();
    Serial.println("=== H.A.W.K. System Boot ===");
    Serial.print("VitalSignData size: ");
    Serial.println(sizeof(VitalSignData));
    Serial.print("DashboardTelemetry size: ");
    Serial.println(sizeof(DashboardTelemetry));
    Serial.print("Free heap before queues: ");
    Serial.println(ESP.getFreeHeap());

    // 1. Create Queues and Semaphores
    rawDataQueue       = xQueueCreate(2048, sizeof(uint16_t));            // Buffered for 1024-pt FFT accumulation
    processedDataQueue = xQueueCreate(4, sizeof(VitalSignData));          // Holds dual-band vital sign data (reduced depth)
    dashboardQueue     = xQueueCreate(4, sizeof(DashboardTelemetry));     // Detection → CommsUI telemetry
    detectionSemaphore = xSemaphoreCreateBinary();                        // Triggers when a human is found

    Serial.print("Free heap after queues: ");
    Serial.println(ESP.getFreeHeap());

    if (rawDataQueue == NULL || processedDataQueue == NULL ||
        dashboardQueue == NULL || detectionSemaphore == NULL) {
        Serial.println("ERROR: RTOS queue creation failed!");
        // RTOS object creation failed — system halted
        while (1); 
    }
    Serial.println("Queues OK");

    // 2a. Connect to WiFi and start WebSocket server (must run before scheduler)
    initCommsWiFi();

    // 2. Launch the Tasks (Name, Stack Size, Params, Priority, Task Handle)
    // Priorities: Higher number = Higher Priority in FreeRTOS
    xTaskCreate(vRadarAcquisitionTask, "RadarTask",  2048, NULL, 4, NULL);
    xTaskCreate(vSignalProcessingTask, "ProcessTask", 8192, NULL, 3, NULL);
    xTaskCreate(vDetectionTask,        "DetectTask",  6144, NULL, 2, NULL);
    xTaskCreate(vCommsUITask,          "CommsTask",   8192, NULL, 1, NULL);
}

void loop() {
    // Architecture Rule: Delete the Arduino loopTask to free CPU.
    // All work is done by our FreeRTOS tasks.
    vTaskDelete(NULL);
}