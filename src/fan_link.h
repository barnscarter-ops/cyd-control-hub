#pragma once

#include <Arduino.h>

enum class FanLinkState : uint8_t { NotConfigured, Offline, Requesting, Online, Stale, Fault };

void fanLinkBegin();
void fanLinkLoop(uint32_t now);
void fanLinkConfigure(const char *host, uint16_t port = 80);

FanLinkState fanLinkState();
const char *fanLinkLabel();
const String &fanLinkError();
String fanLinkHost();
uint32_t fanLinkRevision();
uint32_t fanLinkAgeMs(uint32_t now);
bool fanLinkHasSnapshot();
bool fanLinkRequestActive();
uint8_t fanLinkQueuedControls();

bool fanLinkSensorValid();
float fanLinkTemperatureC();
String fanLinkMode();
String fanLinkFailsafeReason();
float fanLinkTargetC();
float fanLinkManualDutyPct();
float fanLinkDutyPct();
size_t fanLinkFanCount();
bool fanLinkFanHealthy();
uint32_t fanLinkFanRpm(size_t index);
float fanLinkFanDutyPct(size_t index);
bool fanLinkFanHealthy(size_t index);
uint32_t fanLinkControllerUpdatedMs();

// True means queued, not applied. Confirmed values change only after a successful
// full-status response. False means invalid input or a full bounded queue.
bool fanLinkSetAuto(float targetC);
bool fanLinkSetManual(float dutyPct);
