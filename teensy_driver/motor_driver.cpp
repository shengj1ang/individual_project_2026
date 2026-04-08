#include "motor_driver.h"

static const uint8_t PWM_PINS[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
static const uint8_t NUM_PWM = 16;
static const uint32_t DEFAULT_PWM_FREQ = 300;

// Motor pulse task state
struct MotorTask {
  bool active;
  uint8_t amp;
  uint16_t remaining;   // pulses left
  uint32_t on_ms;
  uint32_t off_ms;

  bool state_on;
  uint32_t next_ts;
};

static MotorTask motors[NUM_PWM];

// Init all motor output pins
static void initPins() {
  for (uint8_t i = 0; i < NUM_PWM; i++) {
    pinMode(PWM_PINS[i], OUTPUT);
    digitalWrite(PWM_PINS[i], LOW);
    analogWrite(PWM_PINS[i], 0);
  }
}

// Set default PWM frequency on Teensy
static void initDefaultFrequencies() {
#if defined(TEENSYDUINO)
  for (uint8_t i = 0; i < NUM_PWM; i++) {
    analogWriteFrequency(PWM_PINS[i], (float)DEFAULT_PWM_FREQ);
  }
#endif
}

// Start one pulse task for one motor
static void startTask(uint8_t idx, uint16_t count, uint8_t amp, uint32_t on_ms, uint32_t off_ms) {
  if (idx >= NUM_PWM) return;

  MotorTask &m = motors[idx];

  m.active = true;
  m.amp = amp;
  m.remaining = count;
  m.on_ms = on_ms;
  m.off_ms = off_ms;

  m.state_on = true;
  m.next_ts = millis() + on_ms;

  analogWrite(PWM_PINS[idx], amp);
}

// Public init
void motorInit() {
  initPins();
  initDefaultFrequencies();
  stopAll();
}

// Stop all motors immediately
void stopAll() {
  for (uint8_t i = 0; i < NUM_PWM; i++) {
    motors[i].active = false;
    analogWrite(PWM_PINS[i], 0);
  }
}

// Non-blocking motor scheduler
void updateMotors() {
  uint32_t now = millis();

  for (uint8_t i = 0; i < NUM_PWM; i++) {
    MotorTask &m = motors[i];

    if (!m.active) continue;
    if (now < m.next_ts) continue;

    if (m.state_on) {
      // Turn motor off
      analogWrite(PWM_PINS[i], 0);
      m.state_on = false;
      m.next_ts = now + m.off_ms;
      m.remaining--;

      if (m.remaining == 0) {
        m.active = false;
      }
    } else {
      // Turn motor on again
      analogWrite(PWM_PINS[i], m.amp);
      m.state_on = true;
      m.next_ts = now + m.on_ms;
    }
  }
}

// Parse command: P idx count amp on_ms off_ms
void handlePulse(char* p) {
  long idx, count, amp, on_ms, off_ms;

  if (sscanf(p, "%ld %ld %ld %ld %ld",
             &idx, &count, &amp, &on_ms, &off_ms) == 5) {

    if (idx >= 0 && idx < NUM_PWM &&
        count > 0 &&
        amp >= 0 && amp <= 255) {

      startTask((uint8_t)idx,
                (uint16_t)count,
                (uint8_t)amp,
                (uint32_t)on_ms,
                (uint32_t)off_ms);
    }
  }
}

// Parse command: S mask amp
void handleImmediate(char* p) {
  long mask, amp;
  if (sscanf(p, "%ld %ld", &mask, &amp) == 2) {
    for (uint8_t i = 0; i < NUM_PWM; i++) {
      if (mask & (1 << i)) {
        analogWrite(PWM_PINS[i], amp);
      } else {
        analogWrite(PWM_PINS[i], 0);
      }
      motors[i].active = false;
    }
  }
}