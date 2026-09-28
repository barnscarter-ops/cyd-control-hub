// XPT2046 resistive touch via TFT_eSPI, with NVS-persisted calibration.
#pragma once

#include <Arduino.h>

// Loads calibration, or runs the interactive 4-corner calibration if none is
// stored or the screen is held during boot. Blocking; call once from setup().
void touchBegin();

// Non-blocking poll. Returns true once per new press, with screen coords.
bool touchTapped(uint32_t now, uint16_t &x, uint16_t &y);
