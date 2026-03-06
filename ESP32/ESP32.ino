#include "globals.h"
#include "radar_sensor.h"
#include "signal_processing.h"
#include "detection.h"
#include "comms_ui.h"

// Define the RTOS handles
QueueHandle_t rawDataQueue;
QueueHandle_t processedDataQueue;
SemaphoreHandle_t detectionSemaphore;

void setup() {
    Serial.begin(115200);
    Serial.println("Starting Through-Wall Human Detection System...");

    // 1. Create Queues and Semaphores
    rawDataQueue = xQueueCreate(256, sizeof(uint16_t));     // Holds raw ADC values
    processedDataQueue = xQueueCreate(128, sizeof(float));  // Holds processed peak frequencies
    detectionSemaphore = xSemaphoreCreateBinary();          // Triggers when a human is found

    if (rawDataQueue == NULL || processedDataQueue == NULL || detectionSemaphore == NULL) {
        Serial.println("Failed to create RTOS objects! Freezing...");
        while (1); 
    }

    // 2. Launch the Tasks (Name, Stack Size, Params, Priority, Task Handle)
    // Priorities: Higher number = Higher Priority in FreeRTOS
    xTaskCreate(vRadarAcquisitionTask, "RadarTask", 2048, NULL, 4, NULL);
    xTaskCreate(vSignalProcessingTask, "ProcessTask", 4096, NULL, 3, NULL);
    xTaskCreate(vDetectionTask,        "DetectTask",  2048, NULL, 2, NULL);
    xTaskCreate(vCommsUITask,          "CommsTask",   2048, NULL, 1, NULL);
}

void loop() {
    // Empty! The FreeRTOS Scheduler is running the tasks in the background.
}