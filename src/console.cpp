#include "console.h"

#include <WiFi.h>

#include "gfx.h"
#include "fan_link.h"
#include "lan_scan.h"
#include "portalbox.h"
#include "theme.h"
#include "ui.h"
#include "wifi_link.h"

static String line;

// Dumps the framebuffer as run-length encoded hex: one line per row of
// tokens "CCCCNN" (RGB565 color, run length). tools/snap.py turns it into a PNG.
static void snap() {
  static uint16_t row[SCREEN_W];
  Serial.printf("SNAP BEGIN %d %d\n", SCREEN_W, SCREEN_H);
  for (int16_t y = 0; y < SCREEN_H; y++) {
    tft.readRect(0, y, SCREEN_W, 1, row);
    int16_t x = 0;
    while (x < SCREEN_W) {
      const uint16_t c   = row[x];
      uint8_t        run = 1;
      while (x + run < SCREEN_W && run < 255 && row[x + run] == c) run++;
      Serial.printf("%04X%02X", (uint16_t)((c >> 8) | (c << 8)), run);  // readRect is byte-swapped
      x += run;
    }
    Serial.print('\n');
  }
  Serial.println(F("SNAP END"));
}

static void status() {
  Serial.printf("[CON ] screen=%s link=%s portal=%s clients=%u saved=%u\n", uiScreenName(),
                linkStateLabel(), linkPortalActive() ? "open" : "closed", linkPortalClients(),
                (unsigned)linkSavedCount());
  if (linkState() == LinkState::Online) {
    Serial.printf("[CON ] ssid=\"%s\" ip=%s gw=%s rssi=%d\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(),
                  WiFi.RSSI());
  }
  if (linkPortalActive()) {
    Serial.printf("[CON ] portal ssid=\"%s\" pass=\"%s\"\n", linkPortalSsid().c_str(),
                  linkPortalPass().c_str());
  }
  Serial.printf("[CON ] heap=%u min=%u largest=%u devices=%u\n", ESP.getFreeHeap(),
                ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), (unsigned)lanDeviceCount());
  Serial.printf("[CON ] portalbox=%s store=%s portals=%u serving=%s captures=%lu clients=%u\n",
                pbActive() ? "up" : "idle", pbBackendName(), (unsigned)pbPortalCount(),
                pbSelectedPortal().length() ? pbSelectedPortal().c_str() : "built-in",
                (unsigned long)pbCaptureCount(), (unsigned)pbClients());
  Serial.printf("[CON ] fan=%s endpoint=%s queued=%u error=\"%s\"\n", fanLinkLabel(),
                fanLinkHost().c_str(), fanLinkQueuedControls(), fanLinkError().c_str());
  if (fanLinkHasSnapshot()) {
    Serial.printf("[CON ] fan mode=%s temp=%s duty=%.0f%% healthy=%s age=%lus\n",
                  fanLinkMode().c_str(), fanLinkSensorValid() ? String(fanLinkTemperatureC(), 1).c_str() : "invalid",
                  fanLinkDutyPct(), fanLinkFanHealthy() ? "yes" : "no",
                  (unsigned long)(fanLinkAgeMs(millis()) / 1000));
  }
}

static void run(String cmd) {
  cmd.trim();
  if (cmd.isEmpty()) return;

  if (cmd == "help") {
    Serial.println(F("[CON ] status | snap | tap <x> <y> | screen <dashboard|network|setup|fan>"));
    Serial.println(F("[CON ] scan | portal | close | reboot"));
    Serial.println(F("[CON ] wifi add <ssid> <pass> | wifi list | wifi forget"));
    Serial.println(F("[CON ] fan host <ip-or-name> [port]"));
    Serial.println(F("[CON ] screen portalbox | pb start|stop|ssid <name>|ch <n>"));
    Serial.println(F("[CON ] pb portal [list|sync|select <name>|delete <name>]"));
    Serial.println(F("[CON ] pb clone <url> [Name.html] - save a real portal page as a portal"));
    Serial.println(F("[CON ] pb capture [dump|clear] | pb beep on|off"));
  } else if (cmd == "status") {
    status();
  } else if (cmd == "snap") {
    snap();
  } else if (cmd.startsWith("tap ")) {
    int x = 0, y = 0;
    if (sscanf(cmd.c_str() + 4, "%d %d", &x, &y) == 2) uiTap(x, y);
  } else if (cmd == "screen dashboard") {
    uiShow(Screen::Dashboard);
  } else if (cmd == "screen network") {
    uiShow(Screen::Network);
  } else if (cmd == "screen setup") {
    uiShow(Screen::Setup);
  } else if (cmd == "screen fan") {
    uiShow(Screen::Fan);
  } else if (cmd == "screen portalbox") {
    uiShow(Screen::PortalBox);
  } else if (cmd == "scan") {
    lanScanStart();
  } else if (cmd == "portal") {
    linkOpenPortal();
  } else if (cmd == "close") {
    linkClosePortal();
  } else if (cmd.startsWith("wifi add ")) {
    // wifi add <ssid> <password>  (SSID without spaces; the portal handles the rest)
    const String rest = cmd.substring(9);
    const int    sp   = rest.indexOf(' ');
    linkAddNetwork(sp < 0 ? rest : rest.substring(0, sp), sp < 0 ? "" : rest.substring(sp + 1));
  } else if (cmd == "wifi list") {
    const int16_t n = WiFi.scanNetworks();  // blocking ~2 s; debug only
    for (int16_t i = 0; i < n; i++) {
      Serial.printf("[CON ] %4d dBm  ch%-2d  %s\n", WiFi.RSSI(i), WiFi.channel(i), WiFi.SSID(i).c_str());
    }
    WiFi.scanDelete();
  } else if (cmd == "wifi forget") {
    linkForgetAll();
  } else if (cmd == "reboot") {
    ESP.restart();
  } else if (cmd == "pb start") {
    pbStart();
  } else if (cmd == "pb stop") {
    pbStop();
  } else if (cmd.startsWith("pb ssid ")) {
    pbSetSsid(cmd.substring(8));
  } else if (cmd.startsWith("pb ch ")) {
    pbSetChannel((uint8_t)cmd.substring(6).toInt());
  } else if (cmd == "pb portal list") {
    Serial.printf("[CON ] store=%s pages=%u used=%llu/%llu KB serving=%s\n", pbBackendName(),
                  (unsigned)pbPortalCount(),
                  (unsigned long long)(pbStoreUsedBytes() >> 10),
                  (unsigned long long)(pbStoreTotalBytes() >> 10),
                  pbSelectedPortal().length() ? pbSelectedPortal().c_str() : "built-in");
    for (size_t i = 0; i < pbPortalCount(); i++)
      Serial.printf("[CON ]   %u: %s  %u B\n", (unsigned)i, pbPortalAt(i).c_str(),
                    (unsigned)pbPortalSize(i));
  } else if (cmd.startsWith("pb clone ")) {
    const String rest = cmd.substring(9);
    const int    sp   = rest.indexOf(' ');
    if (!pbClone(sp < 0 ? rest : rest.substring(0, sp), sp < 0 ? "" : rest.substring(sp + 1)))
      Serial.println(F("[CON ] Clone failed"));
  } else if (cmd == "pb portal sync") {
    Serial.printf("[CON ] sync: %u page(s) copied from flash\n", (unsigned)pbSyncFromFlash());
  } else if (cmd.startsWith("pb portal delete ")) {
    if (!pbDeletePortal(cmd.substring(17))) Serial.println(F("[CON ] No such portal"));
  } else if (cmd.startsWith("pb portal select ")) {
    if (!pbSelectPortal(cmd.substring(17))) Serial.println(F("[CON ] No such portal"));
  } else if (cmd == "pb capture dump") {
    pbDumpCaptures(Serial);
  } else if (cmd == "pb capture clear") {
    pbClearCaptures();
  } else if (cmd == "pb beep on") {
    pbSetBeep(true);
  } else if (cmd == "pb beep off") {
    pbSetBeep(false);
  } else if (cmd.startsWith("fan host ")) {
    const String rest = cmd.substring(9); const int sp = rest.indexOf(' ');
    fanLinkConfigure((sp < 0 ? rest : rest.substring(0, sp)).c_str(), sp < 0 ? 80 : (uint16_t)rest.substring(sp + 1).toInt());
  } else {
    Serial.printf("[CON ] Unknown command \"%s\" - try help\n", cmd.c_str());
  }
}

void consoleLoop() {
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      run(line);
      line = "";
    } else if (line.length() < 64) {
      line += c;
    }
  }
}
