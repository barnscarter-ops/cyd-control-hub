// Control Hub — minimalist terminal dashboard for the 3.5" CYD
// Hardware: ESP32-WROOM-32E + ST7796 (320x480), driven by TFT_eSPI.
// All display and touch config lives in platformio.ini build_flags.

#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>

// ---------------------------------------------------------------------------
// Palette (RGB565)
// ---------------------------------------------------------------------------
static const uint16_t COLOR_BG     = 0x0000;  // Pure black background
static const uint16_t COLOR_TEXT   = 0xFFFF;  // Pure white primary labels
static const uint16_t COLOR_MUTED  = 0x5AEB;  // Slate gray wireframes
static const uint16_t COLOR_CYAN   = 0x07FF;  // Electric cyan structural accents
static const uint16_t COLOR_MATRIX = 0x2E44;  // Matrix green live statuses

// ---------------------------------------------------------------------------
// Layout (landscape, 480x320)
// ---------------------------------------------------------------------------
static const int16_t HEADER_LINE_Y = 40;

static const int16_t COL_LEFT_X  = 20;
static const int16_t COL_RIGHT_X = 250;
static const int16_t ROW_TOP_Y   = 60;
static const int16_t ROW_BOT_Y   = 185;
static const int16_t TILE_W      = 210;
static const int16_t TILE_H      = 105;
static const int16_t TILE_PAD    = 10;
static const int16_t FONT2_H     = 16;

struct Tile {
  int16_t x;
  int16_t y;
  const char *title;
  const char *status;
};

static Tile tiles[] = {
  { COL_LEFT_X,  ROW_TOP_Y, "SERVER FAN",     "OFF"    },
  { COL_RIGHT_X, ROW_TOP_Y, "GARAGE CTRL",    "CLOSED" },
  { COL_LEFT_X,  ROW_BOT_Y, "WIFI PENTESTER", "READY"  },
  { COL_RIGHT_X, ROW_BOT_Y, "SYSTEM STATUS",  "SECURE" },
};
static const size_t TILE_COUNT = sizeof(tiles) / sizeof(tiles[0]);

// ---------------------------------------------------------------------------
// Timing (non-blocking)
// ---------------------------------------------------------------------------
static const uint32_t HEARTBEAT_INTERVAL_MS  = 5000;
static const uint32_t TOUCH_POLL_INTERVAL_MS = 30;
static const uint32_t TAP_HIGHLIGHT_MS       = 150;

static uint32_t lastHeartbeatMs = 0;
static uint32_t lastTouchPollMs = 0;

// ---------------------------------------------------------------------------
// Touch state
// ---------------------------------------------------------------------------
static const char  *PREFS_NAMESPACE = "touch";
static const char  *PREFS_CAL_KEY   = "cal";
static const size_t CAL_LEN         = 5;

static bool     touchWasDown     = false;
static int      highlightedTile  = -1;
static uint32_t highlightStartMs = 0;

TFT_eSPI tft = TFT_eSPI();
Preferences prefs;

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

// Draws a GLCD (font 1) '+' whose optical center lands exactly on (cx, cy).
// The glyph's bars sit at column 2 / row 3 of its 6x8 cell.
static void drawAnchor(int16_t cx, int16_t cy) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_CYAN);  // transparent background
  tft.drawString("+", cx - 2, cy - 3, 1);
}

static void drawHeader() {
  tft.setTextDatum(TL_DATUM);

  tft.setTextColor(COLOR_CYAN, COLOR_BG);
  const int16_t titleX = COL_LEFT_X;
  const int16_t titleY = 8;
  tft.drawString("HOME HUB", titleX, titleY, 4);

  const int16_t titleW = tft.textWidth("HOME HUB", 4);
  tft.setTextColor(COLOR_MATRIX, COLOR_BG);
  tft.drawString("[ ONLINE ]", titleX + titleW + 12, titleY + 6, 2);

  tft.drawFastHLine(0, HEADER_LINE_Y, tft.width(), COLOR_CYAN);

  // Structural anchors where the header rule meets the grid's column edges.
  drawAnchor(COL_LEFT_X, HEADER_LINE_Y);
  drawAnchor(COL_LEFT_X + TILE_W, HEADER_LINE_Y);
  drawAnchor(COL_RIGHT_X, HEADER_LINE_Y);
  drawAnchor(COL_RIGHT_X + TILE_W, HEADER_LINE_Y);
}

static void drawTileStatus(const Tile &t) {
  const int16_t sx = t.x + TILE_PAD;
  const int16_t sy = t.y + TILE_H - TILE_PAD - FONT2_H;
  tft.fillRect(sx, sy, TILE_W - 2 * TILE_PAD, FONT2_H, COLOR_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_MATRIX, COLOR_BG);
  tft.drawString(t.status, sx, sy, 2);
}

static void drawTile(const Tile &t) {
  tft.drawRect(t.x, t.y, TILE_W, TILE_H, COLOR_MUTED);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawString(t.title, t.x + TILE_PAD, t.y + TILE_PAD, 2);

  drawTileStatus(t);
}

static void drawGrid() {
  for (size_t i = 0; i < TILE_COUNT; i++) {
    drawTile(tiles[i]);
  }

  // Anchor at the center of the gutter crossing between all four tiles.
  const int16_t gutterX = (COL_LEFT_X + TILE_W + COL_RIGHT_X) / 2;
  const int16_t gutterY = (ROW_TOP_Y + TILE_H + ROW_BOT_Y) / 2;
  drawAnchor(gutterX, gutterY);
}

// Update a tile's live status and redraw only its status line.
// Hook point for future sensor / MQTT / JSON-driven updates.
void setTileStatus(size_t index, const char *status) {
  if (index >= TILE_COUNT) return;
  tiles[index].status = status;
  drawTileStatus(tiles[index]);
}

// ---------------------------------------------------------------------------
// Touch (XPT2046 via TFT_eSPI)
// ---------------------------------------------------------------------------

// Interactive 4-corner calibration. Blocking by design: only runs from setup().
static void runTouchCalibration(uint16_t *cal) {
  tft.fillScreen(COLOR_BG);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_CYAN, COLOR_BG);
  tft.drawString("TOUCH CALIBRATION", tft.width() / 2, tft.height() / 2 - 12, 4);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawString("Tap each corner marker as it appears", tft.width() / 2, tft.height() / 2 + 20, 2);

  Serial.println(F("[TOUCH] Calibration started - tap the corner markers"));
  tft.calibrateTouch(cal, COLOR_CYAN, COLOR_BG, 15);

  prefs.putBytes(PREFS_CAL_KEY, cal, CAL_LEN * sizeof(uint16_t));
  Serial.printf("[TOUCH] Calibration saved: %u %u %u %u %u\n",
                cal[0], cal[1], cal[2], cal[3], cal[4]);

  tft.fillScreen(COLOR_BG);
  digitalWrite(TFT_BL, !TFT_BACKLIGHT_ON);
}

// Loads stored calibration, or calibrates if none exists or the screen is
// held down during boot (forces re-calibration).
static void initTouch() {
  pinMode(TOUCH_IRQ, INPUT);
  prefs.begin(PREFS_NAMESPACE, false);

  uint16_t cal[CAL_LEN];
  const bool haveCal    = prefs.getBytes(PREFS_CAL_KEY, cal, sizeof(cal)) == sizeof(cal);
  const bool heldAtBoot = digitalRead(TOUCH_IRQ) == LOW;

  if (!haveCal || heldAtBoot) {
    if (heldAtBoot) Serial.println(F("[TOUCH] Screen held at boot - forcing re-calibration"));
    runTouchCalibration(cal);
  } else {
    Serial.printf("[TOUCH] Loaded calibration: %u %u %u %u %u\n",
                  cal[0], cal[1], cal[2], cal[3], cal[4]);
  }

  tft.setTouch(cal);
  Serial.println(F("[TOUCH] XPT2046 ready"));
}

static int tileAt(uint16_t x, uint16_t y) {
  for (size_t i = 0; i < TILE_COUNT; i++) {
    const Tile &t = tiles[i];
    if (x >= t.x && x < t.x + TILE_W && y >= t.y && y < t.y + TILE_H) return (int)i;
  }
  return -1;
}

static void onTileTapped(int index) {
  Serial.printf("[TOUCH] Tap: %s\n", tiles[index].title);

  if (highlightedTile >= 0) {
    const Tile &prev = tiles[highlightedTile];
    tft.drawRect(prev.x, prev.y, TILE_W, TILE_H, COLOR_MUTED);
  }
  const Tile &t = tiles[index];
  tft.drawRect(t.x, t.y, TILE_W, TILE_H, COLOR_CYAN);
  highlightedTile  = index;
  highlightStartMs = millis();

  // Future: toggle the device behind this tile and call setTileStatus().
}

static void pollTouch(uint32_t now) {
  if (now - lastTouchPollMs < TOUCH_POLL_INTERVAL_MS) return;
  lastTouchPollMs = now;

  // IRQ is LOW only while pressed; skip the SPI read otherwise.
  uint16_t x, y;
  const bool down = digitalRead(TOUCH_IRQ) == LOW && tft.getTouch(&x, &y);

  if (down && !touchWasDown) {  // fire once per press
    const int index = tileAt(x, y);
    if (index >= 0) onTileTapped(index);
  }
  touchWasDown = down;
}

static void updateHighlight(uint32_t now) {
  if (highlightedTile < 0 || now - highlightStartMs < TAP_HIGHLIGHT_MS) return;
  const Tile &t = tiles[highlightedTile];
  tft.drawRect(t.x, t.y, TILE_W, TILE_H, COLOR_MUTED);
  highlightedTile = -1;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);  // let USB-serial settle; one-time, before the loop starts
  Serial.println();
  Serial.println(F("[BOOT] Control Hub starting"));

  tft.init();  // also drives TFT_BL on; we blank it until the first frame is drawn
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, !TFT_BACKLIGHT_ON);
  tft.setRotation(1);
  tft.fillScreen(COLOR_BG);
  Serial.printf("[TFT ] ST7796 init OK, rotation=1, %dx%d\n", tft.width(), tft.height());
  Serial.printf("[TFT ] SPI pins MOSI=%d MISO=%d SCLK=%d CS=%d DC=%d RST=%d BL=%d\n",
                TFT_MOSI, TFT_MISO, TFT_SCLK, TFT_CS, TFT_DC, TFT_RST, TFT_BL);

  initTouch();

  drawHeader();
  drawGrid();
  Serial.printf("[UI  ] Dashboard rendered (%u tiles)\n", (unsigned)TILE_COUNT);

  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
  Serial.println(F("[TFT ] Backlight ON"));
  Serial.println(F("[BOOT] Display initialization sequence complete"));

  lastHeartbeatMs = millis();
}

// ---------------------------------------------------------------------------
// Loop — keep non-blocking: no delay(), poll with millis() timers.
// ---------------------------------------------------------------------------
void loop() {
  const uint32_t now = millis();

  if (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeatMs = now;
    Serial.printf("[HB  ] uptime=%lus heap=%u\n",
                  (unsigned long)(now / 1000), (unsigned)ESP.getFreeHeap());
  }

  pollTouch(now);
  updateHighlight(now);

  // Future: poll sensors / network here and call setTileStatus() on change.
}
