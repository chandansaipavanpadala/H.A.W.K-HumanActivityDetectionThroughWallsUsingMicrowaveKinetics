<div align="center">

# 🦅 Project H.A.W.K.

### **H**uman **A**ctivity Detection Through **W**alls Using Microwave **K**inetics

*A FreeRTOS-powered through-wall life detection radar with real-time vital sign extraction and a tactical LAN dashboard.*

---

[![Platform](https://img.shields.io/badge/Platform-ESP32_Dual_Core-0078D4?style=for-the-badge&logo=espressif&logoColor=white)](https://www.espressif.com/en/products/socs/esp32)
[![OS](https://img.shields.io/badge/RTOS-FreeRTOS-F7931E?style=for-the-badge&logo=freertos&logoColor=white)](https://freertos.org/)
[![FFT](https://img.shields.io/badge/FFT-1024_Point-8B5CF6?style=for-the-badge)]()
[![Dashboard](https://img.shields.io/badge/Dashboard-WebSocket_LAN-22D3EE?style=for-the-badge)]()
[![License](https://img.shields.io/badge/License-MIT-34D399?style=for-the-badge)](LICENSE)

</div>

---

## 📋 Table of Contents

- [Overview](#overview)
- [Key Applications](#-key-applications)
- [System Architecture](#-system-architecture)
- [Hardware Architecture](#-hardware-architecture)
- [FreeRTOS Task Architecture](#-freertos-task-architecture)
- [Signal Processing Pipeline](#-signal-processing-pipeline)
- [Dynamic Auto-Calibration](#-dynamic-auto-calibration)
- [Tactical LAN Dashboard](#-tactical-lan-dashboard)
- [Communication Protocol](#-communication-protocol)
- [Repository Structure](#-repository-structure)
- [Getting Started](#-getting-started)
- [Development Tools](#-development-tools)
- [License](#-license)

---

## Overview

Project H.A.W.K. is an RTOS-based embedded system engineered for **non-line-of-sight (NLOS) human detection** using microwave Doppler radar. The system penetrates solid obstacles — drywall, rubble, wood, and smoke — to detect physiological micro-movements: **breathing (0.2–0.6 Hz)** and **heartbeat (1.0–2.5 Hz)**.

Built on an **ESP32 dual-core processor** running **FreeRTOS**, the architecture guarantees deterministic timing and real-time preemptive multitasking across four concurrent tasks. A **1024-point FFT pipeline** with parabolic interpolation provides sub-bin frequency accuracy for vital sign extraction. A **15-second auto-calibration sequence** dynamically learns the environmental noise floor on every boot, and all telemetry is streamed over **WebSocket** to a tactical browser-based dashboard accessible from any device on the local network.

---

## 🎯 Key Applications

| Domain | Use Case |
|---|---|
| **Tactical & Defense** | Urban warfare situational awareness — detect human presence behind walls before room entry, hostage rescue, and tactical reconnaissance. |
| **Search & Rescue** | Locate survivors trapped under earthquake debris or collapsed structures by detecting faint vital signs through rubble. |
| **Advanced Security** | Covert perimeter monitoring and intrusion detection in zero-visibility environments (smoke, darkness, fog). |
| **Medical Monitoring** | Non-contact vital sign monitoring in isolation wards or hazardous environments. |

---

## 🏗 System Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        PROJECT H.A.W.K. — SYSTEM OVERVIEW                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   ┌──────────────┐     ┌──────────────┐     ┌──────────────────────────┐   │
│   │  HB100/CDM324│     │  Active BPF  │     │     ESP32 (Dual-Core)    │   │
│   │  Doppler     │────▶│  0.1–3.0 Hz  │────▶│      FreeRTOS Kernel     │   │
│   │  Radar       │  IF │  Op-Amp      │ ADC │                          │   │
│   │  10.525 GHz  │     │  Circuit     │     │  ┌─────────────────────┐ │   │
│   └──────────────┘     └──────────────┘     │  │ T1: Radar Sampling  │ │   │
│                                              │  │     250 Hz (P4)     │ │   │
│                                              │  ├─────────────────────┤ │   │
│                                              │  │ T2: 1024-pt FFT    │ │   │
│   ┌──────────────────────────────────────┐  │  │     Pipeline (P3)   │ │   │
│   │     TACTICAL LAN DASHBOARD           │  │  ├─────────────────────┤ │   │
│   │  ┌────────┐ ┌────────┐ ┌──────────┐ │  │  │ T3: Detection &     │ │   │
│   │  │Vital   │ │Confid. │ │ Event    │ │◀─┤  │   Calibration (P2)  │ │   │
│   │  │Charts  │ │Gauge   │ │ Log      │ │  │  ├─────────────────────┤ │   │
│   │  └────────┘ └────────┘ └──────────┘ │  │  │ T4: WebSocket &     │ │   │
│   │  WebSocket on ws://<ESP32_IP>:81     │  │  │     Alarms (P1)     │ │   │
│   └──────────────────────────────────────┘  │  └─────────────────────┘ │   │
│                                              └──────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 🔧 Hardware Architecture

| Component | Specification | Purpose |
|---|---|---|
| **Microcontroller** | ESP32 DevKit (32-bit, Dual-Core Xtensa LX6, 240 MHz) | FreeRTOS host, ADC sampling, WiFi/WebSocket server |
| **Radar Sensor** | HB100 (10.525 GHz) / CDM324 (24 GHz) Microwave Doppler | Emits CW microwave; IF output encodes target micro-motion |
| **Signal Conditioning** | Custom 2-stage Active Bandpass Filter (Op-Amp) | Passband: 0.1–3.0 Hz — isolates human vitals, rejects mechanical noise |
| **ADC Interface** | ESP32 12-bit SAR ADC (GPIO 34) | 0–4095 counts at 250 Hz deterministic sampling |
| **Alert Output** | Active Buzzer (GPIO 25) + LED Indicator (GPIO 2) | 1.5-second local hardware alarm on confirmed detection |
| **Dashboard** | Any browser on the LAN | Real-time tactical display via WebSocket (`ws://<IP>:81`) |

### Pin Map

| Signal | GPIO | Direction |
|---|---|---|
| Radar ADC (Bandpass Filter Output) | `34` | Input (Analog) |
| Active Buzzer | `25` | Output (Digital) |
| Status LED | `2` | Output (Digital) |

---

## ⚙ FreeRTOS Task Architecture

The system employs a **preemptive priority-based RTOS scheduler** with four concurrent tasks communicating through FreeRTOS Queues and Semaphores. The Arduino `loop()` is immediately deleted to free CPU cycles for the RTOS kernel.

### Task Overview

| # | Task | Priority | Stack | Function | IPC Mechanism |
|---|---|---|---|---|---|
| **T1** | Radar Acquisition | **4** (Highest) | 2 KB | 250 Hz deterministic ADC sampling via `vTaskDelayUntil()` | Produces → `rawDataQueue` (2048 × `uint16_t`) |
| **T2** | Signal Processing | **3** | 8 KB | 1024-point FFT (DC removal → Hamming window → FFT → Magnitude → Sub-band peak detection) | Consumes `rawDataQueue` → Produces → `processedDataQueue` (8 × `VitalSignData`) |
| **T3** | Detection & Calibration | **2** | 4 KB | Auto-calibration (15 s) + confidence ramp-up/decay algorithm | Consumes `processedDataQueue` → Produces → `dashboardQueue` + `detectionSemaphore` |
| **T4** | Communications & UI | **1** (Lowest) | 8 KB | WebSocket broadcast at ~20 Hz loop, buzzer/LED alarm with 1.5s auto-reset | Consumes `dashboardQueue` / `detectionSemaphore` |

### Inter-Task Data Flow

```
rawDataQueue          processedDataQueue        dashboardQueue
 (2048 slots)           (8 slots)                 (4 slots)
      │                     │                         │
T1 ──▶│──▶ T2 ──────────▶│──▶ T3 ──────────────▶│──▶ T4 ──▶ WebSocket
(250 Hz)   (1024-pt FFT)      (Calibration +          (Broadcast +
                                Confidence)             Buzzer/LED)
                                    │
                                    ▼
                           detectionSemaphore
                              (Binary)
```

---

## 🔬 Signal Processing Pipeline

Each FFT cycle processes **1024 samples** collected over **~4.1 seconds** at 250 Hz, yielding a frequency resolution of **0.2441 Hz/bin**.

### Pipeline Stages

1. **DC Removal** — Strips the ADC bias offset to center the waveform at zero.
2. **Hamming Windowing** — Reduces spectral leakage at bin boundaries.
3. **1024-Point FFT** — Full complex FFT using `float` (not `double`) to leverage the ESP32's hardware single-precision FPU (~30 ms vs ~300 ms on double).
4. **Magnitude Extraction** — `complexToMagnitude()` converts complex bins to real magnitudes.
5. **Dual Sub-Band Peak Detection** — Independent peak search with **parabolic interpolation** for sub-bin frequency accuracy:

| Vital Sign | Frequency Band | FFT Bin Range | Resolution |
|---|---|---|---|
| **Breathing** | 0.20 – 0.60 Hz | Bins 1–2 | ±0.12 Hz |
| **Heartbeat** | 1.00 – 2.50 Hz | Bins 4–10 | ±0.12 Hz |

### Key Design Decisions

- **`float` over `double`**: The ESP32's FPU is single-precision only. Using `double` forces software emulation at ~10× the compute cost — unacceptable for a real-time system.
- **`static` arrays**: The 8 KB FFT buffers (`vReal[1024]`, `vImag[1024]`) are declared `static` to place them in BSS rather than on the task stack, preventing stack overflow.
- **DC-safe bin clamping**: Bin 0 (DC component) is always excluded from peak search, and parabolic interpolation results are clamped to the safe band range to prevent negative-frequency artifacts.

---

## 🎛 Dynamic Auto-Calibration

Instead of a hardcoded noise threshold, the system performs a **15-second environmental calibration** on every boot.

### Calibration Sequence

```
BOOT ──▶ STATE_CALIBRATING (15s) ──▶ STATE_ACTIVE
              │                            │
              │ Track peak FFT magnitude   │ Use dynamic threshold:
              │ across both vital bands    │   activeThreshold =
              │ (no detection logic runs)  │     peakNoise × 1.5
              │                            │
              │ Telemetry still streams    │ Confidence algorithm
              │ to dashboard (state flag)  │ fully operational
              │                            │
              ▼                            ▼
         Dashboard shows             Dashboard shows
         amber CALIBRATION           full tactical view
         overlay with                with live charts
         progress bar
```

| Parameter | Value | Purpose |
|---|---|---|
| `CALIBRATION_DURATION_MS` | 15000 ms | Window for noise floor measurement |
| `SAFETY_MARGIN` | 1.5× | Multiplier applied to peak noise magnitude |
| `FALLBACK_THRESHOLD` | 50.0 | Used if calibration sees zero signal (dead ADC) |
| `REQUIRED_CONFIDENCE` | 5 | Consecutive positive readings needed for alert |

### Non-Blocking Design

The calibration phase uses `xTaskGetTickCount()` comparisons — **no `delay()` calls** — so the Detection Task continues consuming the `processedDataQueue` and feeding telemetry to the dashboard throughout the entire calibration window.

---

## 🖥 Tactical LAN Dashboard

A military-themed, browser-based monitoring console that connects to the ESP32 via **WebSocket on port 81**. No server, no cloud — just open `index.html` from any device on the same LAN.

### Dashboard Features

| Feature | Description |
|---|---|
| **Connection Overlay** | Full-screen animated radar sweep with live reconnect countdown |
| **Calibration Overlay** | Amber warning overlay with progress bar and countdown timer during the 15s noise floor analysis |
| **Threat Assessment** | Real-time status panel — transitions from green `MONITORING` to red-alert `HUMAN DETECTED` with flashing vignette |
| **Confidence Gauge** | 5-bar visual gauge (green → amber → red) with labels: NO SIGNAL → WEAK → TRACKING → ACQUIRING → LOCKED → CONFIRMED |
| **Vital Sign Charts** | Dual Chart.js line graphs — Breathing (0.2–0.6 Hz, cyan) and Heartbeat (1.0–2.5 Hz, magenta) — scrolling 30-point window |
| **System Metrics** | Total alerts, current breathing Hz, current heartbeat Hz, last alert timestamp |
| **Event Log** | Scrolling console with timestamped telemetry, system events, and alerts (max 150 entries) |
| **Audio Alert** | Web Audio API tactical 3-tone square-wave alarm on human detection |
| **Auto-Reconnect** | Automatic WebSocket recovery with visual countdown on connection loss |

### Tech Stack

| Layer | Technology |
|---|---|
| Structure | Semantic HTML5 |
| Styling | Vanilla CSS — glassmorphism, CSS custom properties, CRT scan-line overlay |
| Typography | Orbitron (display) + JetBrains Mono (data) via Google Fonts |
| Charts | Chart.js 4.4.7 (CDN) |
| Data Transport | Native WebSocket API → `ws://<ESP32_IP>:81` |

---

## 📡 Communication Protocol

All telemetry is transmitted exclusively over WebSocket (Serial output is disabled in production firmware to eliminate UART jitter in the time-critical 250 Hz sampling loop).

### $HAWK Protocol

| Message Type | Format | Direction |
|---|---|---|
| **Handshake** | `$HAWK,STATUS,CONNECTED` | ESP32 → Dashboard |
| **Telemetry** | `$HAWK,DATA,<breathFreq>,<heartFreq>,<breathMag>,<heartMag>,<confidence>,<maxConf>,<timestamp>,<state>` | ESP32 → Dashboard |
| **Alert** | `$HAWK,ALERT,HUMAN_DETECTED,TIME:<ms_timestamp>` | ESP32 → Dashboard |

### Telemetry Fields

| Field | Type | Description |
|---|---|---|
| `breathFreq` | `float` | Dominant frequency in breathing band (Hz) |
| `heartFreq` | `float` | Dominant frequency in heartbeat band (Hz) |
| `breathMag` | `float` | FFT magnitude of breathing peak (signal strength) |
| `heartMag` | `float` | FFT magnitude of heartbeat peak (signal strength) |
| `confidence` | `int` | Current detection confidence (0 to `maxConf`) |
| `maxConf` | `int` | Required confidence threshold for alert trigger |
| `timestamp` | `ulong` | ESP32 `millis()` uptime |
| `state` | `string` | `CALIBRATING` or `ACTIVE` |

---

## 📁 Repository Structure

```text
H.A.W.K/
│
├── ESP32/                             # Embedded firmware (Arduino/FreeRTOS)
│   ├── ESP32.ino                      # Entry point — Queue/Semaphore creation, WiFi init, task launch
│   ├── globals.h                      # Pin definitions, SystemState enum, VitalSignData struct, RTOS handles
│   ├── radar_sensor.cpp / .h          # T1: 250 Hz deterministic ADC sampling task
│   ├── signal_processing.cpp / .h     # T2: 1024-pt FFT pipeline with dual sub-band peak detection
│   ├── detection.cpp / .h             # T3: Auto-calibration + confidence algorithm + alert logic
│   └── comms_ui.cpp / .h              # T4: WiFi/WebSocket server, $HAWK protocol, buzzer/LED alarms
│
├── Dashboard/                         # Browser-based tactical monitoring console
│   ├── index.html                     # Semantic HTML5 structure with all overlay layers
│   ├── styles.css                     # Full design system — dark theme, glassmorphism, animations
│   └── app.js                         # WebSocket client, Chart.js graphs, calibration/alert UI logic
│
├── LTspice/                           # Analog circuit simulation files
│   ├── HB100-ESP32.asc                # LTspice schematic — active bandpass filter design
│   └── HB100-ESP32.raw / .log         # Simulation output data
│
├── Oscilloscope/                      # Hardware debug utility
│   └── Oscilloscope.ino               # ESP32 Serial Plotter sketch — verify ADC signal & DC bias
│
├── Abstract.txt                       # Project abstract
├── LICENSE                            # MIT License
└── README.md                          # This file
```

---

## 🚀 Getting Started

### Prerequisites

**Hardware**

| Component | Quantity | Notes |
|---|---|---|
| ESP32 Development Board | 1 | Any variant with ADC-capable GPIO 34 |
| HB100 or CDM324 Radar Module | 1 | 10.525 GHz or 24 GHz Doppler |
| Op-Amp (e.g., LM358 / MCP6002) | 1–2 | For active bandpass filter stages |
| Resistors & Capacitors | As per filter design | See LTspice schematic for values |
| Active Buzzer | 1 | 3.3V compatible |
| LED | 1 | With appropriate current-limiting resistor |

**Software**

| Tool | Version |
|---|---|
| Arduino IDE | 2.x+ (or PlatformIO) |
| ESP32 Board Package | Latest via Board Manager |
| `arduinoFFT` library | v2.0.0+ by Kosme |
| `WebSockets` library | By Markus Sattler (Links2004) |

### Installation

#### 1. Clone the Repository

```bash
git clone https://github.com/chandansaipavanpadala/H.A.W.K-HumanActivityDetectionThroughWallsUsingMicrowaveKinetics.git
cd H.A.W.K-HumanActivityDetectionThroughWallsUsingMicrowaveKinetics
```

#### 2. Hardware Wiring

| Signal | Connect To | ESP32 GPIO |
|---|---|---|
| Bandpass Filter Output | `RADAR_ADC_PIN` | **34** |
| Active Buzzer (+) | `BUZZER_PIN` | **25** |
| Status LED (Anode) | `LED_PIN` | **2** |

> **Tip:** Use the `LTspice/HB100-ESP32.asc` schematic as a reference for the analog filter circuit design.

#### 3. Configure WiFi Credentials

Open `ESP32/comms_ui.h` and update:

```cpp
#define WIFI_SSID     "YourNetworkName"
#define WIFI_PASSWORD "YourPassword"
```

#### 4. Compile and Flash

1. Open `ESP32/ESP32.ino` in Arduino IDE.
2. Select your **ESP32 board model** and **COM port**.
3. Install required libraries (`arduinoFFT`, `WebSockets`) via Library Manager.
4. Compile and upload the firmware.

#### 5. Launch the Dashboard

1. After flashing, note the ESP32's IP address from your router's DHCP table.
2. Open `Dashboard/app.js` and update the WebSocket URL:
   ```javascript
   const WEBSOCKET_URL = 'ws://<YOUR_ESP32_IP>:81';
   ```
3. Open `Dashboard/index.html` in any modern browser on the same LAN.

#### 6. System Validation

1. On boot, the dashboard will show a **cyan radar-sweep loading overlay** while connecting.
2. Once connected, the **amber calibration overlay** appears for **15 seconds** — stand clear of the sensor.
3. After calibration, the full tactical dashboard activates with live charts.
4. Introduce a target subject in front of the radar — monitor the **confidence gauge** ramp from `NO SIGNAL` → `CONFIRMED`.
5. Upon confirmed detection: **RED ALERT** flashes, buzzer sounds, and the `$HAWK,ALERT` packet is broadcast.

---

## 🛠 Development Tools

### Oscilloscope Utility

The `Oscilloscope/Oscilloscope.ino` sketch streams raw ADC values at 250 Hz to the Arduino **Serial Plotter** for hardware debugging:

- Verify the **1.65V DC bias** from the op-amp circuit
- Confirm the ADC signal is centered and not clipping
- Validate the radar module is producing a clean IF signal

```
Tools → Serial Plotter → 115200 baud
```

### LTspice Simulation

The `LTspice/` directory contains the complete active bandpass filter schematic (`HB100-ESP32.asc`). Use [LTspice](https://www.analog.com/en/resources/design-tools-and-calculators/ltspice-simulator.html) to simulate and verify the filter response before building the physical circuit.

---

## 📄 License

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.

**Copyright © 2026 Chandan Sai Pavan Padala**

---

<div align="center">

*Built with ❤️ for the Real-Time Operating Systems course — Amrita Vishwa Vidyapeetham*

**Project H.A.W.K. — Because every second counts when lives are behind walls.**

</div>
