// Screens and navigation: dashboard, network device list, and Wi-Fi setup.
#pragma once

#include <Arduino.h>

enum class Screen : uint8_t { Dashboard, Network, Setup, Fan, PortalBox };

void   uiBegin();                      // draws the first screen
void   uiTick(uint32_t now);           // redraws whatever changed; call every loop
void   uiTap(uint16_t x, uint16_t y);  // routes a touch to the current screen
void   uiShow(Screen screen);
Screen uiScreen();
const char *uiScreenName();
