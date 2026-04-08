#include "LED_array.h"
#include <FastLED.h>

// ===== LED config =====
#define LED_PIN_0 23
#define LED_PIN_1 22

#define NUM_LEDS_0 60
#define NUM_LEDS_1 60

#define LED_TYPE WS2812
#define COLOR_ORDER GRB

static CRGB leds0[NUM_LEDS_0];
static CRGB leds1[NUM_LEDS_1];

// Global brightness for both strips
static uint8_t g_ledBrightness = 255;

// Only show when data changed
static bool ledDirty = false;

// Return strip pointer by strip ID
static CRGB* getStrip(uint8_t stripId, uint16_t &stripLen) {
  if (stripId == 0) {
    stripLen = NUM_LEDS_0;
    return leds0;
  }

  if (stripId == 1) {
    stripLen = NUM_LEDS_1;
    return leds1;
  }

  stripLen = 0;
  return nullptr;
}

// Clear one strip or all strips
static void clearStrip(int stripId) {
  if (stripId == 0 || stripId == -1) {
    for (int i = 0; i < NUM_LEDS_0; i++) {
      leds0[i] = CRGB::Black;
    }
  }

  if (stripId == 1 || stripId == -1) {
    for (int i = 0; i < NUM_LEDS_1; i++) {
      leds1[i] = CRGB::Black;
    }
  }

  ledDirty = true;
}

// Init LED strips
void ledInit() {
  FastLED.addLeds<LED_TYPE, LED_PIN_0, COLOR_ORDER>(leds0, NUM_LEDS_0);
  FastLED.addLeds<LED_TYPE, LED_PIN_1, COLOR_ORDER>(leds1, NUM_LEDS_1);

  FastLED.setBrightness(g_ledBrightness);

  for (int i = 0; i < NUM_LEDS_0; i++) leds0[i] = CRGB::Black;
  for (int i = 0; i < NUM_LEDS_1; i++) leds1[i] = CRGB::Black;

  FastLED.show();
  ledDirty = false;
}

// Update LEDs only when needed
void updateLEDs() {
  if (!ledDirty) return;

  FastLED.show();
  ledDirty = false;
}

// Parse command: L strip idx r g b brightness
void handleLEDSet(char* p) {
  long strip, idx, r, g, b, brightness;

  if (sscanf(p, "%ld %ld %ld %ld %ld %ld",
             &strip, &idx, &r, &g, &b, &brightness) == 6) {

    if (strip < 0 || strip > 1) return;
    if (r < 0 || r > 255) return;
    if (g < 0 || g > 255) return;
    if (b < 0 || b > 255) return;
    if (brightness < 0 || brightness > 255) return;

    uint16_t stripLen = 0;
    CRGB* target = getStrip((uint8_t)strip, stripLen);
    if (target == nullptr) return;
    if (idx < 0 || idx >= stripLen) return;

    CRGB color((uint8_t)r, (uint8_t)g, (uint8_t)b);
    color.nscale8_video((uint8_t)brightness);  // per-pixel brightness

    target[idx] = color;
    ledDirty = true;
  }
}

// Parse command: B brightness
void handleLEDGlobalBrightness(char* p) {
  long b;
  if (sscanf(p, "%ld", &b) == 1) {
    if (b >= 0 && b <= 255) {
      g_ledBrightness = (uint8_t)b;
      FastLED.setBrightness(g_ledBrightness);
      ledDirty = true;
    }
  }
}

// Parse command: C strip
void handleLEDClear(char* p) {
  long strip;
  if (sscanf(p, "%ld", &strip) == 1) {
    if (strip == 0 || strip == 1 || strip == -1) {
      clearStrip((int)strip);
    }
  }
}

// Force output immediately
void handleLEDShowNow() {
  FastLED.show();
  ledDirty = false;
}