#include "signal_processing.h"
#include "globals.h"
#include "arduinoFFT.h" // You'll need to install this via Library Manager

// FFT Configuration
#define SAMPLES 128             // Must be a power of 2
#define SAMPLING_FREQUENCY 250  // Must match your Radar Acquisition Task (250Hz)

ArduinoFFT<double> FFT = ArduinoFFT<double>(); 

double vReal[SAMPLES];
double vImag[SAMPLES];
int sampleCounter = 0;

void vSignalProcessingTask(void *pvParameters) {
    uint16_t rawSample;

    for (;;) {
        // 1. Collect samples from the Radar Task via the Queue
        if (xQueueReceive(rawDataQueue, &rawSample, portMAX_DELAY) == pdPASS) {
            
            vReal[sampleCounter] = (double)rawSample;
            vImag[sampleCounter] = 0.0; // Imaginary part is 0 for real-world signals
            sampleCounter++;

            // 2. Once we have enough samples, run the FFT
            if (sampleCounter == SAMPLES) {
                
                // Remove DC Offset (crucial for breathing detection)
                FFT.dcRemoval(vReal, SAMPLES);
                
                // Apply Windowing (reduces leakage)
                FFT.windowing(vReal, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
                
                // Compute FFT
                FFT.compute(vReal, vImag, SAMPLES, FFT_FORWARD);
                
                // Calculate Magnitudes
                FFT.complexToMagnitude(vReal, vImag, SAMPLES);

                // 3. Find the dominant frequency
                double peakFreq = FFT.majorPeak(vReal, SAMPLES, SAMPLING_FREQUENCY);

                // 4. Send this result to the Detection Task
                xQueueSend(processedDataQueue, &peakFreq, 0);

                // Reset counter for next batch
                sampleCounter = 0;
            }
        }
    }
}