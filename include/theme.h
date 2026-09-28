// Visual design tokens: palette, geometry, and timing shared by all screens.
#pragma once

#include <stdint.h>

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// ---------------------------------------------------------------------------
// Palette (RGB565)
// ---------------------------------------------------------------------------
// Core brand colors
constexpr uint16_t COLOR_BG     = 0x0000;  // Pure black background
constexpr uint16_t COLOR_TEXT   = 0xFFFF;  // Pure white primary labels
constexpr uint16_t COLOR_MUTED  = 0x5AEB;  // Slate gray wireframes / tertiary text
constexpr uint16_t COLOR_CYAN   = 0x07FF;  // Electric cyan structural accents
constexpr uint16_t COLOR_MATRIX = 0x2E44;  // Matrix green live statuses

// Supporting tones
constexpr uint16_t COLOR_SUBTLE     = rgb(140, 150, 165);  // Secondary text
constexpr uint16_t COLOR_PANEL      = rgb(11, 15, 21);     // Card fill
constexpr uint16_t COLOR_PANEL_HI   = rgb(18, 26, 36);     // Pressed card / table header fill
constexpr uint16_t COLOR_EDGE       = rgb(36, 44, 56);     // Card border
constexpr uint16_t COLOR_CYAN_DEEP  = rgb(0, 60, 255);     // Gradient end for the header rule
constexpr uint16_t COLOR_CYAN_TINT  = rgb(0, 38, 46);      // Icon badge fill
constexpr uint16_t COLOR_GREEN_TINT = rgb(8, 36, 18);      // Online pill fill
constexpr uint16_t COLOR_AMBER      = rgb(255, 180, 40);
constexpr uint16_t COLOR_AMBER_TINT = rgb(48, 34, 6);
constexpr uint16_t COLOR_RED        = rgb(255, 84, 84);
constexpr uint16_t COLOR_RED_TINT   = rgb(52, 12, 12);

// ---------------------------------------------------------------------------
// Geometry (landscape 480x320)
// ---------------------------------------------------------------------------
constexpr int16_t SCREEN_W = 480;
constexpr int16_t SCREEN_H = 320;
constexpr int16_t MARGIN   = 16;
constexpr int16_t GAP      = 12;

constexpr int16_t HEADER_H     = 48;   // Title bar height
constexpr int16_t RULE_Y       = 48;   // Header rule top edge
constexpr int16_t RULE_H       = 3;    // Header rule thickness
constexpr int16_t CONTENT_Y    = 62;   // First content row
constexpr int16_t FOOTER_Y     = 302;
constexpr int16_t FOOTER_H     = SCREEN_H - FOOTER_Y;

constexpr int16_t CARD_W       = (SCREEN_W - 2 * MARGIN - GAP) / 2;  // 218
constexpr int16_t CARD_H       = 112;
constexpr int16_t CARD_RADIUS  = 10;
constexpr int16_t COL_LEFT_X   = MARGIN;
constexpr int16_t COL_RIGHT_X  = MARGIN + CARD_W + GAP;
constexpr int16_t ROW_TOP_Y    = CONTENT_Y;
constexpr int16_t ROW_BOT_Y    = CONTENT_Y + CARD_H + GAP;
