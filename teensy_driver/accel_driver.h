#ifndef ACCEL_DRIVER_H
#define ACCEL_DRIVER_H

#include <Arduino.h>

void accelInit();
void updateAccelerometer();

// Serial command handler
// A HELP                  -> print command help
// A WHOAMI                -> print WHO_AM_I register
// A READ                  -> print one sample immediately
// A START [interval_ms]   -> start non-blocking streaming (default 10 ms)
// A STOP                  -> stop streaming
// A RATE interval_ms      -> set streaming interval without starting/stopping
void handleAccelCommand(char* p);

#endif
