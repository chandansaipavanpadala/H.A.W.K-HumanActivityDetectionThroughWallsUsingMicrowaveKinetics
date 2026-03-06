#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// --- Pin Definitions ---
#define RADAR_ADC_PIN 34
#define BUZZER_PIN 25
#define LED_PIN 2

// --- FreeRTOS Handles ---
// Use 'extern' so they are declared here but defined in ESP32.ino
extern QueueHandle_t rawDataQueue;
extern QueueHandle_t processedDataQueue;
extern SemaphoreHandle_t detectionSemaphore;

#endif // GLOBALS_H