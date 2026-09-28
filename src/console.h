// Line-based serial console for testing without touching the panel.
// Type `help` in the serial monitor for the command list.
#pragma once

#include <Arduino.h>

void consoleLoop();  // non-blocking; call every loop
