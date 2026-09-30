#include "gfx.h"

#include <Preferences.h>

#include "theme.h"
#include "fonts/FontMonoMd.h"
#include "fonts/FontMonoSm.h"
#include "fonts/FontUiLg.h"
#include "fonts/FontUiMd.h"
#include "fonts/FontUiSm.h"
#include "fonts/FontUiXl.h"

TFT_eSPI tft = TFT_eSPI();

// ---------------------------------------------------------------------------
// Orientation (landscape only: rotation 1 = USB right, 3 = 180° inverted)
// ---------------------------------------------------------------------------
static const char *ORIENT_NS  = "display";
static bool        orientFlip = false;

void displayBegin() {
  Preferences prefs;
  prefs.begin(ORIENT_NS, false);
  orientFlip = prefs.getBool("flip", false);
  prefs.end();
  tft.setRotation(orientFlip ? 3 : 1);
}

bool displayFlipped() { return orientFlip; }

void displayFlip() {
  orientFlip = !orientFlip;
  Preferences prefs;
  prefs.begin(ORIENT_NS, false);
  prefs.putBool("flip", orientFlip);
  prefs.end();
  tft.setRotation(orientFlip ? 3 : 1);
  Serial.printf("[TFT ] Rotation -> %s\n", orientFlip ? "landscape inverted" : "landscape");
}

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------
static const uint8_t *fontData(Font font) {
  switch (font) {
    case Font::UiSm:   return FontUiSm;
    case Font::UiMd:   return FontUiMd;
    case Font::UiLg:   return FontUiLg;
    case Font::UiXl:   return FontUiXl;
    case Font::MonoSm: return FontMonoSm;
    case Font::MonoMd: return FontMonoMd;
  }
  return FontUiSm;
}

void useFont(TFT_eSPI &g, Font font) {
  const uint8_t *data = fontData(font);
  if (g.fontLoaded && g.gFont.gArray == data) return;
  if (g.fontLoaded) g.unloadFont();
  g.loadFont(data);
}

int16_t text(TFT_eSPI &g, const char *s, int32_t x, int32_t y, Font font,
             uint16_t fg, uint16_t bg, uint8_t datum) {
  useFont(g, font);
  g.setTextDatum(datum);
  g.setTextColor(fg, bg);
  return g.drawString(s, x, y);
}

String fitText(TFT_eSPI &g, const String &s, Font font, int16_t maxW) {
  useFont(g, font);
  if (g.textWidth(s) <= maxW) return s;
  String out = s;
  while (out.length() > 0 && g.textWidth(out + "…") > maxW) {
    out.remove(out.length() - 1);
  }
  return out + "…";
}

// ---------------------------------------------------------------------------
// Surfaces
// ---------------------------------------------------------------------------
void card(TFT_eSPI &g, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
          uint16_t fill, uint16_t border, uint16_t outside) {
  g.fillSmoothRoundRect(x, y, w, h, r, border, outside);
  g.fillSmoothRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, fill, border);
}

int16_t pill(TFT_eSPI &g, int32_t x, int32_t y, const char *label,
             uint16_t color, uint16_t tint, uint16_t outside) {
  useFont(g, Font::UiSm);
  const int16_t h = 22;
  const int16_t w = 26 + g.textWidth(label) + 11;
  g.fillSmoothRoundRect(x, y, w, h, h / 2, tint, outside);
  g.fillSmoothCircle(x + 14, y + h / 2, 3, color, tint);
  text(g, label, x + 25, y + h / 2 + 1, Font::UiSm, color, tint, ML_DATUM);
  return w;
}

void button(TFT_eSPI &g, int32_t x, int32_t y, int32_t w, int32_t h, const char *label,
            bool active) {
  if (active) {
    g.fillSmoothRoundRect(x, y, w, h, 8, COLOR_CYAN, COLOR_BG);
    text(g, label, x + w / 2, y + h / 2 + 1, Font::UiSm, COLOR_BG, COLOR_CYAN, MC_DATUM);
  } else {
    card(g, x, y, w, h, 8, COLOR_BG, COLOR_CYAN);
    text(g, label, x + w / 2, y + h / 2 + 1, Font::UiSm, COLOR_CYAN, COLOR_BG, MC_DATUM);
  }
}

void iconButton(TFT_eSPI &g, int32_t x, int32_t y, int32_t w, int32_t h, Icon icon,
                bool active) {
  if (active) {
    g.fillSmoothRoundRect(x, y, w, h, 8, COLOR_CYAN, COLOR_BG);
    iconGlyph(g, x + w / 2, y + h / 2, icon, COLOR_BG, COLOR_CYAN);
  } else {
    card(g, x, y, w, h, 8, COLOR_BG, COLOR_CYAN);
    iconGlyph(g, x + w / 2, y + h / 2, icon, COLOR_CYAN, COLOR_BG);
  }
}

void signalBars(TFT_eSPI &g, int32_t x, int32_t y, uint8_t level, uint16_t on, uint16_t off) {
  for (uint8_t i = 0; i < 4; i++) {
    const int16_t h = 4 + i * 3;
    g.fillRect(x + i * 5, y - h, 3, h, i < level ? on : off);
  }
}

void headerRule(TFT_eSPI &g) {
  g.fillRectHGradient(0, RULE_Y, SCREEN_W, RULE_H, COLOR_CYAN, COLOR_CYAN_DEEP);
}

void logoMark(TFT_eSPI &g, int32_t x, int32_t y) {
  g.fillSmoothRoundRect(x, y, 24, 24, 6, COLOR_CYAN, COLOR_BG);
  g.fillSmoothRoundRect(x + 3, y + 3, 18, 18, 4, COLOR_BG, COLOR_CYAN);
  g.fillSmoothRoundRect(x + 8, y + 8, 8, 8, 2, COLOR_CYAN, COLOR_BG);
}

// ---------------------------------------------------------------------------
// Icons (line style, ~20px, 2px strokes)
// ---------------------------------------------------------------------------
static void stroke(TFT_eSPI &g, float ax, float ay, float bx, float by, uint16_t fg, uint16_t bg,
                   float w = 2.0f) {
  g.drawWideLine(ax, ay, bx, by, w, fg, bg);
}

void iconGlyph(TFT_eSPI &g, int32_t cx, int32_t cy, Icon icon, uint16_t fg, uint16_t bg) {
  switch (icon) {
    case Icon::Power:
      // Angles are clockwise from 6 o'clock; leave a gap around 12 o'clock.
      g.drawSmoothArc(cx, cy + 1, 8, 6, 220, 140, fg, bg, true);
      stroke(g, cx, cy - 9, cx, cy - 1, fg, bg);
      break;

    case Icon::Garage:
      stroke(g, cx - 10, cy - 2, cx, cy - 9, fg, bg);
      stroke(g, cx, cy - 9, cx + 10, cy - 2, fg, bg);
      stroke(g, cx - 7, cy - 3, cx - 7, cy + 8, fg, bg);
      stroke(g, cx + 7, cy - 3, cx + 7, cy + 8, fg, bg);
      stroke(g, cx - 4, cy + 1, cx + 4, cy + 1, fg, bg, 1.5f);
      stroke(g, cx - 4, cy + 4, cx + 4, cy + 4, fg, bg, 1.5f);
      stroke(g, cx - 4, cy + 7, cx + 4, cy + 7, fg, bg, 1.5f);
      break;

    case Icon::Wifi:
      g.drawSmoothArc(cx, cy + 7, 13, 11, 135, 225, fg, bg, true);
      g.drawSmoothArc(cx, cy + 7, 9, 7, 135, 225, fg, bg, true);
      g.drawSmoothArc(cx, cy + 7, 5, 3, 135, 225, fg, bg, true);
      g.drawSpot(cx, cy + 7, 1.6f, fg, bg);
      break;

    case Icon::Pulse:
      stroke(g, cx - 10, cy, cx - 5, cy, fg, bg);
      stroke(g, cx - 5, cy, cx - 2, cy - 7, fg, bg);
      stroke(g, cx - 2, cy - 7, cx + 2, cy + 7, fg, bg);
      stroke(g, cx + 2, cy + 7, cx + 5, cy, fg, bg);
      stroke(g, cx + 5, cy, cx + 10, cy, fg, bg);
      break;

    case Icon::Back:
      stroke(g, cx + 3, cy - 7, cx - 4, cy, fg, bg, 2.5f);
      stroke(g, cx - 4, cy, cx + 3, cy + 7, fg, bg, 2.5f);
      break;

    case Icon::Plus:
      stroke(g, cx - 7, cy, cx + 7, cy, fg, bg);
      stroke(g, cx, cy - 7, cx, cy + 7, fg, bg);
      break;

    case Icon::Rotate:
      // Two circular arrows. Angles are clockwise from 6 o'clock.
      g.drawSmoothArc(cx, cy, 9, 7, 135, 225, fg, bg, false);  // top arc
      g.drawSmoothArc(cx, cy, 9, 7, 315, 45, fg, bg, false);   // bottom arc
      // Arrowheads at the clockwise-travelling ends.
      stroke(g, cx + 9, cy - 9, cx + 6, cy - 6, fg, bg);  // top-right (points right)
      stroke(g, cx + 6, cy - 6, cx + 9, cy - 3, fg, bg);
      stroke(g, cx - 9, cy + 9, cx - 6, cy + 6, fg, bg);  // bottom-left (points left)
      stroke(g, cx - 6, cy + 6, cx - 9, cy + 3, fg, bg);
      break;
  }
}

void iconBadge(TFT_eSPI &g, int32_t x, int32_t y, Icon icon, uint16_t fg, uint16_t tint,
               uint16_t outside) {
  g.fillSmoothRoundRect(x, y, 32, 32, 8, tint, outside);
  iconGlyph(g, x + 16, y + 16, icon, fg, tint);
}
