#pragma once

#include <Arduino.h>

// Async HTTP client for the ESP32 workstand bench-supply meter's v1 status API.
// v1 is monitor-only: there is no control route, so unlike fan_link there is no
// command queue here.
enum class BenchLinkState : uint8_t { NotConfigured, Offline, Requesting, Online, Stale, Fault };

void benchLinkBegin();
void benchLinkLoop(uint32_t now);
void benchLinkConfigure(const char *host, uint16_t port = 80);

BenchLinkState benchLinkState();
const char *benchLinkLabel();
const String &benchLinkError();
String benchLinkHost();
uint32_t benchLinkRevision();
uint32_t benchLinkAgeMs(uint32_t now);
bool benchLinkHasSnapshot();
bool benchLinkRequestActive();

bool benchLinkMeterOk();
float benchLinkVolts();
float benchLinkAmps();
float benchLinkWatts();
float benchLinkMah();
float benchLinkWh();
bool benchLinkAirValid();
float benchLinkAirC();
float benchLinkHumidityPct();
bool benchLinkFanOn();
float benchLinkFanDutyPct();
uint32_t benchLinkFanRpm();
bool benchLinkFailsafe();
uint32_t benchLinkControllerUpdatedMs();
