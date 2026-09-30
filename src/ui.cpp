#include "ui.h"

#include <WiFi.h>
#include <qrcode.h>
#include <time.h>

#include "gfx.h"
#include "fan_link.h"
#include "lan_scan.h"
#include "portalbox.h"
#include "theme.h"
#include "touch_input.h"
#include "wifi_link.h"

static const uint32_t CLOCK_REFRESH_MS   = 1000;
static const uint32_t STATS_REFRESH_MS   = 5000;
static const uint32_t PRESS_FEEDBACK_MS  = 140;
static const uint32_t LONG_PRESS_MS      = 1500;  // final hold in the tap-tap-hold unlock
static const uint32_t TAP_WINDOW_MS      = 400;   // max gap between the two taps
static const uint32_t LAN_STALE_MS       = 60000;

static const int16_t  TABLE_Y     = 128;
static const int16_t  ROW_H       = 24;
static const int16_t  ROWS_Y      = TABLE_Y + ROW_H + 2;
static const uint8_t  ROWS_PER_PAGE = 6;
static const int16_t  COL_MAC_X   = 132;
static const int16_t  COL_TYPE_X  = 300;

// Header buttons on sub-screens
static const int16_t BTN_H         = 30;
static const int16_t BTN_Y         = 9;
static const int16_t BTN_ADD_W     = 96;
static const int16_t BTN_ADD_X     = SCREEN_W - MARGIN - BTN_ADD_W;
static const int16_t BTN_SCAN_W    = 92;
static const int16_t BTN_SCAN_X    = BTN_ADD_X - 8 - BTN_SCAN_W;
static const int16_t BTN_FLIP_W    = 30;
static const int16_t BTN_FLIP_X    = BTN_SCAN_X - 8 - BTN_FLIP_W;
static const int16_t DASH_FLIP_W   = 28;
static const int16_t DASH_FLIP_X   = 336;

static Screen   screen          = Screen::Dashboard;
static uint32_t seenLinkRev     = 0;
static uint32_t seenFanRev      = 0;
static uint32_t nextClockMs     = 0;
static uint32_t nextStatsMs     = 0;
static int8_t   pressedCard     = -1;
static uint32_t pressedAtMs     = 0;
static uint8_t  tablePage       = 0;
static uint32_t tableSig        = 0;
static bool     lastScanRunning = false;
static uint8_t  lastClients     = 0;
static bool     longPressDone   = false;  // the current card press already fired its hold action
static uint8_t  pentestTaps     = 0;      // taps in the current tap-tap-hold sequence
static uint32_t pentestTapUpMs  = 0;      // when the last tap in the sequence was released

// ---------------------------------------------------------------------------
// Off-screen rendering
// ---------------------------------------------------------------------------
// Renders `draw(g, ox, oy)` into a sprite and pushes it in one go (no flicker).
// If there's not enough heap for the sprite it draws straight to the panel.
template <typename F>
static void offscreen(int16_t x, int16_t y, int16_t w, int16_t h, F draw) {
  TFT_eSprite spr(&tft);
  spr.setColorDepth(16);
  if (spr.createSprite(w, h)) {
    spr.fillSprite(COLOR_BG);
    draw(spr, 0, 0);
    spr.pushSprite(x, y);
  } else {
    tft.fillRect(x, y, w, h, COLOR_BG);
    draw(tft, x, y);
  }
}

// ---------------------------------------------------------------------------
// Shared pieces
// ---------------------------------------------------------------------------
struct Tone {
  const char *label;
  uint16_t    color;
  uint16_t    tint;
};

static Tone linkTone() {
  switch (linkState()) {
    case LinkState::Online:     return {"ONLINE", COLOR_MATRIX, COLOR_GREEN_TINT};
    case LinkState::Searching:
    case LinkState::Connecting: return {"CONNECTING", COLOR_AMBER, COLOR_AMBER_TINT};
    case LinkState::Offline:
      if (linkPortalActive()) return {"SETUP MODE", COLOR_CYAN, COLOR_CYAN_TINT};
      return {"OFFLINE", COLOR_RED, COLOR_RED_TINT};
    case LinkState::Suspended: return {"PENTEST AP", COLOR_CYAN, COLOR_CYAN_TINT};
  }
  return {"", COLOR_SUBTLE, COLOR_PANEL};
}

static uint8_t signalLevel() {
  if (linkState() != LinkState::Online) return 0;
  const int32_t rssi = WiFi.RSSI();
  if (rssi > -55) return 4;
  if (rssi > -65) return 3;
  if (rssi > -75) return 2;
  if (rssi > -85) return 1;
  return 0;
}

static void chevron(TFT_eSPI &g, int32_t cx, int32_t cy, uint16_t fg, uint16_t bg) {
  g.drawWideLine(cx - 3, cy - 6, cx + 3, cy, 2.0f, fg, bg);
  g.drawWideLine(cx + 3, cy, cx - 3, cy + 6, 2.0f, fg, bg);
}

static void subHeader(const char *title) {
  iconBadge(tft, MARGIN, 8, Icon::Back, COLOR_CYAN, COLOR_CYAN_TINT, COLOR_BG);
  text(tft, title, MARGIN + 44, HEADER_H / 2 + 1, Font::UiLg, COLOR_TEXT, COLOR_BG, ML_DATUM);
}

static bool hitBack(uint16_t x, uint16_t y) { return x < 150 && y < HEADER_H; }

static String uptimeText() {
  uint32_t s = millis() / 1000;
  const uint32_t d = s / 86400;
  s %= 86400;
  char buf[24];
  if (d) snprintf(buf, sizeof(buf), "%lud %02lu:%02lu", (unsigned long)d, (unsigned long)(s / 3600),
                  (unsigned long)(s / 60 % 60));
  else   snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", (unsigned long)(s / 3600),
                  (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
  return buf;
}

static void drawFooter() {
  offscreen(0, FOOTER_Y, SCREEN_W, FOOTER_H, [](TFT_eSPI &g, int16_t ox, int16_t oy) {
    String left;
    if (screen == Screen::Setup) {
      left = "SAVED  ";
      if (linkSavedCount() == 0) left += "none yet";
      for (size_t i = 0; i < linkSavedCount(); i++) {
        if (i) left += " · ";
        left += linkSavedSsid(i);
      }
    } else if (linkState() == LinkState::Online) {
      left = WiFi.localIP().toString() + " · " + WiFi.SSID();
    } else if (linkState() == LinkState::Suspended) {
      left = "AP " + pbSsid() + " · " + pbApIp();
    } else if (linkPortalActive()) {
      left = "AP " + linkPortalSsid() + " · 192.168.4.1";
    } else {
      left = "No network";
    }
    const String right = "UP " + uptimeText() + " · " + String(ESP.getFreeHeap() / 1024) + "K";
    useFont(g, Font::MonoSm);
    const int16_t rightW = g.textWidth(right);
    text(g, fitText(g, left, Font::MonoSm, SCREEN_W - 2 * MARGIN - rightW - 16).c_str(),
         ox + MARGIN, oy + FOOTER_H / 2, Font::MonoSm, COLOR_SUBTLE, COLOR_BG, ML_DATUM);
    text(g, right.c_str(), ox + SCREEN_W - MARGIN, oy + FOOTER_H / 2, Font::MonoSm, COLOR_MUTED,
         COLOR_BG, MR_DATUM);
  });
}

// ---------------------------------------------------------------------------
// Dashboard
// ---------------------------------------------------------------------------
struct CardDef {
  const char *title;
  const char *subtitle;
  Icon        icon;
  int16_t     x;
  int16_t     y;
};

static const CardDef CARDS[] = {
    {"SERVER FAN", "Rack cooling", Icon::Power, COL_LEFT_X, ROW_TOP_Y},
    {"GARAGE CTRL", "Main door", Icon::Garage, COL_RIGHT_X, ROW_TOP_Y},
    {"WIFI PENTESTER", "Audit toolkit", Icon::Wifi, COL_LEFT_X, ROW_BOT_Y},
    {"SYSTEM STATUS", "Network & devices", Icon::Pulse, COL_RIGHT_X, ROW_BOT_Y},
};
static const size_t CARD_COUNT  = sizeof(CARDS) / sizeof(CARDS[0]);
static const size_t CARD_SYSTEM = 3;
static const size_t CARD_PENTEST = 2;  // hold to open PortalBox

struct CardValue {
  String   value;
  uint16_t color;
  String   meta;
};

static CardValue cardValue(size_t i) {
  switch (i) {
    case 0: {
      uint16_t color = COLOR_AMBER;
      if (fanLinkState() == FanLinkState::Online && fanLinkSensorValid() && fanLinkFanHealthy() &&
          fanLinkMode() != "failsafe") color = COLOR_MATRIX;
      if (fanLinkState() == FanLinkState::Offline || fanLinkState() == FanLinkState::Fault ||
          fanLinkState() == FanLinkState::Stale ||
          (fanLinkHasSnapshot() && !fanLinkSensorValid()) ||
          (fanLinkHasSnapshot() && !fanLinkFanHealthy())) color = COLOR_RED;
      const String value = fanLinkState() == FanLinkState::Online && fanLinkSensorValid()
                               ? String(fanLinkTemperatureC(), 1) + " C" : fanLinkLabel();
      String meta = "Fan controller";
      if (fanLinkHasSnapshot()) meta = fanLinkMode() + " / " + String(fanLinkDutyPct(), 0) + "%";
      return {value, color, meta};
    }
    case 1: return {"CLOSED", COLOR_MATRIX, "Locked"};
    case 2:
      if (pbActive())
        return {"LIVE", COLOR_MATRIX,
                String(pbClients()) + (pbClients() == 1 ? " client" : " clients")};
      return {"READY", COLOR_CYAN, "Locked"};
  }
  switch (linkState()) {
    case LinkState::Online:
      return {"SECURE", COLOR_MATRIX,
              lanScanFinishedMs() ? String(lanDeviceCount()) + " devices" : WiFi.SSID()};
    case LinkState::Searching:  return {"LINKING", COLOR_AMBER, "Searching"};
    case LinkState::Connecting: return {"LINKING", COLOR_AMBER, "Joining"};
    case LinkState::Suspended:  return {"PAUSED", COLOR_CYAN, "Pentest AP"};
    case LinkState::Offline:    break;
  }
  if (linkPortalActive()) return {"SETUP", COLOR_CYAN, "Add Wi-Fi"};
  return {"OFFLINE", COLOR_RED, "Retrying"};
}

static void drawCard(size_t i) {
  const CardDef &def     = CARDS[i];
  const bool     pressed = pressedCard == (int8_t)i;
  offscreen(def.x, def.y, CARD_W, CARD_H, [&](TFT_eSPI &g, int16_t ox, int16_t oy) {
    const uint16_t fill = pressed ? COLOR_PANEL_HI : COLOR_PANEL;
    card(g, ox, oy, CARD_W, CARD_H, CARD_RADIUS, fill, pressed ? COLOR_CYAN : COLOR_EDGE, COLOR_BG);
    iconBadge(g, ox + 14, oy + 14, def.icon, COLOR_CYAN, COLOR_CYAN_TINT, fill);
    text(g, def.title, ox + 58, oy + 14, Font::UiMd, COLOR_TEXT, fill);
    text(g, def.subtitle, ox + 58, oy + 34, Font::UiSm, COLOR_SUBTLE, fill);
    g.drawFastHLine(ox + 14, oy + 60, CARD_W - 28, COLOR_EDGE);

    // Cards that open on a tap get a chevron; WIFI PENTESTER opens on a hold.
    const bool    nav    = i == CARD_SYSTEM || i == 0;
    const int16_t metaR  = CARD_W - 14 - (nav ? 16 : 0);
    if (nav) chevron(g, ox + CARD_W - 18, oy + CARD_H - 22, COLOR_CYAN, fill);

    const CardValue v      = cardValue(i);
    const int16_t   valueW = text(g, v.value.c_str(), ox + 14, oy + CARD_H - 16, Font::UiXl, v.color,
                                  fill, L_BASELINE);
    const String meta = fitText(g, v.meta, Font::UiSm, metaR - 14 - valueW - 12);
    text(g, meta.c_str(), ox + metaR, oy + CARD_H - 17, Font::UiSm, COLOR_SUBTLE, fill,
         R_BASELINE);
  });
}

static int16_t dashboardPillX() {
  useFont(tft, Font::UiLg);
  return 50 + tft.textWidth("HOME HUB") + 14;
}

static void drawDashboardPill() {
  const int16_t x = dashboardPillX();
  offscreen(x, 13, 150, 22, [](TFT_eSPI &g, int16_t ox, int16_t oy) {
    const Tone t = linkTone();
    pill(g, ox, oy, t.label, t.color, t.tint, COLOR_BG);
  });
}

static void drawClock() {
  offscreen(330, 0, SCREEN_W - MARGIN - 330, HEADER_H, [](TFT_eSPI &g, int16_t ox, int16_t oy) {
    const int16_t w = SCREEN_W - MARGIN - 330;
    char      buf[16] = "--:--";
    struct tm t;
    if (getLocalTime(&t, 0) && t.tm_year > 120) strftime(buf, sizeof(buf), "%I:%M %p", &t);
    const char *shown = buf[0] == '0' ? buf + 1 : buf;  // "09:41 AM" -> "9:41 AM"
    const int16_t tw  = text(g, shown, ox + w, oy + HEADER_H / 2 + 1, Font::UiMd, COLOR_TEXT,
                             COLOR_BG, MR_DATUM);
    signalBars(g, ox + w - tw - 30, oy + HEADER_H / 2 + 7, signalLevel(), COLOR_CYAN, COLOR_EDGE);
  });
  // Drawn after the clock sprite every refresh so the clock redraw never covers it.
  iconButton(tft, DASH_FLIP_X, BTN_Y, DASH_FLIP_W, BTN_H, Icon::Rotate, displayFlipped());
}

static void drawDashboard() {
  tft.fillScreen(COLOR_BG);
  logoMark(tft, MARGIN, 12);
  text(tft, "HOME HUB", 50, HEADER_H / 2 + 1, Font::UiLg, COLOR_TEXT, COLOR_BG, ML_DATUM);
  headerRule(tft);
  drawDashboardPill();
  drawClock();
  for (size_t i = 0; i < CARD_COUNT; i++) drawCard(i);
  drawFooter();
}

static void tapDashboard(uint16_t x, uint16_t y) {
  if (y < HEADER_H && x >= DASH_FLIP_X && x < DASH_FLIP_X + DASH_FLIP_W) {
    displayFlip();
    uiShow(Screen::Dashboard);
    return;
  }
  for (size_t i = 0; i < CARD_COUNT; i++) {
    const CardDef &c = CARDS[i];
    if (x >= c.x && x < c.x + CARD_W && y >= c.y && y < c.y + CARD_H) {
      Serial.printf("[UI  ] Card tapped: %s\n", c.title);
      pressedCard   = (int8_t)i;
      pressedAtMs   = millis();
      longPressDone = false;
      drawCard(i);
      return;
    }
  }
}

// Called once the press highlight has been visible long enough.
static void releaseCard() {
  const size_t i = (size_t)pressedCard;
  pressedCard    = -1;
  if (i == CARD_SYSTEM) {
    const bool needsSetup = linkState() != LinkState::Online && linkPortalActive();
    uiShow(needsSetup ? Screen::Setup : Screen::Network);
  } else if (i == 0) {
    uiShow(Screen::Fan);
  } else {
    drawCard(i);
  }
}

static float fanTargetC = 35.0f, fanDutyPct = 50.0f;

static uint16_t fanStateColor() {
  if (fanLinkState() == FanLinkState::Online && fanLinkSensorValid() && fanLinkFanHealthy() &&
      fanLinkMode() != "failsafe") return COLOR_MATRIX;
  if (fanLinkState() == FanLinkState::Requesting) return COLOR_CYAN;
  if (fanLinkState() == FanLinkState::NotConfigured) return COLOR_AMBER;
  return COLOR_RED;
}

static void drawFan() {
  tft.fillScreen(COLOR_BG);
  subHeader("Server Fan");
  headerRule(tft);

  const String endpoint = fanLinkHost().length() ? fanLinkHost() : "console: fan host <ip> [port]";
  text(tft, fitText(tft, endpoint, Font::MonoSm, 260).c_str(), SCREEN_W - MARGIN, 24,
       Font::MonoSm, COLOR_SUBTLE, COLOR_BG, MR_DATUM);
  text(tft, fanLinkLabel(), MARGIN, 70, Font::UiLg, fanStateColor(), COLOR_BG, ML_DATUM);

  String activity;
  if (fanLinkRequestActive()) activity = "sending";
  if (fanLinkQueuedControls()) {
    if (activity.length()) activity += " / ";
    activity += String(fanLinkQueuedControls()) + " queued";
  }
  if (!activity.length() && fanLinkHasSnapshot())
    activity = "age " + String(fanLinkAgeMs(millis()) / 1000) + "s";
  text(tft, activity.c_str(), SCREEN_W - MARGIN, 70, Font::MonoSm, COLOR_SUBTLE, COLOR_BG,
       MR_DATUM);

  String detail = fanLinkError();
  if (!detail.length() && fanLinkFailsafeReason().length())
    detail = "Failsafe: " + fanLinkFailsafeReason();
  text(tft, fitText(tft, detail, Font::UiSm, SCREEN_W - 2 * MARGIN).c_str(), MARGIN, 94,
       Font::UiSm, detail.length() ? COLOR_RED : COLOR_SUBTLE, COLOR_BG, ML_DATUM);

  const int16_t panelY = 108, panelH = 66, panelW = SCREEN_W - 2 * MARGIN;
  card(tft, MARGIN, panelY, panelW, panelH, CARD_RADIUS, COLOR_PANEL, COLOR_EDGE, COLOR_BG);
  const char *labels[4] = {"SENSOR", "MODE", "APPLIED", "FANS"};
  String values[4] = {
      fanLinkHasSnapshot() ? (fanLinkSensorValid() ? String(fanLinkTemperatureC(), 1) + " C" : "FAULT") : "-",
      fanLinkHasSnapshot() ? fanLinkMode() : "-",
      fanLinkFanCount() ? String(fanLinkFanDutyPct(0), 0) + "%" : "-",
      fanLinkHasSnapshot() ? (fanLinkFanHealthy() ? "OK" : "FAULT") : "-",
  };
  if (fanLinkFanCount() > 1)
    values[2] = String(fanLinkFanDutyPct(0), 0) + "/" + String(fanLinkFanDutyPct(1), 0) + "%";
  if (fanLinkFanCount()) {
    values[3] += " " + String(fanLinkFanRpm(0));
    if (fanLinkFanCount() > 1) values[3] += "/" + String(fanLinkFanRpm(1));
  }
  const int16_t colW = panelW / 4;
  for (int i = 0; i < 4; i++) {
    if (i) tft.drawFastVLine(MARGIN + i * colW, panelY + 12, panelH - 24, COLOR_EDGE);
    const int16_t x = MARGIN + i * colW + 10;
    text(tft, labels[i], x, panelY + 11, Font::UiSm, COLOR_SUBTLE, COLOR_PANEL);
    text(tft, fitText(tft, values[i], Font::MonoMd, colW - 18).c_str(), x, panelY + 34,
         Font::MonoMd, COLOR_TEXT, COLOR_PANEL);
  }

  text(tft, "AUTO TARGET", 16, 188, Font::UiSm, COLOR_SUBTLE, COLOR_BG);
  text(tft, "MANUAL DUTY", 260, 188, Font::UiSm, COLOR_SUBTLE, COLOR_BG);
  button(tft, 16, 205, 42, 36, "-", false);
  button(tft, 64, 205, 92, 36, (String(fanTargetC, 1) + " C").c_str(), false);
  button(tft, 162, 205, 42, 36, "+", false);
  button(tft, 260, 205, 42, 36, "-", false);
  button(tft, 308, 205, 92, 36, (String(fanDutyPct, 0) + "%").c_str(), false);
  button(tft, 406, 205, 42, 36, "+", false);
  button(tft, 16, 250, 188, 36, "Queue Auto", false);
  button(tft, 260, 250, 188, 36, "Queue Manual", false);
}

static void tapFan(uint16_t x, uint16_t y) {
  if (hitBack(x, y)) { uiShow(Screen::Dashboard); return; }
  if (y >= 205 && y < 241) {
    if (x >= 16 && x < 58) fanTargetC = max(20.0f, fanTargetC - 1.0f);
    else if (x >= 162 && x < 204) fanTargetC = min(80.0f, fanTargetC + 1.0f);
    else if (x >= 260 && x < 302) fanDutyPct = max(0.0f, fanDutyPct - 5.0f);
    else if (x >= 406 && x < 448) fanDutyPct = min(100.0f, fanDutyPct + 5.0f);
    else return;
    drawFan();
  } else if (y >= 250 && y < 286) {
    if (x >= 16 && x < 204) fanLinkSetAuto(fanTargetC);
    else if (x >= 260 && x < 448) fanLinkSetManual(fanDutyPct);
    drawFan();
  }
}

// ---------------------------------------------------------------------------
// Network screen
// ---------------------------------------------------------------------------
static uint8_t pageCount() {
  const size_t n = lanDeviceCount();
  return n == 0 ? 1 : (uint8_t)((n + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE);
}

static void drawNetworkButtons() {
  const bool scanning = lanScanRunning();
  iconButton(tft, BTN_FLIP_X, BTN_Y, BTN_FLIP_W, BTN_H, Icon::Rotate, displayFlipped());
  button(tft, BTN_SCAN_X, BTN_Y, BTN_SCAN_W, BTN_H, scanning ? "Scanning" : "Rescan", scanning);
  button(tft, BTN_ADD_X, BTN_Y, BTN_ADD_W, BTN_H, "Add Wi-Fi", false);
}

static void drawStats() {
  offscreen(MARGIN, CONTENT_Y, SCREEN_W - 2 * MARGIN, 54, [](TFT_eSPI &g, int16_t ox, int16_t oy) {
    const int16_t w = SCREEN_W - 2 * MARGIN;
    card(g, ox, oy, w, 54, CARD_RADIUS, COLOR_PANEL, COLOR_EDGE, COLOR_BG);

    const bool online = linkState() == LinkState::Online;
    const String values[4] = {
        online ? WiFi.SSID() : String("-"),
        online ? WiFi.localIP().toString() : String("-"),
        online ? String(WiFi.RSSI()) + " dBm" : String("-"),
        lanScanFinishedMs() || lanScanRunning() ? String(lanDeviceCount()) : String("-"),
    };
    static const char *labels[4] = {"NETWORK", "IP ADDRESS", "SIGNAL", "DEVICES"};
    const int16_t colW = w / 4;
    for (int i = 0; i < 4; i++) {
      const int16_t cx = ox + i * colW + 14;
      if (i) g.drawFastVLine(ox + i * colW, oy + 12, 30, COLOR_EDGE);
      text(g, labels[i], cx, oy + 10, Font::UiSm, COLOR_SUBTLE, COLOR_PANEL);
      const String v = fitText(g, values[i], Font::MonoMd, colW - 24);
      text(g, v.c_str(), cx, oy + 28, Font::MonoMd, i == 0 && online ? COLOR_CYAN : COLOR_TEXT,
           COLOR_PANEL);
    }
  });
}

static void drawTableHeader() {
  offscreen(MARGIN, TABLE_Y, SCREEN_W - 2 * MARGIN, ROW_H, [](TFT_eSPI &g, int16_t ox, int16_t oy) {
    const int16_t w  = SCREEN_W - 2 * MARGIN;
    const int16_t cy = oy + ROW_H / 2 + 1;
    g.fillSmoothRoundRect(ox, oy, w, ROW_H, 6, COLOR_PANEL_HI, COLOR_BG);
    text(g, "IP ADDRESS", ox + 12, cy, Font::UiSm, COLOR_SUBTLE, COLOR_PANEL_HI, ML_DATUM);
    text(g, "MAC ADDRESS", ox + COL_MAC_X, cy, Font::UiSm, COLOR_SUBTLE, COLOR_PANEL_HI, ML_DATUM);
    text(g, "TYPE", ox + COL_TYPE_X, cy, Font::UiSm, COLOR_SUBTLE, COLOR_PANEL_HI, ML_DATUM);

    if (lanScanRunning()) {
      const uint8_t p = lanScanProgress();
      text(g, (String(p) + "%").c_str(), ox + w - 12, cy, Font::MonoSm, COLOR_CYAN,
           COLOR_PANEL_HI, MR_DATUM);
      g.fillRect(ox + 6, oy + ROW_H - 2, (w - 12) * p / 100, 2, COLOR_CYAN);
    } else if (pageCount() > 1) {
      const String page = String(tablePage + 1) + " / " + String(pageCount());
      const int16_t pw  = text(g, page.c_str(), ox + w - 24, cy, Font::MonoSm, COLOR_SUBTLE,
                               COLOR_PANEL_HI, MR_DATUM);
      (void)pw;
      chevron(g, ox + w - 12, cy, COLOR_CYAN, COLOR_PANEL_HI);
    }
  });
}

static uint16_t kindColor(const char *kind) {
  if (strcmp(kind, "Gateway") == 0) return COLOR_CYAN;
  if (strcmp(kind, "This hub") == 0) return COLOR_MATRIX;
  if (strcmp(kind, "Private MAC") == 0) return COLOR_SUBTLE;
  return COLOR_TEXT;
}

static void drawRow(uint8_t row) {
  const int16_t y = ROWS_Y + row * ROW_H;
  offscreen(MARGIN, y, SCREEN_W - 2 * MARGIN, ROW_H, [row](TFT_eSPI &g, int16_t ox, int16_t oy) {
    const int16_t  w     = SCREEN_W - 2 * MARGIN;
    const int16_t  cy    = oy + ROW_H / 2 + 1;
    const uint16_t fill  = row % 2 ? COLOR_PANEL : COLOR_BG;
    const size_t   index = (size_t)tablePage * ROWS_PER_PAGE + row;
    if (fill != COLOR_BG) g.fillSmoothRoundRect(ox, oy, w, ROW_H, 6, fill, COLOR_BG);

    if (index >= lanDeviceCount()) {
      if (row == 0 && !lanScanRunning()) {
        const char *msg = linkState() != LinkState::Online ? "Connect to Wi-Fi to scan the network"
                                                           : "No devices yet - tap Rescan";
        text(g, msg, ox + 12, cy, Font::UiSm, COLOR_SUBTLE, fill, ML_DATUM);
      }
      return;
    }
    const LanDevice &d = lanDevice(index);
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", d.mac[0], d.mac[1], d.mac[2],
             d.mac[3], d.mac[4], d.mac[5]);
    text(g, IPAddress(d.ip).toString().c_str(), ox + 12, cy, Font::MonoSm, COLOR_TEXT, fill,
         ML_DATUM);
    text(g, mac, ox + COL_MAC_X, cy, Font::MonoSm, COLOR_SUBTLE, fill, ML_DATUM);
    text(g, d.kind, ox + COL_TYPE_X, cy, Font::UiSm, kindColor(d.kind), fill, ML_DATUM);
  });
}

static uint32_t deviceListSig() {
  uint32_t sig = lanDeviceCount() * 2654435761UL + tablePage;
  for (size_t i = 0; i < lanDeviceCount(); i++) sig = sig * 31 + lanDevice(i).ip;
  return sig;
}

static void drawRows() {
  tableSig = deviceListSig();
  for (uint8_t r = 0; r < ROWS_PER_PAGE; r++) drawRow(r);
}

static void drawNetwork() {
  tft.fillScreen(COLOR_BG);
  subHeader("Network");
  drawNetworkButtons();
  headerRule(tft);
  drawStats();
  drawTableHeader();
  drawRows();
  drawFooter();
}

static void tapNetwork(uint16_t x, uint16_t y) {
  if (hitBack(x, y)) {
    uiShow(Screen::Dashboard);
  } else if (y < HEADER_H && x >= BTN_FLIP_X && x < BTN_FLIP_X + BTN_FLIP_W) {
    displayFlip();
    uiShow(Screen::Network);
  } else if (y < HEADER_H && x >= BTN_SCAN_X && x < BTN_SCAN_X + BTN_SCAN_W) {
    if (!lanScanRunning()) {
      tablePage = 0;
      lanScanStart();
    }
  } else if (y < HEADER_H && x >= BTN_ADD_X) {
    linkOpenPortal();
    uiShow(Screen::Setup);
  } else if (y >= TABLE_Y && y < FOOTER_Y && pageCount() > 1) {
    tablePage = (tablePage + 1) % pageCount();
    drawTableHeader();
    drawRows();
  }
}

// ---------------------------------------------------------------------------
// Setup screen (captive portal instructions)
// ---------------------------------------------------------------------------
static TFT_eSPI *qrTarget;
static int16_t   qrX, qrY, qrBox;

static void qrDraw(esp_qrcode_handle_t qr) {
  const int size   = esp_qrcode_get_size(qr);
  const int scale  = max(1, qrBox / (size + 4));  // leave a 2-module quiet zone
  const int offset = (qrBox - size * scale) / 2;
  for (int my = 0; my < size; my++) {
    for (int mx = 0; mx < size; mx++) {
      if (esp_qrcode_get_module(qr, mx, my)) {
        qrTarget->fillRect(qrX + offset + mx * scale, qrY + offset + my * scale, scale, scale,
                           COLOR_BG);
      }
    }
  }
}

static void drawQr(int16_t x, int16_t y, int16_t box) {
  tft.fillSmoothRoundRect(x, y, box, box, CARD_RADIUS, COLOR_TEXT, COLOR_BG);
  const String payload = "WIFI:T:WPA;S:" + linkPortalSsid() + ";P:" + linkPortalPass() + ";;";
  qrTarget = &tft;
  qrX = x + 6;
  qrY = y + 6;
  qrBox = box - 12;
  esp_qrcode_config_t cfg;
  cfg.display_func        = qrDraw;
  cfg.max_qrcode_version  = 6;
  cfg.qrcode_ecc_level    = ESP_QRCODE_ECC_LOW;
  if (esp_qrcode_generate(&cfg, payload.c_str()) != ESP_OK) {
    Serial.println(F("[UI  ] QR generation failed"));
  }
}

static void drawSetupPill() {
  offscreen(220, 13, SCREEN_W - MARGIN - 220, 22, [](TFT_eSPI &g, int16_t ox, int16_t oy) {
    const int16_t w = SCREEN_W - MARGIN - 220;
    Tone t;
    if (!linkPortalActive())          t = {"CLOSED", COLOR_SUBTLE, COLOR_PANEL_HI};
    else if (linkPortalClients() > 0) t = {"PHONE CONNECTED", COLOR_MATRIX, COLOR_GREEN_TINT};
    else                              t = {"WAITING FOR PHONE", COLOR_CYAN, COLOR_CYAN_TINT};
    useFont(g, Font::UiSm);
    const int16_t pw = 37 + g.textWidth(t.label);
    pill(g, ox + w - pw, oy, t.label, t.color, t.tint, COLOR_BG);
  });
}

static void step(int16_t x, int16_t y, const char *num, const char *title) {
  tft.fillSmoothCircle(x + 11, y + 10, 11, COLOR_CYAN_TINT, COLOR_BG);
  text(tft, num, x + 11, y + 11, Font::UiSm, COLOR_CYAN, COLOR_CYAN_TINT, MC_DATUM);
  text(tft, title, x + 32, y + 2, Font::UiMd, COLOR_TEXT, COLOR_BG);
}

static void drawSetup() {
  tft.fillScreen(COLOR_BG);
  subHeader("Wi-Fi Setup");
  drawSetupPill();
  headerRule(tft);

  const int16_t qrSize = 196;
  drawQr(MARGIN, CONTENT_Y, qrSize);

  const int16_t x  = MARGIN + qrSize + 18;
  const int16_t cw = SCREEN_W - MARGIN - x - 32;  // detail card width
  step(x, CONTENT_Y + 2, "1", "Scan the code, or join:");
  card(tft, x + 32, CONTENT_Y + 28, cw, 52, 8, COLOR_PANEL, COLOR_EDGE, COLOR_BG);
  text(tft, "Network", x + 44, CONTENT_Y + 37, Font::UiSm, COLOR_SUBTLE, COLOR_PANEL);
  text(tft, linkPortalSsid().c_str(), x + 32 + cw - 12, CONTENT_Y + 37, Font::MonoSm, COLOR_TEXT,
       COLOR_PANEL, TR_DATUM);
  text(tft, "Password", x + 44, CONTENT_Y + 58, Font::UiSm, COLOR_SUBTLE, COLOR_PANEL);
  text(tft, linkPortalPass().c_str(), x + 32 + cw - 12, CONTENT_Y + 58, Font::MonoSm, COLOR_CYAN,
       COLOR_PANEL, TR_DATUM);

  step(x, CONTENT_Y + 94, "2", "Sign-in page opens");
  text(tft, "Pick your hotspot, enter its", x + 32, CONTENT_Y + 118, Font::UiSm, COLOR_SUBTLE,
       COLOR_BG);
  text(tft, "password, and tap Save.", x + 32, CONTENT_Y + 134, Font::UiSm, COLOR_SUBTLE, COLOR_BG);

  step(x, CONTENT_Y + 158, "3", "Hub connects itself");
  text(tft, "It remembers every network.", x + 32, CONTENT_Y + 182, Font::UiSm,
       COLOR_SUBTLE, COLOR_BG);

  const int16_t hintY = CONTENT_Y + qrSize + 8;
  tft.fillSmoothRoundRect(MARGIN, hintY, SCREEN_W - 2 * MARGIN, 28, 8, COLOR_AMBER_TINT, COLOR_BG);
  text(tft, "2.4 GHz only  ·  iPhone: turn on Maximize Compatibility",
       MARGIN + 12, hintY + 15, Font::UiSm, COLOR_AMBER, COLOR_AMBER_TINT, ML_DATUM);

  drawFooter();
}

static void tapSetup(uint16_t x, uint16_t y) {
  if (!hitBack(x, y)) return;
  // Leave an auto-opened portal running while there's no network; close a manual one.
  if (linkState() == LinkState::Online) linkClosePortal();
  uiShow(Screen::Dashboard);
}

// ---------------------------------------------------------------------------
// PortalBox screen (opened by holding the WIFI PENTESTER card)
// ---------------------------------------------------------------------------
static const int16_t  PB_CARD_H  = 118;
static const int16_t  PB_BTN_Y   = 188;
static const int16_t  PB_BTN_H   = 38;
static const int16_t  PB_BTN_W   = 140;
static const int16_t  PB_ROW_Y[4] = {34, 56, 78, 100};
static uint32_t       seenPbRev  = 0;
static bool           lastPbLive = false;

static void pbRow(TFT_eSPI &g, int16_t ox, int16_t oy, int16_t w, int16_t y, const char *label,
                  const String &value, uint16_t valueColor) {
  text(g, label, ox + 14, oy + y, Font::UiSm, COLOR_SUBTLE, COLOR_PANEL);
  text(g, fitText(g, value, Font::MonoSm, w - 100).c_str(), ox + w - 14, oy + y, Font::MonoSm,
       valueColor, COLOR_PANEL, TR_DATUM);
}

static void drawPbApCard() {
  offscreen(MARGIN, CONTENT_Y, CARD_W, PB_CARD_H, [](TFT_eSPI &g, int16_t ox, int16_t oy) {
    card(g, ox, oy, CARD_W, PB_CARD_H, CARD_RADIUS, COLOR_PANEL, COLOR_EDGE, COLOR_BG);
    text(g, "ACCESS POINT", ox + 14, oy + 10, Font::UiSm, COLOR_SUBTLE, COLOR_PANEL);
    g.drawFastHLine(ox + 14, oy + 27, CARD_W - 28, COLOR_EDGE);
    pbRow(g, ox, oy, CARD_W, PB_ROW_Y[0], "SSID", pbSsid(), COLOR_TEXT);
    pbRow(g, ox, oy, CARD_W, PB_ROW_Y[1], "ADDRESS", pbApIp(), COLOR_TEXT);
    pbRow(g, ox, oy, CARD_W, PB_ROW_Y[2], "CHANNEL", String((unsigned)pbChannel()), COLOR_TEXT);
    pbRow(g, ox, oy, CARD_W, PB_ROW_Y[3], "CLIENTS",
          pbActive() ? String((unsigned)pbClients()) : String("-"),
          pbActive() && pbClients() ? COLOR_MATRIX : COLOR_SUBTLE);
  });
}

static void drawPbStoreCard() {
  offscreen(COL_RIGHT_X, CONTENT_Y, CARD_W, PB_CARD_H, [](TFT_eSPI &g, int16_t ox, int16_t oy) {
    card(g, ox, oy, CARD_W, PB_CARD_H, CARD_RADIUS, COLOR_PANEL, COLOR_EDGE, COLOR_BG);
    text(g, "PORTAL LIBRARY", ox + 14, oy + 10, Font::UiSm, COLOR_SUBTLE, COLOR_PANEL);
    g.drawFastHLine(ox + 14, oy + 27, CARD_W - 28, COLOR_EDGE);
    pbRow(g, ox, oy, CARD_W, PB_ROW_Y[0], "STORE",
          String(pbBackendName()) + " · " + String((unsigned)pbPortalCount()) + " files", COLOR_TEXT);
    pbRow(g, ox, oy, CARD_W, PB_ROW_Y[1], "SERVING",
          pbSelectedPortal().length() ? pbSelectedPortal() : String("built-in"), COLOR_CYAN);
    pbRow(g, ox, oy, CARD_W, PB_ROW_Y[2], "CAPTURES",
          String((unsigned long)pbCaptureCount()), COLOR_TEXT);
    pbRow(g, ox, oy, CARD_W, PB_ROW_Y[3], "LAST",
          pbLastCaptureEmail().length() ? pbLastCaptureEmail() : String("-"), COLOR_SUBTLE);
  });
}

static void drawPbPill() {
  offscreen(220, 13, SCREEN_W - MARGIN - 220, 22, [](TFT_eSPI &g, int16_t ox, int16_t oy) {
    const int16_t w = SCREEN_W - MARGIN - 220;
    Tone t;
    if (!pbActive())           t = {"IDLE", COLOR_SUBTLE, COLOR_PANEL_HI};
    else if (pbClients() > 0)  t = {"CAPTURING", COLOR_MATRIX, COLOR_GREEN_TINT};
    else                       t = {"BROADCASTING", COLOR_CYAN, COLOR_CYAN_TINT};
    useFont(g, Font::UiSm);
    const int16_t pw = 37 + g.textWidth(t.label);
    pill(g, ox + w - pw, oy, t.label, t.color, t.tint, COLOR_BG);
  });
}

static void drawPbButtons() {
  const int16_t y2 = PB_BTN_Y + PB_BTN_H + 8;
  button(tft, 16, PB_BTN_Y, PB_BTN_W, PB_BTN_H, pbActive() ? "Stop Portal" : "Start Portal",
         pbActive());
  button(tft, 170, PB_BTN_Y, PB_BTN_W, PB_BTN_H, "Next Portal", false);
  button(tft, 324, PB_BTN_Y, PB_BTN_W, PB_BTN_H, "Sync Pages", false);
  button(tft, 16, y2, PB_BTN_W, PB_BTN_H, "Dump Log", false);
  button(tft, 170, y2, PB_BTN_W, PB_BTN_H, "Clear Log", false);
  button(tft, 324, y2, PB_BTN_W, PB_BTN_H, pbBeepEnabled() ? "Beep On" : "Beep Off",
         pbBeepEnabled());
}

// Status line plus the reminder that SSID and channel are console settings.
static void drawPbHint() {
  tft.fillRect(MARGIN, 280, SCREEN_W - 2 * MARGIN, 20, COLOR_BG);
  const char *hint  = pbActive() ? "AP live · hub Wi-Fi, LAN scan and fan polling paused"
                                 : "AP idle — start it when you're ready";
  const char *right = "pb ssid | pb ch on console";
  useFont(tft, Font::MonoSm);
  const int16_t rightW = tft.textWidth(right);
  const int16_t leftW  = SCREEN_W - 2 * MARGIN - rightW - 16;
  text(tft, fitText(tft, hint, Font::UiSm, leftW).c_str(), MARGIN, 284, Font::UiSm,
       pbActive() ? COLOR_AMBER : COLOR_SUBTLE, COLOR_BG);
  text(tft, right, SCREEN_W - MARGIN, 284, Font::MonoSm, COLOR_MUTED, COLOR_BG, TR_DATUM);
}

static void drawPb() {
  tft.fillScreen(COLOR_BG);
  subHeader("PortalBox");
  drawPbPill();
  headerRule(tft);
  drawPbApCard();
  drawPbStoreCard();
  drawPbButtons();
  drawPbHint();
  drawFooter();
}

static void tapPb(uint16_t x, uint16_t y) {
  if (hitBack(x, y)) {
    uiShow(Screen::Dashboard);
    return;
  }
  const int16_t y2 = PB_BTN_Y + PB_BTN_H + 8;
  if (y >= PB_BTN_Y && y < PB_BTN_Y + PB_BTN_H) {
    if (x >= 16 && x < 16 + PB_BTN_W) {
      if (pbActive()) pbStop(); else pbStart();
    } else if (x >= 170 && x < 170 + PB_BTN_W) {
      if (!pbSelectNextPortal()) Serial.println(F("[UI  ] No portal files to cycle"));
    } else if (x >= 324 && x < 324 + PB_BTN_W) {
      Serial.printf("[UI  ] Portal sync: %u page(s) copied\n", (unsigned)pbSyncFromFlash());
    } else {
      return;
    }
    drawPb();
  } else if (y >= y2 && y < y2 + PB_BTN_H) {
    if (x >= 16 && x < 16 + PB_BTN_W) {
      pbDumpCaptures(Serial);
    } else if (x >= 170 && x < 170 + PB_BTN_W) {
      pbClearCaptures();
    } else if (x >= 324 && x < 324 + PB_BTN_W) {
      pbSetBeep(!pbBeepEnabled());
    } else {
      return;
    }
    drawPb();
  }
}

// ---------------------------------------------------------------------------
// Controller
// ---------------------------------------------------------------------------
void uiShow(Screen next) {
  screen      = next;
  pressedCard = -1;
  seenLinkRev = linkRevision();
  seenFanRev  = fanLinkRevision();
  seenPbRev   = pbRevision();
  lastPbLive  = pbActive();
  nextClockMs = millis() + CLOCK_REFRESH_MS;
  nextStatsMs = millis() + STATS_REFRESH_MS;
  lastClients = linkPortalClients();
  Serial.printf("[UI  ] Screen -> %s\n", uiScreenName());

  switch (screen) {
    case Screen::Dashboard: drawDashboard(); break;
    case Screen::Setup:     drawSetup(); break;
    case Screen::Network:
      tablePage = 0;
      if (linkState() == LinkState::Online && !lanScanRunning() &&
          (lanScanFinishedMs() == 0 || millis() - lanScanFinishedMs() > LAN_STALE_MS)) {
        lanScanStart();
      }
      lastScanRunning = lanScanRunning();
      lanScanChanged();  // the full draw below covers it
      drawNetwork();
      break;
    case Screen::Fan:
      if (fanLinkHasSnapshot()) {
        fanTargetC = fanLinkTargetC();
        fanDutyPct = fanLinkManualDutyPct();
      }
      drawFan();
      break;
    case Screen::PortalBox: drawPb(); break;
  }
}

void uiBegin() {
  uiShow(linkPortalActive() && linkSavedCount() == 0 ? Screen::Setup : Screen::Dashboard);
}

Screen uiScreen() { return screen; }

const char *uiScreenName() {
  switch (screen) {
    case Screen::Dashboard: return "dashboard";
    case Screen::Network:   return "network";
    case Screen::Setup:     return "setup";
    case Screen::Fan:       return "fan";
    case Screen::PortalBox: return "portalbox";
  }
  return "";
}

void uiTap(uint16_t x, uint16_t y) {
  Serial.printf("[TOUCH] Tap at x=%u y=%u\n", x, y);
  if (pressedCard >= 0) return;  // a card press is still animating
  switch (screen) {
    case Screen::Dashboard: tapDashboard(x, y); break;
    case Screen::Network:   tapNetwork(x, y); break;
    case Screen::Setup:     tapSetup(x, y); break;
    case Screen::Fan:       tapFan(x, y); break;
    case Screen::PortalBox: tapPb(x, y); break;
  }
}

void uiTick(uint32_t now) {
  // WIFI PENTESTER opens on tap-tap-hold: two quick taps then a long press.
  // A stray tap, a single tap, or a bare hold never opens it.
  if (pressedCard >= 0) {
    const uint32_t held = now - pressedAtMs;
    if (pressedCard == (int8_t)CARD_PENTEST) {
      if (touchDown()) {
        if (held >= LONG_PRESS_MS && !longPressDone) {
          longPressDone = true;
          if (pentestTaps >= 2) {  // tap-tap-hold completed
            const int8_t card = pressedCard;
            pressedCard = -1;
            pentestTaps = 0;
            drawCard((size_t)card);
            Serial.println(F("[UI  ] tap-tap-hold on WIFI PENTESTER - opening PortalBox"));
            uiShow(Screen::PortalBox);
            return;
          }
          // Held long enough but with too few taps: keep the highlight, don't open.
        }
      } else {  // released
        if (held < LONG_PRESS_MS) {
          // A quick release counts as a tap in the sequence.
          if (pentestTaps == 0 || now - pentestTapUpMs > TAP_WINDOW_MS) pentestTaps = 1;
          else if (pentestTaps < 2) pentestTaps++;
          pentestTapUpMs = now;
        } else {
          pentestTaps = 0;  // a full hold that didn't unlock resets the sequence
        }
        const int8_t card = pressedCard;
        pressedCard = -1;
        longPressDone = false;
        drawCard((size_t)card);
      }
    } else if (held >= PRESS_FEEDBACK_MS) {
      releaseCard();
    }
  }

  const bool linkChanged = linkRevision() != seenLinkRev;
  seenLinkRev            = linkRevision();
  const bool fanChanged  = fanLinkRevision() != seenFanRev;
  seenFanRev             = fanLinkRevision();

  // A portal that opened on its own (no network found) takes over the screen;
  // when it closes, leave the setup screen.
  if (linkChanged) {
    if (screen == Screen::Setup && !linkPortalActive()) {
      uiShow(Screen::Dashboard);
      return;
    }
    if (screen == Screen::Dashboard && linkPortalActive() && linkState() != LinkState::Online &&
        linkSavedCount() == 0) {
      uiShow(Screen::Setup);
      return;
    }
  }

  const bool clock = (int32_t)(now - nextClockMs) >= 0;
  if (clock) nextClockMs = now + CLOCK_REFRESH_MS;

  switch (screen) {
    case Screen::Dashboard: {
      if (fanChanged) drawCard(0);
      if (linkChanged) {
        drawDashboardPill();
        drawCard(CARD_SYSTEM);
      } else if (lanScanChanged() && !lanScanRunning()) {
        drawCard(CARD_SYSTEM);  // device count
      }
      if (pbRevision() != seenPbRev) {
        seenPbRev = pbRevision();
        drawCard(CARD_PENTEST);
        drawDashboardPill();
      }
      if (clock) {
        drawClock();
        drawFooter();
      }
      break;
    }

    case Screen::Network: {
      const bool running = lanScanRunning();
      if (running != lastScanRunning) {
        lastScanRunning = running;
        drawNetworkButtons();
      }
      bool stats = linkChanged || (int32_t)(now - nextStatsMs) >= 0;
      if (lanScanChanged()) {
        if (tablePage >= pageCount()) tablePage = 0;
        const bool listChanged = deviceListSig() != tableSig;
        drawTableHeader();
        if (listChanged || !running) drawRows();
        stats = stats || listChanged || !running;
      }
      if (stats) {
        nextStatsMs = now + STATS_REFRESH_MS;
        drawStats();
      }
      if (linkChanged && !running) drawRows();
      if (clock) drawFooter();
      break;
    }

    case Screen::Setup: {
      const uint8_t clients = linkPortalClients();
      if (linkChanged || clients != lastClients) {
        lastClients = clients;
        drawSetupPill();
        drawFooter();
      }
      break;
    }
    case Screen::Fan: { if (fanChanged) drawFan(); break; }

    case Screen::PortalBox: {
      const bool live = pbActive();
      if (pbRevision() != seenPbRev) {
        seenPbRev = pbRevision();
        drawPbPill();
        drawPbApCard();
        drawPbStoreCard();
        drawFooter();
      }
      if (live != lastPbLive) {  // the button face follows the AP state
        lastPbLive = live;
        drawPbButtons();
        drawPbHint();
      }
      break;
    }
  }
}
