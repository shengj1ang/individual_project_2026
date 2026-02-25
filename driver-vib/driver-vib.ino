/*
  Vibration Motor Driver over Serial (non-blocking)
  - Supports motors 1..3 on PWM pins
  - Patterns: n pulses with on/off, plus optional gap
  - Command protocol: text lines "KEY=VALUE" style
  - ACK/ERR responses, optional id=xxx

  NOTE:
  - analogWriteFrequency() exists on Teensy and some cores.
    If not available, frequency calls are compiled out.
*/

#include <Arduino.h>

// ------------------- Pin mapping -------------------
static const uint8_t PWM_PINS[3] = {2, 4, 6};
static const uint8_t DIR_PINS[3] = {3, 5, 255}; // motor3 has no dir in your snippet

// Default PWM frequencies (Hz) from your example
static const uint16_t PWM_FREQ[3] = {300, 240, 240};

// ------------------- Serial settings -------------------
static const uint32_t BAUD = 115200;
static const uint16_t LINE_MAX = 160;

// ------------------- Job & state machine -------------------
enum StepState : uint8_t { STEP_IDLE = 0, STEP_ON = 1, STEP_OFF = 2, STEP_GAP = 3 };

struct VibJob {
  uint8_t mask;        // bit0->motor1, bit1->motor2, bit2->motor3
  uint8_t amp;         // 0..255
  uint8_t n;           // pulses count
  uint16_t on_ms;      // on duration
  uint16_t off_ms;     // off between pulses
  uint16_t gap_ms;     // gap after full pattern
};

static volatile bool running = false;
static VibJob current;
static StepState stepState = STEP_IDLE;

static uint8_t pulsesLeft = 0;
static uint32_t tNext = 0;

// simple queue (optional)
static const uint8_t QCAP = 8;
static VibJob queueBuf[QCAP];
static uint8_t qHead = 0, qTail = 0, qCount = 0;

// ------------------- Helpers -------------------
static inline void setMotor(uint8_t idx, uint8_t amp) {
  analogWrite(PWM_PINS[idx], amp);
}

static void applyMask(uint8_t mask, uint8_t amp) {
  for (uint8_t i = 0; i < 3; i++) {
    if (mask & (1u << i)) setMotor(i, amp);
    else setMotor(i, 0);
  }
}

static void stopAll() {
  applyMask(0, 0);
  running = false;
  stepState = STEP_IDLE;
  pulsesLeft = 0;
  tNext = 0;
}

static bool qPush(const VibJob &j) {
  if (qCount >= QCAP) return false;
  queueBuf[qTail] = j;
  qTail = (uint8_t)((qTail + 1) % QCAP);
  qCount++;
  return true;
}

static bool qPop(VibJob &out) {
  if (qCount == 0) return false;
  out = queueBuf[qHead];
  qHead = (uint8_t)((qHead + 1) % QCAP);
  qCount--;
  return true;
}

static void startJob(const VibJob &j) {
  current = j;
  running = true;
  pulsesLeft = current.n;
  stepState = STEP_ON;
  applyMask(current.mask, current.amp);
  tNext = millis() + current.on_ms;
}

// Called frequently in loop()
static void tickVibration() {
  if (!running) {
    // if idle and queue has jobs, start next
    VibJob j;
    if (qPop(j)) startJob(j);
    return;
  }

  uint32_t now = millis();
  // handle millis wrap safely:
  if ((int32_t)(now - tNext) < 0) return; // not yet

  switch (stepState) {
    case STEP_ON:
      // finished ON -> go OFF
      applyMask(current.mask, 0);
      stepState = STEP_OFF;
      tNext = now + current.off_ms;
      break;

    case STEP_OFF:
      if (pulsesLeft > 0) pulsesLeft--;
      if (pulsesLeft > 0) {
        // more pulses -> ON again
        applyMask(current.mask, current.amp);
        stepState = STEP_ON;
        tNext = now + current.on_ms;
      } else {
        // done pulses -> GAP or IDLE
        if (current.gap_ms > 0) {
          stepState = STEP_GAP;
          tNext = now + current.gap_ms;
        } else {
          running = false;
          stepState = STEP_IDLE;
        }
      }
      break;

    case STEP_GAP:
      running = false;
      stepState = STEP_IDLE;
      break;

    default:
      running = false;
      stepState = STEP_IDLE;
      break;
  }
}

// ------------------- Line reader -------------------
static char lineBuf[LINE_MAX];
static uint16_t lineLen = 0;
static bool lineOverflow = false;

static bool readLine(char *out, uint16_t outMax) {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    if (c == '\r') continue; // ignore CR

    if (c == '\n') {
      if (lineOverflow) {
        // discard this line
        lineOverflow = false;
        lineLen = 0;
        return false;
      }
      // terminate and return
      if (lineLen >= outMax) lineLen = outMax - 1;
      out[lineLen] = '\0';
      lineLen = 0;
      return true;
    }

    if (lineLen < outMax - 1) {
      out[lineLen++] = c;
    } else {
      lineOverflow = true; // too long; discard until newline
    }
  }
  return false;
}

// ------------------- Minimal parser -------------------
// tokens: split by spaces. key=value.
static const char* skipSpaces(const char* s) {
  while (*s == ' ' || *s == '\t') s++;
  return s;
}

static bool streq(const char* a, const char* b) {
  while (*a && *b) {
    if (*a != *b) return false;
    a++; b++;
  }
  return (*a == '\0' && *b == '\0');
}

static int parseInt(const char* s, bool &ok) {
  ok = false;
  if (!s || !*s) return 0;
  char *endp = nullptr;
  long v = strtol(s, &endp, 10);
  if (endp == s) return 0;
  ok = true;
  return (int)v;
}

static uint8_t parseMask(const char* val, bool &ok) {
  ok = false;
  if (!val) return 0;
  if (streq(val, "all") || streq(val, "ALL")) {
    ok = true;
    return 0b111;
  }
  // parse comma separated: "1,3"
  uint8_t mask = 0;
  const char* p = val;
  bool any = false;
  while (*p) {
    // read number
    char tmp[8];
    uint8_t ti = 0;
    while (*p && *p != ',' && ti < sizeof(tmp) - 1) {
      tmp[ti++] = *p++;
    }
    tmp[ti] = '\0';
    bool okn;
    int m = parseInt(tmp, okn);
    if (!okn || m < 1 || m > 3) return 0;
    mask |= (1u << (m - 1));
    any = true;
    if (*p == ',') p++;
  }
  if (!any) return 0;
  ok = true;
  return mask;
}

static void sendERR(const char* idStr, const char* code, const char* msg) {
  Serial.print("ERR");
  if (idStr && *idStr) { Serial.print(" id="); Serial.print(idStr); }
  Serial.print(" code="); Serial.print(code);
  Serial.print(" msg="); Serial.println(msg);
}

static void sendACK(const char* idStr, const char* extra = nullptr) {
  Serial.print("ACK");
  if (idStr && *idStr) { Serial.print(" id="); Serial.print(idStr); }
  if (extra && *extra) { Serial.print(" "); Serial.print(extra); }
  Serial.println();
}

// ------------------- Command handling -------------------
static void handleCommand(const char* line) {
  // copy to mutable buffer
  char buf[LINE_MAX];
  strncpy(buf, line, LINE_MAX);
  buf[LINE_MAX - 1] = '\0';

  // tokenize by spaces
  char *saveptr = nullptr;
  char *tok = strtok_r(buf, " \t", &saveptr);
  if (!tok) return;

  // command word
  const char* cmd = tok;

  // defaults
  char idStr[16] = {0};

  // For VIB defaults
  VibJob j;
  j.mask = 0;
  j.amp = 80;
  j.n = 1;
  j.on_ms = 100;
  j.off_ms = 100;
  j.gap_ms = 0;

  enum { MODE_OVR = 0, MODE_Q = 1 } mode = MODE_OVR;

  // parse key=val tokens
  while ((tok = strtok_r(nullptr, " \t", &saveptr))) {
    char *eq = strchr(tok, '=');
    if (!eq) continue;
    *eq = '\0';
    const char* key = tok;
    const char* val = eq + 1;

    if (streq(key, "id")) {
      strncpy(idStr, val, sizeof(idStr) - 1);
      idStr[sizeof(idStr) - 1] = '\0';
    } else if (streq(cmd, "VIB")) {
      if (streq(key, "m")) {
        bool okm;
        j.mask = parseMask(val, okm);
        if (!okm) { sendERR(idStr, "BAD_ARG", "m must be 1..3 list or all"); return; }
      } else if (streq(key, "amp")) {
        bool ok;
        int v = parseInt(val, ok);
        if (!ok || v < 0 || v > 255) { sendERR(idStr, "BAD_ARG", "amp 0..255"); return; }
        j.amp = (uint8_t)v;
      } else if (streq(key, "n")) {
        bool ok;
        int v = parseInt(val, ok);
        if (!ok || v < 1 || v > 20) { sendERR(idStr, "BAD_ARG", "n 1..20"); return; }
        j.n = (uint8_t)v;
      } else if (streq(key, "on")) {
        bool ok;
        int v = parseInt(val, ok);
        if (!ok || v < 1 || v > 60000) { sendERR(idStr, "BAD_ARG", "on 1..60000ms"); return; }
        j.on_ms = (uint16_t)v;
      } else if (streq(key, "off")) {
        bool ok;
        int v = parseInt(val, ok);
        if (!ok || v < 0 || v > 60000) { sendERR(idStr, "BAD_ARG", "off 0..60000ms"); return; }
        j.off_ms = (uint16_t)v;
      } else if (streq(key, "gap")) {
        bool ok;
        int v = parseInt(val, ok);
        if (!ok || v < 0 || v > 60000) { sendERR(idStr, "BAD_ARG", "gap 0..60000ms"); return; }
        j.gap_ms = (uint16_t)v;
      } else if (streq(key, "mode")) {
        if (streq(val, "OVR") || streq(val, "ovr")) mode = MODE_OVR;
        else if (streq(val, "Q") || streq(val, "q")) mode = MODE_Q;
        else { sendERR(idStr, "BAD_ARG", "mode must be OVR or Q"); return; }
      }
    }
  }

  // Dispatch commands
  if (streq(cmd, "STOP")) {
    stopAll();
    // also clear queue
    qHead = qTail = qCount = 0;
    sendACK(idStr, "state=IDLE");
    return;
  }

  if (streq(cmd, "STATUS")) {
    char extra[80];
    // show mask in binary-like
    snprintf(extra, sizeof(extra),
             "state=%s mask=0b%c%c%c step=%u left=%u q=%u",
             running ? "RUN" : "IDLE",
             (current.mask & 0b100) ? '1' : '0',
             (current.mask & 0b010) ? '1' : '0',
             (current.mask & 0b001) ? '1' : '0',
             (unsigned)stepState,
             (unsigned)pulsesLeft,
             (unsigned)qCount);
    sendACK(idStr, extra);
    return;
  }

  if (streq(cmd, "VIB")) {
    if (j.mask == 0) { sendERR(idStr, "BAD_ARG", "missing m=..."); return; }
    if (j.amp == 0) { sendERR(idStr, "BAD_ARG", "amp=0 means no vibration"); return; }

    if (mode == MODE_OVR) {
      // override immediately: stop current but keep it clean
      stopAll();
      // do not clear queue by default; but for "override" usually you'd want to clear:
      qHead = qTail = qCount = 0;
      startJob(j);
      sendACK(idStr);
      return;
    } else {
      // queue
      if (!qPush(j)) { sendERR(idStr, "BUSY", "queue full"); return; }
      sendACK(idStr);
      return;
    }
  }

  // Unknown command
  sendERR(idStr, "UNKNOWN", "cmd must be VIB/STOP/STATUS");
}

// ------------------- Setup/Loop -------------------
void setup() {
  Serial.begin(BAUD);

  // pin init
  for (uint8_t i = 0; i < 3; i++) {
    pinMode(PWM_PINS[i], OUTPUT);
    analogWrite(PWM_PINS[i], 0);
  }

  if (DIR_PINS[0] != 255) { pinMode(DIR_PINS[0], OUTPUT); digitalWrite(DIR_PINS[0], HIGH); }
  if (DIR_PINS[1] != 255) { pinMode(DIR_PINS[1], OUTPUT); digitalWrite(DIR_PINS[1], HIGH); }
  // motor3 no dir in your snippet

  // set PWM frequency if supported
  #if defined(TEENSYDUINO)
    analogWriteFrequency(PWM_PINS[0], PWM_FREQ[0]);
    analogWriteFrequency(PWM_PINS[1], PWM_FREQ[1]);
    analogWriteFrequency(PWM_PINS[2], PWM_FREQ[2]);
  #endif

  stopAll();
  Serial.println("ACK boot=OK proto=VIBv1");
}

void loop() {
  // non-blocking vibration engine
  tickVibration();

  // serial command intake
  if (readLine(lineBuf, LINE_MAX)) {
    const char* s = skipSpaces(lineBuf);
    if (*s) handleCommand(s);
  }
}