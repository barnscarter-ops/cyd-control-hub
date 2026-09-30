#include "touch_input.h"

#include <Preferences.h>

#include "gfx.h"
#include "theme.h"

static const uint32_t TOUCH_POLL_INTERVAL_MS = 30;
static const char    *PREFS_NAMESPACE        = "touch";
static const char    *PREFS_CAL_KEY          = "cal";
static const size_t   CAL_LEN                = 5;

static uint32_t lastPollMs   = 0;
static bool     touchWasDown = false;

static void runCalibration(Preferences &prefs, uint16_t *cal) {
  tft.fillScreen(COLOR_BG);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);

  text(tft, "Touch calibration", SCREEN_W / 2, SCREEN_H / 2 - 14, Font::UiLg, COLOR_TEXT,
       COLOR_BG, MC_DATUM);
  text(tft, "Tap each corner marker as it appears", SCREEN_W / 2, SCREEN_H / 2 + 16, Font::UiSm,
       COLOR_SUBTLE, COLOR_BG, MC_DATUM);
  tft.unloadFont();  // calibrateTouch() draws with the built-in font

  Serial.println(F("[TOUCH] Calibration started - tap the corner markers"));
  tft.calibrateTouch(cal, COLOR_CYAN, COLOR_BG, 15);

  prefs.putBytes(PREFS_CAL_KEY, cal, CAL_LEN * sizeof(uint16_t));
  Serial.printf("[TOUCH] Calibration saved: %u %u %u %u %u\n",
                cal[0], cal[1], cal[2], cal[3], cal[4]);

  tft.fillScreen(COLOR_BG);
  digitalWrite(TFT_BL, !TFT_BACKLIGHT_ON);
}

void touchBegin() {
  pinMode(TOUCH_IRQ, INPUT);

  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);

  uint16_t cal[CAL_LEN];
  const bool haveCal    = prefs.isKey(PREFS_CAL_KEY) &&
                          prefs.getBytes(PREFS_CAL_KEY, cal, sizeof(cal)) == sizeof(cal);
  const bool heldAtBoot = digitalRead(TOUCH_IRQ) == LOW;

  if (!haveCal || heldAtBoot) {
    if (heldAtBoot) Serial.println(F("[TOUCH] Screen held at boot - forcing re-calibration"));
    runCalibration(prefs, cal);
  } else {
    Serial.printf("[TOUCH] Loaded calibration: %u %u %u %u %u\n",
                  cal[0], cal[1], cal[2], cal[3], cal[4]);
  }
  prefs.end();

  tft.setTouch(cal);
  Serial.println(F("[TOUCH] XPT2046 ready"));
}

bool touchDown() { return digitalRead(TOUCH_IRQ) == LOW; }

bool touchTapped(uint32_t now, uint16_t &x, uint16_t &y) {
  if (now - lastPollMs < TOUCH_POLL_INTERVAL_MS) return false;
  lastPollMs = now;

  // IRQ is LOW only while pressed; skip the SPI read otherwise. A press stays
  // "consumed" until IRQ releases, so a noisy mid-press read can't re-fire.
  if (digitalRead(TOUCH_IRQ) != LOW) {
    touchWasDown = false;
    return false;
  }
  if (touchWasDown || !tft.getTouch(&x, &y)) return false;
  touchWasDown = true;
  return true;
}
