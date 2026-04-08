#include <Arduino.h>
#include "motor_driver.h"
#include "LED_array.h"

#define FW_VERSION "v2.1.0"

// ===== serial buffer =====
static char lineBuf[128];
static uint8_t lineLen = 0;
static bool overflow = false;

// Skip spaces/tabs at the beginning
static char* skipSpaces(char* p) {
  while (*p == ' ' || *p == '\t') p++;
  return p;
}

// Read one full command line from Serial
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

// Return firmware version
static void handleEcho() {
  Serial.print("E ");
  Serial.println(FW_VERSION);
}

void setup() {
  Serial.begin(115200);

  motorInit();
  ledInit();
}

void loop() {
  // Non-blocking updates
  updateMotors();
  updateLEDs();

  if (!readLine()) return;

  char* p = skipSpaces(lineBuf);
  if (!*p) return;

  char cmd = *p++;
  p = skipSpaces(p);

  // ===== motor =====
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

  // ===== LED =====
  // L strip idx r g b brightness
  if (cmd == 'L') {
    handleLEDSet(p);
    return;
  }

  // B brightness
  if (cmd == 'B') {
    handleLEDGlobalBrightness(p);
    return;
  }

  // C strip
  // strip = 0 / 1 / -1(all)
  if (cmd == 'C') {
    handleLEDClear(p);
    return;
  }

  // U
  // force LED output immediately
  if (cmd == 'U') {
    handleLEDShowNow();
    return;
  }
}