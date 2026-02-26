#include <Arduino.h>

static const uint8_t PWM_PINS[3] = {2, 4, 6};
static const uint8_t DIR_PINS[3] = {3, 5, 255}; // motor3 no dir in your snippet

static char lineBuf[64];
static uint8_t lineLen = 0;
static bool overflow = false;

static inline void applyMask(uint8_t mask, uint8_t amp) {
  for (uint8_t i = 0; i < 3; i++) {
    if (mask & (1u << i)) analogWrite(PWM_PINS[i], amp);
    else analogWrite(PWM_PINS[i], 0);
  }
}

static inline void stopAll() { applyMask(0, 0); }

// Returns true when a full line is ready in lineBuf (null-terminated)
static bool readLine() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') continue;

    if (c == '\n') {
      if (overflow) { overflow = false; lineLen = 0; return false; }
      lineBuf[lineLen] = '\0';
      lineLen = 0;
      return true;
    }

    if (lineLen < sizeof(lineBuf) - 1) lineBuf[lineLen++] = c;
    else overflow = true; // discard until newline
  }
  return false;
}

void setup() {
  Serial.begin(115200);

  for (uint8_t i = 0; i < 3; i++) {
    pinMode(PWM_PINS[i], OUTPUT);
    analogWrite(PWM_PINS[i], 0);
  }
  if (DIR_PINS[0] != 255) { pinMode(DIR_PINS[0], OUTPUT); digitalWrite(DIR_PINS[0], HIGH); }
  if (DIR_PINS[1] != 255) { pinMode(DIR_PINS[1], OUTPUT); digitalWrite(DIR_PINS[1], HIGH); }

  stopAll();
}

void loop() {
  if (!readLine()) return;

  // skip spaces
  char* p = lineBuf;
  while (*p == ' ' || *p == '\t') p++;
  if (!*p) return;

  char cmd = *p++;

  if (cmd == 'X') {            // stop
    stopAll();
    return;
  }

  if (cmd == 'S') {            // start: S mask amp
    long mask, amp;
    if (sscanf(p, "%ld %ld", &mask, &amp) == 2) {
      if (mask >= 0 && mask <= 7 && amp >= 0 && amp <= 255) {
        applyMask((uint8_t)mask, (uint8_t)amp);
      }
    }
    return;
  }

  if (cmd == 'F') {            // freq: F f1 f2 f3 (best effort)
    long f1, f2, f3;
    if (sscanf(p, "%ld %ld %ld", &f1, &f2, &f3) == 3) {
      if (f1 > 0 && f2 > 0 && f3 > 0) {
        #if defined(TEENSYDUINO)
          analogWriteFrequency(PWM_PINS[0], (float)f1);
          analogWriteFrequency(PWM_PINS[1], (float)f2);
          analogWriteFrequency(PWM_PINS[2], (float)f3);
        #endif
      }
    }
    return;
  }

  // unknown cmd: ignore silently
}