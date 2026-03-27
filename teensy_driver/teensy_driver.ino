#include <Arduino.h>

#define FW_VERSION "v1.0.1"

static const uint8_t PWM_PINS[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
static const uint8_t NUM_PWM = sizeof(PWM_PINS) / sizeof(PWM_PINS[0]);

static const uint16_t ALL_MASK = (1u << NUM_PWM) - 1u;
static const uint32_t DEFAULT_PWM_FREQ = 300;

static char lineBuf[128];
static uint8_t lineLen = 0;
static bool overflow = false;

static uint16_t currentMask = 0;
static uint8_t currentAmp = 0;

// Apply a full output mask atomically
static inline void applyMask(uint16_t mask, uint8_t amp) {
  mask &= ALL_MASK;
  for (uint8_t i = 0; i < NUM_PWM; i++) {
    analogWrite(PWM_PINS[i], (mask & (1u << i)) ? amp : 0);
  }
  currentMask = mask;
  currentAmp = amp;
}

// Stop all outputs
static inline void stopAll() {
  applyMask(0, 0);
}

// Initialize all PWM pins to a safe output-low state
static void initPins() {
  for (uint8_t i = 0; i < NUM_PWM; i++) {
    pinMode(PWM_PINS[i], OUTPUT);
    digitalWrite(PWM_PINS[i], LOW);
    analogWrite(PWM_PINS[i], 0);
  }
}

// Set default PWM frequency on all channels
static void initDefaultFrequencies() {
  #if defined(TEENSYDUINO)
    for (uint8_t i = 0; i < NUM_PWM; i++) {
      analogWriteFrequency(PWM_PINS[i], (float)DEFAULT_PWM_FREQ);
    }
  #endif
}

// Read one full line from serial
static bool readLine() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    if (c == '\r') continue;

    if (c == '\n') {
      if (overflow) {
        overflow = false;
        lineLen = 0;
        return false;
      }
      lineBuf[lineLen] = '\0';
      lineLen = 0;
      return true;
    }

    if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = c;
    } else {
      overflow = true;
    }
  }
  return false;
}

// Skip leading spaces/tabs
static inline char* skipSpaces(char* p) {
  while (*p == ' ' || *p == '\t') p++;
  return p;
}

// Handle command: X
static void handleStop() {
  stopAll();
}

// Handle command: S mask amp
static void handleStart(char* p) {
  long mask, amp;
  if (sscanf(p, "%ld %ld", &mask, &amp) == 2) {
    if (mask >= 0 && mask <= (long)ALL_MASK && amp >= 0 && amp <= 255) {
      applyMask((uint16_t)mask, (uint8_t)amp);
    }
  }
}

// Handle command: F f0 f1 ... f9
static void handleFreq(char* p) {
  long f[10];
  if (sscanf(p, "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld",
             &f[0], &f[1], &f[2], &f[3], &f[4],
             &f[5], &f[6], &f[7], &f[8], &f[9]) == 10) {
    #if defined(TEENSYDUINO)
      for (uint8_t i = 0; i < NUM_PWM; i++) {
        if (f[i] > 0) {
          analogWriteFrequency(PWM_PINS[i], (float)f[i]);
        }
      }
    #endif
  }
}

// Handle command: E
static void handleEcho() {
  Serial.print("E ");
  Serial.println(FW_VERSION);
}

void setup() {
  Serial.begin(115200);

  initPins();
  initDefaultFrequencies();
  stopAll();
}

void loop() {
  if (!readLine()) return;

  char* p = skipSpaces(lineBuf);
  if (!*p) return;

  char cmd = *p++;
  p = skipSpaces(p);

  if (cmd == 'X') {
    handleStop();
    return;
  }

  if (cmd == 'S') {
    handleStart(p);
    return;
  }

  if (cmd == 'F') {
    handleFreq(p);
    return;
  }

  if (cmd == 'E') {
    handleEcho();
    return;
  }

  // Unknown command: ignore
}