// LAN device discovery by ARP sweep of the local subnet (capped at /24).
//
// Sends ARP requests in small batches from loop() and harvests the lwIP ARP
// table as replies arrive, so it never blocks.
#pragma once

#include <Arduino.h>

struct LanDevice {
  uint32_t ip;      // network byte order (as IPAddress stores it)
  uint8_t  mac[6];
  const char *kind;  // "Gateway", "This hub", "Private MAC", vendor, or "Device"
};

static const size_t LAN_MAX_DEVICES = 64;

void lanScanStart();
void lanScanLoop(uint32_t now);

bool             lanScanRunning();
uint8_t          lanScanProgress();  // 0..100
uint32_t         lanScanFinishedMs(); // millis() of the last completed scan, 0 if never
size_t           lanDeviceCount();
const LanDevice &lanDevice(size_t index);

// Returns true once after the device list changed.
bool lanScanChanged();
