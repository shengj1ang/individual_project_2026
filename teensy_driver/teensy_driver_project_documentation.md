# Teensy Driver Project Documentation

## Overview

This project is a Teensy-based real-time control system for two hardware domains:

1. **Vibration motors** driven through PWM output pins and scheduled asynchronously in firmware.
2. **WS2812 LED strips** driven from dedicated LED data pins and controlled over the same serial protocol.

The design goal is to keep the embedded firmware simple, deterministic, and easy to extend, while exposing a high-level Python interface for host-side control.

This document describes the current project architecture, firmware layout, serial protocol, Python integration, usage patterns, performance considerations, and extension guidelines.

---

## Project Goals

The project is designed around the following requirements:

- Control **10 vibration motors** from a Teensy board.
- Control **two WS2812 LED strips** independently.
- Use a **single serial connection** between Python and Teensy.
- Keep motor control **non-blocking**.
- Keep LED control modular and separate from the motor subsystem.
- Allow **per-pixel RGB and brightness** control for LEDs.
- Preserve the existing motor behavior without changing the motor command model.
- Provide a Python-side API that is easy to integrate into a larger application.

---

## High-Level Architecture

The system has two layers.

### Firmware layer on Teensy

The Teensy firmware handles:

- serial command parsing
- motor task scheduling
- LED framebuffer updates
- LED output refresh

The firmware is organized into separate files:

- `teensy_driver.ino` or `main.ino` as the entry point
- `motor_driver.*` for vibration motor logic
- `LED_array.*` for WS2812 strip logic

### Host layer in Python

Python communicates with the firmware using USB serial.

The host side is intentionally split into separate files/classes:

- existing motor controller, for example `controller.py`
- separate LED controller, for example `led_controller.py`

This separation is important because it preserves backward compatibility and reduces the risk of regressions in the motor subsystem.

---

## Hardware Configuration

### Motor outputs

The firmware currently defines ten motor channels:

- PWM pins: `0, 1, 2, 3, 4, 5, 6, 7, 8, 9`
- PWM frequency: `300 Hz`
- Number of motors: `10`

These motors are controlled using asynchronous pulse scheduling in firmware.

### LED outputs

The LED firmware currently defines two WS2812 strips:

- **Strip 0** on pin `23`
- **Strip 1** on pin `22`

Current default strip lengths are typically:

- `NUM_LEDS_0 = 60`
- `NUM_LEDS_1 = 60`

If the physical strip lengths are different, those constants must be updated in the firmware.

### Recommended electrical notes

For stable WS2812 operation, the following are recommended:

- Place a **330 Ω resistor** in series with each LED data line.
- Add a **large capacitor** (for example 1000 µF) across the LED power rails.
- Ensure the LED power supply ground and Teensy ground are shared.
- Do not power long WS2812 strips directly from the Teensy board.
- Teensy 4.1 uses **3.3 V logic**, so a level shifter may be required for best signal reliability with 5 V LED strips.

---

## Firmware File Structure

A clean project layout can look like this:

```text
teensy_driver/
├── teensy_driver.ino        # or main.ino
├── motor_driver.h
├── motor_driver.cpp
├── LED_array.h
├── LED_array.cpp
```

The important rule is that the project should not contain duplicate legacy `.ino` files with overlapping definitions such as `setup()`, `loop()`, `FW_VERSION`, or serial buffers.

If an older all-in-one firmware file still exists in the same Arduino sketch directory, it will be compiled together with the new files and cause symbol redefinition errors.

---

## Firmware Design

## Main entry file

The main file is responsible for:

- opening the serial port on the Teensy side
- calling initialization functions for the motor and LED subsystems
- polling the non-blocking update functions
- reading complete serial command lines
- dispatching commands to the correct subsystem

Typical responsibilities of the main file:

- `motorInit()`
- `ledInit()`
- `updateMotors()`
- `updateLEDs()`
- command parsing for `E`, `X`, `P`, `S`, `L`, `B`, `C`, and `U`

### Why this split matters

This keeps the firmware modular:

- motor logic remains stable and isolated
- LED logic can be expanded independently
- the serial parser stays centralized

---

## Motor subsystem

The motor subsystem is designed around asynchronous tasks.

Each motor has a `MotorTask` state entry that stores:

- whether the task is active
- PWM amplitude
- remaining pulse count
- on-time in milliseconds
- off-time in milliseconds
- current motor state (on/off)
- next scheduled timestamp

### Main motor concepts

#### `motorInit()`
Initializes motor output pins, applies default PWM frequencies, and performs a safety stop.

#### `stopAll()`
Immediately disables every motor channel and clears active tasks.

#### `startTask(idx, count, amp, on_ms, off_ms)`
Creates a pulse schedule for one motor.

#### `updateMotors()`
The motor scheduler. This function is called every loop iteration and checks whether a motor should switch on or off based on the current `millis()` timestamp.

This is the core reason motor control is non-blocking.

### Why the motor design is robust

- No `delay()` is used in motor timing.
- All pulse behavior is represented as state.
- Once a pulse task is scheduled, the board can continue receiving serial commands.
- Python only sends high-level commands; the firmware handles timing locally.

---

## LED subsystem

The LED subsystem controls two WS2812 strips using FastLED.

### Main LED concepts

#### `ledInit()`
Initializes both strips, sets default global brightness, clears the framebuffer, and sends the initial state.

#### `handleLEDSet()`
Parses a serial command for a single pixel update:

```text
L strip idx r g b brightness
```

This command sets one pixel on one strip.

#### `handleLEDGlobalBrightness()`
Parses:

```text
B brightness
```

This sets global brightness for all strips.

#### `handleLEDClear()`
Parses:

```text
C strip
```

This clears one strip or all strips.

#### `handleLEDShowNow()`
Parses:

```text
U
```

This forces the LED buffer to be flushed immediately.

#### `updateLEDs()`
Checks whether the LED data changed and only calls `FastLED.show()` when the LED state is marked dirty.

### Why the LED design is robust

- LED state is buffered before output.
- LED refreshes are not forced unnecessarily.
- Per-pixel brightness is handled by scaling RGB values before storage.
- The subsystem is separated from the motor logic.

---

## Serial Protocol

The Teensy firmware uses a line-based serial protocol.

Each command is sent as one ASCII line ending in `\n`.

### General commands

#### Echo firmware version

```text
E
```

Response example:

```text
E v2.1.0
```

#### Emergency stop for all motors

```text
X
```

---

## Motor commands

### Schedule a pulse task

```text
P idx count amp on_ms off_ms
```

Example:

```text
P 0 5 180 100 50
```

Meaning:

- motor index `0`
- pulse `5` times
- PWM amplitude `180`
- on time `100 ms`
- off time `50 ms`

### Immediate mask-based output

```text
S mask amp
```

Example:

```text
S 3 200
```

This enables motors whose bits are set in `mask`.

For example, `3` means binary `0000000011`, which targets motors `0` and `1`.

---

## LED commands

### Set one pixel

```text
L strip idx r g b brightness
```

Example:

```text
L 0 5 255 0 0 128
```

Meaning:

- strip `0`
- pixel `5`
- red = `255`
- green = `0`
- blue = `0`
- brightness = `128`

### Set global brightness

```text
B brightness
```

Example:

```text
B 64
```

### Clear one strip or all strips

```text
C strip
```

Examples:

```text
C 0
C 1
C -1
```

### Force LED output immediately

```text
U
```

---

## Python Integration

The Python side is intentionally split into two controllers.

### Existing motor controller

The existing motor controller file contains a class like:

```python
class VibratorController:
    ...
```

This class is responsible for:

- opening the serial connection
- sending raw serial commands
- reading echo responses
- scheduling pulse commands
- stopping all motors

This controller should remain unchanged to preserve existing behavior.

### Separate LED controller

The LED functionality should live in a separate file, for example:

- `led_controller.py`

This class is responsible only for LED operations and does not modify the existing motor controller.

---

## LED Python Controller Design

The recommended LED class is `LEDArrayController`.

### Main responsibilities

- connect to the serial device
- validate strip indices and pixel indices
- validate color and brightness values
- send LED commands safely
- support batch updates
- minimize unnecessary LED refreshes

### Key methods

#### `connect()`
Open the serial connection.

#### `close()`
Close the serial connection cleanly.

#### `send(cmd)`
Send one raw command line.

#### `echo()`
Query the firmware version string.

#### `set_pixel(strip, idx, r, g, b, brightness=255)`
Set one pixel.

#### `set_pixels(pixels)`
Set multiple pixels.

#### `fill_strip(strip, color, brightness=255, start=0, end=None)`
Fill an entire strip or a range.

#### `set_global_brightness(brightness)`
Set brightness for all strips.

#### `clear(strip=-1)`
Clear one strip or all strips.

#### `off(strip=-1)`
Alias for `clear()`.

#### `show()`
Force immediate LED output.

#### `batch(show=True)`
Context manager that batches writes and optionally flushes once at the end.

### Why batching matters

With WS2812 LEDs, output refreshes can become expensive if performed too often. Batch updates provide a safer and more scalable way to update many pixels:

```python
with led.batch(show=True):
    led.set_pixel(0, 0, 255, 0, 0, 128)
    led.set_pixel(0, 1, 0, 255, 0, 128)
    led.set_pixel(1, 0, 0, 0, 255, 128)
```

This is preferable to forcing a refresh after every single pixel write.

---

## Example Python Usage

## Motor only

```python
from controller import VibratorController

with VibratorController() as vib:
    print(vib.echo())
    vib.pulse(0, count=3, amp=120, on_ms=100, off_ms=100)
    vib.pulse_many([1, 2, 3], count=2, amp=90, on_ms=80, off_ms=80)
```

## LED only

```python
from led_controller import LEDArrayController

with LEDArrayController() as led:
    print(led.echo())
    led.set_global_brightness(80)
    led.set_pixel(0, 0, 255, 0, 0, 128)
    led.set_pixel(1, 5, 0, 255, 0, 100)
```

## LED batch update

```python
from led_controller import LEDArrayController

with LEDArrayController(auto_show=False) as led:
    with led.batch(show=True):
        for i in range(10):
            led.set_pixel(0, i, 255, 0, 0, 100)
            led.set_pixel(1, i, 0, 0, 255, 100)
```

---

## Using Motor and LED Together

Both motor and LED commands are sent to the same Teensy serial device.

### Important design fact

This does **not** mean the motor subsystem and LED subsystem are fighting for different serial ports. There is one serial command stream, and the Teensy command parser dispatches each command to the correct subsystem.

### What can go wrong in practice

The main risks are:

1. Python sends commands too quickly.
2. Multiple Python threads write to the same serial object at the same time.
3. LED refreshes are forced too frequently.

### Best practice

- Use a single serial writer per board.
- If multiple components send commands, protect writes with a lock.
- Use batch LED updates.
- Use motor pulse scheduling instead of extremely high-frequency direct writes.

---

## Concurrency and Timing Considerations

## Firmware side

The firmware is structured to remain responsive:

- `updateMotors()` is non-blocking.
- serial reads are line-based and non-blocking.
- LED updates are deferred until needed.

However, `FastLED.show()` still consumes CPU time because WS2812 timing is strict.

### Practical implication

If LED strips become long or refreshes become frequent, the firmware may spend more time outputting LED data. This does not create a serial ownership problem, but it can increase command latency.

For the current project scale, this is usually acceptable.

## Python side

The Python LED controller uses an `RLock` to make writes safer if called from threaded contexts.

That does not eliminate every application-level race condition, but it greatly reduces the risk of interleaved command strings.

---

## Extension Guidelines

## Extending the motor subsystem

The current motor design can be extended by increasing:

```cpp
static const uint8_t PWM_PINS[] = {...};
static const uint8_t NUM_PWM = ...;
```

The scheduler logic already scales with `NUM_PWM`.

### Important caveats

Motor expansion is not only a software question. You must verify:

- available PWM-capable pins on the Teensy
- external driver capacity
- power supply current capacity
- ground integrity
- thermal behavior under simultaneous load

In most real builds, hardware limits are reached before scheduler logic becomes the limiting factor.

## Extending the LED subsystem

You can extend LEDs by:

- increasing strip lengths
- adding more helper methods on the Python side
- adding more strip definitions in firmware

### Important caveats

As LED count grows:

- `FastLED.show()` takes longer
- refresh frequency becomes more important
- host command batching becomes more valuable

---

## Validation and Troubleshooting

## Basic firmware communication test

Use the echo command first:

```python
with LEDArrayController() as led:
    print(led.echo())
```

or:

```python
with VibratorController() as vib:
    print(vib.echo())
```

If echo does not respond correctly:

- verify USB connection
- verify board selection in Arduino IDE
- verify baud rate is `115200`
- verify the correct serial port is selected or detected

## LED test checklist

If LEDs do not light correctly:

- verify pin `23` and pin `22`
- verify strip direction (`DIN`, not `DOUT`)
- verify common ground
- verify power supply capacity
- verify strip lengths in firmware match reality
- verify the board is actually running the new firmware

## Motor test checklist

If motors do not respond correctly:

- verify motor driver hardware
- verify PWM pins and driver wiring
- verify amplitude range is reasonable
- verify motor index is within range
- verify the `X` safety stop is not being sent unexpectedly

## Compilation problems

If you see redefinition errors such as:

- `setup() redefinition`
- `loop() redefinition`
- `FW_VERSION redefined`
- duplicate motor arrays or serial buffers

then the sketch directory still contains old `.ino` files that are being compiled together. Remove or rename old files so only the intended source files remain.

---

## Recommended Development Workflow

1. Keep the motor firmware stable and unchanged unless needed.
2. Implement LED features only in the LED module.
3. Keep Python motor and LED controllers in separate files.
4. Validate the serial protocol with simple echo tests.
5. Use small hardware tests first:
   - one motor
   - one LED pixel
6. Use batch updates for larger LED patterns.
7. Scale hardware slowly and verify power integrity at each step.

---

## Suggested Future Improvements

Possible future directions include:

- unified Python transport layer for one shared serial connection
- structured command acknowledgements from firmware
- optional status or error responses
- animation helpers on the Python side
- DMA-based LED output if LED count grows substantially
- configuration files for motor count, pin assignments, and strip lengths
- test harnesses for validating command protocol behavior automatically

---

## Summary

This project already has a solid foundation:

- motor control is asynchronous and stable
- LED control is modular and isolated
- the firmware uses a clear serial command protocol
- the Python side is split cleanly between motor and LED responsibilities

The architecture is practical for real hardware work because it keeps the time-critical logic on the Teensy while exposing a simple host-side API.

With the current structure, the project is easy to maintain, easy to extend, and well-suited for larger interactive systems that combine haptics and addressable lighting.

