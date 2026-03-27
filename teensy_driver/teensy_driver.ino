#include <Arduino.h>

#define FW_VERSION "v2.0.0"

static const uint8_t PWM_PINS[10] = {0,1,2,3,4,5,6,7,8,9};
static const uint8_t NUM_PWM = 10;

static const uint32_t DEFAULT_PWM_FREQ = 300;

// ===== motor task state =====
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

// ===== serial buffer =====
static char lineBuf[128];
static uint8_t lineLen = 0;
static bool overflow = false;


// ===== init pins =====
static void initPins() {
  for (uint8_t i = 0; i < NUM_PWM; i++) {
    pinMode(PWM_PINS[i], OUTPUT);
    digitalWrite(PWM_PINS[i], LOW);
    analogWrite(PWM_PINS[i], 0);
  }
}

// ===== default freq =====
static void initDefaultFrequencies() {
  #if defined(TEENSYDUINO)
    for (uint8_t i = 0; i < NUM_PWM; i++) {
      analogWriteFrequency(PWM_PINS[i], (float)DEFAULT_PWM_FREQ);
    }
  #endif
}

// ===== stop all =====
static void stopAll() {
  for (uint8_t i = 0; i < NUM_PWM; i++) {
    motors[i].active = false;
    analogWrite(PWM_PINS[i], 0);
  }
}

// ===== schedule motor task =====
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

// ===== scheduler =====
static void updateMotors() {
  uint32_t now = millis();

  for (uint8_t i = 0; i < NUM_PWM; i++) {
    MotorTask &m = motors[i];

    if (!m.active) continue;

    if (now < m.next_ts) continue;

    if (m.state_on) {
      // turn off
      analogWrite(PWM_PINS[i], 0);
      m.state_on = false;
      m.next_ts = now + m.off_ms;
      m.remaining--;

      if (m.remaining == 0) {
        m.active = false;
      }
    } else {
      // turn on again
      analogWrite(PWM_PINS[i], m.amp);
      m.state_on = true;
      m.next_ts = now + m.on_ms;
    }
  }
}

// ===== read line =====
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

static char* skipSpaces(char* p) {
  while (*p == ' ' || *p == '\t') p++;
  return p;
}

// ===== commands =====

static void handleEcho() {
  Serial.print("E ");
  Serial.println(FW_VERSION);
}

// P idx count amp on off
static void handlePulse(char* p) {
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

// optional: keep old S
static void handleImmediate(char* p) {
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

void setup() {
  Serial.begin(115200);

  initPins();
  initDefaultFrequencies();
  stopAll();
}

void loop() {
  updateMotors();  

  if (!readLine()) return;

  char* p = skipSpaces(lineBuf);
  if (!*p) return;

  char cmd = *p++;
  p = skipSpaces(p);

  if (cmd == 'X') {
    stopAll();
    return;
  }

  if (cmd == 'E') {
    handleEcho();
    return;
  }

  if (cmd == 'P') {
    handlePulse(p);
    return;
  }

  if (cmd == 'S') {
    handleImmediate(p);
    return;
  }
}