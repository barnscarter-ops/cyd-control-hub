// Control Hub — home dashboard for the 3.5" CYD (LCDWiki E32R35T)
// Hardware: ESP32-WROOM-32E + ST7796U (320x480) + XPT2046 resistive touch.
// Display and touch config lives in platformio.ini build_flags.
//
// setup() brings up each subsystem once; loop() only polls, so every module
// must stay non-blocking.

#include <Arduino.h>

#include "console.h"
#include "fan_link.h"
#include "gfx.h"
#include "lan_scan.h"
#include "portalbox.h"
#include "theme.h"
#include "touch_input.h"
#include "ui.h"
#include "wifi_link.h"

static const uint32_t HEARTBEAT_INTERVAL_MS = 30000;
static uint32_t       lastHeartbeatMs       = 0;

void setup() {
  Serial.begin(115200);
  delay(200);  // let the USB-serial bridge settle so the boot log isn't clipped
  Serial.println();
  Serial.println(F("[BOOT] Control Hub starting"));
  Serial.printf("[BOOT] Chip %s rev %d, %d MHz, heap %u bytes\n", ESP.getChipModel(),
                ESP.getChipRevision(), ESP.getCpuFreqMHz(), ESP.getFreeHeap());

  tft.init();
  displayBegin();  // landscape 480x320 (USB right) or 180° inverted; persisted in NVS
  digitalWrite(TFT_BL, !TFT_BACKLIGHT_ON);  // init() turns it on; stay dark until drawn
  tft.fillScreen(COLOR_BG);
  Serial.printf("[TFT ] ST7796 ready: %dx%d\n", tft.width(), tft.height());

  touchBegin();
  linkBegin();
  fanLinkBegin();
  pbBegin();
  uiBegin();
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
  Serial.println(F("[BOOT] Ready - type 'help' for console commands"));
}

void loop() {
  const uint32_t now = millis();

  linkLoop(now);
  fanLinkLoop(now);
  lanScanLoop(now);
  pbLoop(now);

  uint16_t x, y;
  if (touchTapped(now, x, y)) uiTap(x, y);
  uiTick(now);

  consoleLoop();

  if (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeatMs = now;
    Serial.printf("[HB  ] up %lus, %s, heap %u\n", (unsigned long)(now / 1000), linkStateLabel(),
                  ESP.getFreeHeap());
  }
}
