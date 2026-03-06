```markdown
# Real-Time Through-Wall Human Detection System

[![Platform](https://img.shields.io/badge/Platform-ESP32-blue.svg)](https://www.espressif.com/en/products/socs/esp32)
[![OS](https://img.shields.io/badge/OS-FreeRTOS-orange.svg)](https://freertos.org/)
[![Build](https://img.shields.io/badge/Build-Passing-brightgreen.svg)]()
[![License](https://img.shields.io/badge/License-MIT-blue.svg)]()

## Overview
An RTOS-based embedded system designed for non-line-of-sight (NLOS) human detection using microwave Doppler radar. By penetrating solid obstacles such as drywall, rubble, and smoke, the system detects micro-movements—specifically human breathing (0.1–0.5 Hz) and heartbeats (0.8–2.5 Hz).

Utilizing an ESP32 dual-core processor running FreeRTOS, the architecture guarantees deterministic timing and real-time preemptive multitasking, ensuring life-saving signals are never missed during high-stakes operations.

## Key Applications

### Tactical and Defense Operations
Provides critical situational awareness for armed forces during urban warfare, hostage rescue, and tactical reconnaissance by identifying human presence behind barriers before entry.

### Search and Rescue (SAR)
Enables emergency responders to locate survivors trapped under earthquake debris or collapsed structures by detecting faint vital signs.

### Advanced Security
Covert perimeter monitoring and intrusion detection in zero-visibility environments.

## Hardware Architecture

- **Microcontroller:** ESP32 Development Board (32-bit, Dual-Core)
- **Radar Sensor:** HB100 / CDM324 Microwave Doppler Radar Module (10.525 GHz / 24 GHz)
- **Signal Conditioning:** Custom Active Bandpass Filter (Op-Amp based) tuned to 0.1 Hz – 3.0 Hz to isolate human vitals and eliminate high-frequency mechanical noise.
- **Output Interface:** OLED Display, Active Buzzer, and LED indicators.

## FreeRTOS Software Architecture

The system abandons standard sequential loops in favor of a Preemptive Priority-Based RTOS scheduler. The workload is distributed across four distinct, concurrent tasks:

### 1. Radar Acquisition Task (Priority 4 - Highest)
- Samples the analog radar signal at a strict, deterministic **250 Hz** using `vTaskDelayUntil()`.
- Pushes raw **12-bit ADC data** to the processing queue non-blockingly.

### 2. Signal Processing Task (Priority 3)
- Accumulates data arrays and utilizes the **arduinoFFT** library to perform Fast Fourier Transforms.
- Applies **DC removal and Hamming windowing** to extract dominant micro-motion frequencies.

### 3. Detection and Decision Task (Priority 2)
- Evaluates FFT output against physiological thresholds **(0.1 - 3.0 Hz)**.
- Utilizes a continuous **Confidence Level algorithm** to require multiple consecutive positive readings, effectively mitigating false positives.

### 4. Communication and UI Task (Priority 1 - Lowest)
- Blocks safely on a **FreeRTOS Semaphore** until a human presence is confirmed.
- Triggers local hardware alarms and handles external data transmission over WiFi.

## Telemetry and Data Protocol

The system utilizes a proprietary packet structure for seamless integration with external monitoring consoles or tactical dashboards.

Upon positive detection, the system broadcasts via serial and wireless interfaces:

```

$TARA,ALERT,HUMAN_DETECTED,TIME:[ms_timestamp]

```

## Repository Structure

```

├── ESP32.ino                  # Main entry point, FreeRTOS queue/task initialization
├── globals.h                  # Shared pin definitions and RTOS handles
├── radar_sensor.cpp / .h      # Hardware interfacing and 250Hz sampling task
├── signal_processing.cpp / .h # FFT computation and frequency extraction task
├── detection.cpp / .h         # Confidence algorithm and physiological matching task
└── comms_ui.cpp / .h          # Local UI and WiFi telemetry task

````

## Getting Started

### Prerequisites

**Hardware**
- ESP32 Development Board
- HB100/CDM324 Radar Module
- Active Low-Pass/Bandpass Circuit components

**Software**
- Arduino IDE 2.x or PlatformIO

**Libraries**
- arduinoFFT by Kosme (v2.0.0 or higher)
- Standard ESP32 Board Package installed via Board Manager

### Installation and Setup

#### Clone the Repository

```bash
git clone https://github.com/yourusername/Through-Wall-Human-Detection.git
cd Through-Wall-Human-Detection
````

#### Hardware Wiring

* Connect the output of your analog bandpass filter circuit to **RADAR_ADC_PIN** (Default: GPIO 34).
* Connect the **active buzzer** to **BUZZER_PIN** (Default: GPIO 25).
* Connect the **indicator LED** to **LED_PIN** (Default: GPIO 2).

#### Configuration

* Open `comms_ui.cpp` and update the **WiFi credentials** if wireless telemetry is required.
* Open `globals.h` to modify **pin definitions** to match your physical hardware setup.

#### Compilation and Flashing

1. Open `ESP32.ino` in your IDE.
2. Select your specific **ESP32 board model** and **COM port**.
3. Compile and upload the firmware.

#### System Validation

1. Open the **Serial Monitor** at **115200 baud**.
2. Verify system initialization messages and ensure the `rawDataQueue` is not dropping samples.
3. Introduce a target subject in front of the radar obstacle.
4. Monitor the **confidence level outputs** to confirm detection.

```
```
