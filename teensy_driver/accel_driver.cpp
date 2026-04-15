#include "accel_driver.h"

// Confirmed working mapping from brute-force search:
// CS=36, SCK=33, MOSI=34, MISO=35
static const uint8_t PIN_CS   = 36;
static const uint8_t PIN_SCK  = 33;
static const uint8_t PIN_MOSI = 34;
static const uint8_t PIN_MISO = 35;

// LIS3DH registers
static const uint8_t REG_STATUS_AUX = 0x07;
static const uint8_t REG_WHO_AM_I   = 0x0F;
static const uint8_t REG_CTRL1      = 0x20;
static const uint8_t REG_CTRL4      = 0x23;
static const uint8_t REG_OUT_X_L    = 0x28;
static const uint8_t LIS3DH_ID      = 0x33;

struct AccelState {
  bool detected = false;
  bool streamEnabled = false;
  uint32_t intervalMs = 10;
  uint32_t nextStreamMs = 0;
  uint32_t droppedFrames = 0;
};

static AccelState g_accel;

static inline void spiDelayShort() {
  delayMicroseconds(3);
}

// Bit-banged SPI MODE3 (CPOL=1, CPHA=1)
static uint8_t spiTransfer(uint8_t data) {
  uint8_t rx = 0;

  for (int i = 7; i >= 0; --i) {
    digitalWrite(PIN_SCK, HIGH);
    spiDelayShort();

    digitalWrite(PIN_MOSI, (data >> i) & 0x01);
    spiDelayShort();

    digitalWrite(PIN_SCK, LOW);
    spiDelayShort();

    rx <<= 1;
    if (digitalRead(PIN_MISO)) {
      rx |= 1;
    }

    spiDelayShort();
  }

  digitalWrite(PIN_SCK, HIGH);
  spiDelayShort();
  return rx;
}

static void writeReg(uint8_t reg, uint8_t value) {
  digitalWrite(PIN_CS, LOW);
  spiTransfer(reg & 0x7F);
  spiTransfer(value);
  digitalWrite(PIN_CS, HIGH);
}

static uint8_t readReg(uint8_t reg) {
  digitalWrite(PIN_CS, LOW);
  spiTransfer(0x80 | reg);
  uint8_t value = spiTransfer(0x00);
  digitalWrite(PIN_CS, HIGH);
  return value;
}

static void readAccelRaw(int16_t &x, int16_t &y, int16_t &z) {
  digitalWrite(PIN_CS, LOW);
  spiTransfer(0xC0 | REG_OUT_X_L); // read + auto-increment

  uint8_t xL = spiTransfer(0x00);
  uint8_t xH = spiTransfer(0x00);
  uint8_t yL = spiTransfer(0x00);
  uint8_t yH = spiTransfer(0x00);
  uint8_t zL = spiTransfer(0x00);
  uint8_t zH = spiTransfer(0x00);

  digitalWrite(PIN_CS, HIGH);

  x = (int16_t)((xH << 8) | xL) >> 4;
  y = (int16_t)((yH << 8) | yL) >> 4;
  z = (int16_t)((zH << 8) | zL) >> 4;
}

static bool canWriteLine(size_t n) {
  return Serial.availableForWrite() >= (int)n;
}

static void printSampleLine(int16_t x, int16_t y, int16_t z) {
  // Format: ACC,x,y,z
  // Typical length < 24 chars, so 32 bytes is a safe minimum.
  if (!canWriteLine(32)) {
    g_accel.droppedFrames++;
    return;
  }

  Serial.print("ACC,");
  Serial.print(x);
  Serial.print(',');
  Serial.print(y);
  Serial.print(',');
  Serial.println(z);
}

static void printStatusLine() {
  if (!canWriteLine(64)) return;

  Serial.print("ACC STATUS detected=");
  Serial.print(g_accel.detected ? 1 : 0);
  Serial.print(" stream=");
  Serial.print(g_accel.streamEnabled ? 1 : 0);
  Serial.print(" interval_ms=");
  Serial.print(g_accel.intervalMs);
  Serial.print(" dropped=");
  Serial.println(g_accel.droppedFrames);
}

static void configureLIS3DH() {
  // 400 Hz data rate + XYZ enable
  writeReg(REG_CTRL1, 0x77);
  // High-resolution mode, +/-2g, BDU off
  writeReg(REG_CTRL4, 0x88);
}

void accelInit() {
  pinMode(PIN_CS, OUTPUT);
  pinMode(PIN_SCK, OUTPUT);
  pinMode(PIN_MOSI, OUTPUT);
  pinMode(PIN_MISO, INPUT_PULLUP);

  digitalWrite(PIN_CS, HIGH);
  digitalWrite(PIN_SCK, HIGH);
  digitalWrite(PIN_MOSI, LOW);

  delay(10);

  uint8_t whoami = readReg(REG_WHO_AM_I);
  g_accel.detected = (whoami == LIS3DH_ID);

  if (g_accel.detected) {
    configureLIS3DH();
  }

  g_accel.streamEnabled = false;
  g_accel.intervalMs = 10;
  g_accel.nextStreamMs = millis() + g_accel.intervalMs;
  g_accel.droppedFrames = 0;

  if (canWriteLine(48)) {
    Serial.print("ACC INIT WHOAMI=0x");
    Serial.println(whoami, HEX);
  }
}

void updateAccelerometer() {
  if (!g_accel.detected || !g_accel.streamEnabled) return;

  uint32_t now = millis();
  if ((int32_t)(now - g_accel.nextStreamMs) < 0) return;

  // Catch up without drifting too much if loop jitter occurs.
  do {
    g_accel.nextStreamMs += g_accel.intervalMs;
  } while ((int32_t)(now - g_accel.nextStreamMs) >= 0);

  int16_t x, y, z;
  readAccelRaw(x, y, z);
  printSampleLine(x, y, z);
}

static char* skipSpacesLocal(char* p) {
  while (*p == ' ' || *p == '\t') p++;
  return p;
}

static bool startsWithToken(const char* s, const char* token) {
  while (*token) {
    char c1 = *s;
    char c2 = *token;
    if (c1 >= 'a' && c1 <= 'z') c1 = (char)(c1 - 'a' + 'A');
    if (c2 >= 'a' && c2 <= 'z') c2 = (char)(c2 - 'a' + 'A');
    if (c1 != c2) return false;
    s++;
    token++;
  }
  return (*s == '\0' || *s == ' ' || *s == '\t');
}

void handleAccelCommand(char* p) {
  p = skipSpacesLocal(p);

  if (!*p || startsWithToken(p, "HELP")) {
    if (canWriteLine(128)) {
      Serial.println("ACC CMDS: A HELP | A WHOAMI | A READ | A START [ms] | A STOP | A RATE ms");
    }
    return;
  }

  if (startsWithToken(p, "WHOAMI")) {
    uint8_t whoami = readReg(REG_WHO_AM_I);
    g_accel.detected = (whoami == LIS3DH_ID);
    if (g_accel.detected) configureLIS3DH();

    if (canWriteLine(48)) {
      Serial.print("ACC WHOAMI 0x");
      Serial.println(whoami, HEX);
    }
    return;
  }

  if (startsWithToken(p, "READ")) {
    if (!g_accel.detected) {
      uint8_t whoami = readReg(REG_WHO_AM_I);
      g_accel.detected = (whoami == LIS3DH_ID);
      if (g_accel.detected) configureLIS3DH();
    }

    if (!g_accel.detected) {
      if (canWriteLine(24)) Serial.println("ACC ERROR NOT_FOUND");
      return;
    }

    int16_t x, y, z;
    readAccelRaw(x, y, z);
    printSampleLine(x, y, z);
    return;
  }

  if (startsWithToken(p, "START")) {
    uint32_t interval = g_accel.intervalMs;
    char* arg = p + 5;
    arg = skipSpacesLocal(arg);
    if (*arg) {
      long tmp = -1;
      if (sscanf(arg, "%ld", &tmp) == 1 && tmp >= 1 && tmp <= 1000) {
        interval = (uint32_t)tmp;
      }
    }

    if (!g_accel.detected) {
      uint8_t whoami = readReg(REG_WHO_AM_I);
      g_accel.detected = (whoami == LIS3DH_ID);
      if (g_accel.detected) configureLIS3DH();
    }

    if (!g_accel.detected) {
      if (canWriteLine(24)) Serial.println("ACC ERROR NOT_FOUND");
      return;
    }

    g_accel.intervalMs = interval;
    g_accel.streamEnabled = true;
    g_accel.nextStreamMs = millis() + g_accel.intervalMs;

    if (canWriteLine(40)) {
      Serial.print("ACC START ");
      Serial.println(g_accel.intervalMs);
    }
    return;
  }

  if (startsWithToken(p, "STOP")) {
    g_accel.streamEnabled = false;
    if (canWriteLine(16)) Serial.println("ACC STOP");
    return;
  }

  if (startsWithToken(p, "RATE")) {
    long interval = -1;
    char* arg = p + 4;
    arg = skipSpacesLocal(arg);
    if (sscanf(arg, "%ld", &interval) == 1 && interval >= 1 && interval <= 1000) {
      g_accel.intervalMs = (uint32_t)interval;
      g_accel.nextStreamMs = millis() + g_accel.intervalMs;
      if (canWriteLine(40)) {
        Serial.print("ACC RATE ");
        Serial.println(g_accel.intervalMs);
      }
    } else {
      if (canWriteLine(24)) Serial.println("ACC ERROR BAD_RATE");
    }
    return;
  }

  if (startsWithToken(p, "STATUS")) {
    printStatusLine();
    return;
  }

  if (canWriteLine(24)) Serial.println("ACC ERROR BAD_CMD");
}
