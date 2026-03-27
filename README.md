# Overview

This project provides a simple multi-channel vibration controller using an Arduino-compatible board and a Python interface.

It allows controlling up to 10 PWM outputs via a serial connection, using a minimal text-based protocol.

The system is designed for:

	•	Quick testing of vibration motors
    
	•	Simple pattern playback
    
	•	Reliable and deterministic control


# Architecture
The system consists of two main parts:

	•	Arduino Firmware → handles PWM output and command parsing
    
	•	Python Controller → sends commands and provides higher-level control
    
  
# Arduino Firmware
The Arduino firmware implements a simple serial command parser and a stateless output model.

All PWM outputs are controlled using a bitmask, where each bit represents one motor channel.

Key Design

	•	All PWM pins are initialized as outputs at startup to ensure a stable state
    
	•	Default PWM frequency is set to 300 Hz (if supported by the platform)
    
	•	Every command fully overwrites the current output state
    
	•	No internal scheduling or task queue is used

This keeps the firmware:

	•	deterministic
    
	•	easy to debug
    
	•	resistant to partial command loss

## Supported Commands

### Stop all outputs
```
X
```
### Set outputs
```
S <mask> <amp>
```

	•	mask: bitmask (0–1023 for 10 channels)
    
	•	amp: PWM value (0–255)
    
   Example: enables channels 0,1,2
```
S 7 80
```

### Set PWM frequency (optional, platform dependent)
```
F f0 f1 f2 f3 f4 f5 f6 f7 f8 f9
```
### Echo firmware version
```
E
```
Response:
```
E v1.0.1
```

# Python Controller

The Python side provides a higher-level interface for interacting with the device.

Features

	•	Automatic serial port detection
    
	•	Simple command wrapper
    
	•	Motor-by-motor testing
    
	•	Pattern playback utilities

Example Usage

```
from vibrator import VibratorController

with VibratorController() as vc:
    print(vc.echo())

    vc.set_all_freqs([300] * 10)

    # single motor
    vc.pulse_motor(0, amp=80, on_ms=120)

    # multiple motors
    vc.pulse_mask(1023, amp=80, on_ms=150)

    # test all motors
    vc.test_all_one_by_one()
```

# Design Notes

This version uses a stateless control model:

Each S command replaces the entire output state.

This means:

	•	Commands are simple and predictable
    
	•	There is no internal task queue
    
	•	Complex overlapping patterns must be handled on the Python side

This is intentional for:

	•	robustness
    
	•	simplicity
    
	•	easier debugging

⸻

Limitations

	•	No independent per-motor scheduling
    
	•	No queued vibration patterns
    
	•	New commands override previous states

Future versions may introduce:

	•	per-channel task scheduling
    
	•	asynchronous pattern execution
    
	•	additive control modes

⸻

# Hardware Notes

If using pins 0 and 1, be aware that they may conflict with the hardware serial interface on some boards.

For best stability, consider using higher-numbered GPIO pins.

Also ensure your power supply can handle multiple motors running simultaneously.


