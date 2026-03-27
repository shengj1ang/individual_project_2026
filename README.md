# Multi-Channel Vibration Control System

This project implements a complete system for controlling multiple vibration motors using an Arduino-compatible board (e.g. Teensy) and Python.

It includes:
- asynchronous multi-motor control firmware
- Python control interface
- real-time keyboard control
- audio-based latency measurement
- automatic data analysis and visualization

---

## Project Structure
```

root/
├── teensy_driver/       # Arduino / Teensy firmware
├── python-code/         # Python control and analysis tools
├── README.md


```

---

## Arduino Firmware (teensy_driver)

The firmware runs on the microcontroller and handles:

- PWM output for up to 10 motors  
- non-blocking scheduling (each motor runs independently)  
- serial command parsing  

### Key Design

- Each motor has its own state machine  
- No blocking delay is used  
- Multiple motors can run different patterns simultaneously  

### Supported Commands
```
X
→ stop all motors

E
→ return firmware version

P    <on_ms> <off_ms>
→ run pulse task on one motor

S  
→ immediate override (debug / manual control)
```
---

## Python Code (python-code)

This folder contains all control, testing, and analysis scripts.

---

## File Overview

### controller.py

High-level interface for communicating with the device.

Provides:
- connection handling
- command sending
- pulse control
- async motor triggering

---

### serial_utils.py

Handles serial port detection and initialization.

Features:
- automatic port selection (macOS / Linux / Windows)
- fallback to manual selection when needed

---

### demo_async.py

Demonstrates asynchronous motor control.

- starts one motor
- injects additional motors while it is still running
- shows non-blocking behavior

---

### demo_keyboard_control.py

Real-time keyboard control.

- maps keys (A–;) to motors (0–9)
- supports multiple simultaneous key presses
- uses `S mask` for real-time control

---

### detect_live_motor.py

Real-time microphone-based detection tool.

- continuously monitors audio input
- detects vibration presence
- helps tune detection parameters (threshold, frequency band)

---

### measure_latency.py

Single-motor latency measurement.

- triggers motor once
- detects acoustic onset
- estimates end-to-end latency

---

### measure_latency_multi_motor.py

Advanced latency measurement (multi-motor version).

Features:
- random motor selection (configurable)
- multiple runs (statistical analysis)
- per-motor latency comparison
- automatic plotting and saving

---

## Latency Measurement

Latency is estimated using:

1. Send command via serial  
2. Motor starts vibrating  
3. Microphone detects vibration sound  
4. Compute time difference  

This includes:
- serial transmission delay  
- MCU execution time  
- motor response time  
- acoustic propagation  
- audio capture latency  

---

## Output (latency_results)

Running the measurement script generates:
```
latency_results/
├── latency_by_run.png
├── latency_grouped_by_motor.png
├── latency_histogram.png
├── summary.txt
```
---

## Configuration

In `measure_latency_multi_motor.py`:

```python
TEST_MOTORS = [0, 2, 6]
NUM_RUNS = 20

You can modify:
	•	which motors to test
	•	number of runs
	•	detection parameters

⸻

Notes
	•	Using pins 0 and 1 may conflict with serial on some boards
	•	Ensure power supply is sufficient for multiple motors
	•	Microphone positioning significantly affects measurement accuracy
Version

Current version includes:
	•	async firmware
	•	Python control layer
	•	measurement + visualization pipeline
