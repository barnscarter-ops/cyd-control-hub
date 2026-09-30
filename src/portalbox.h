// PortalBox - captive-portal audit toolkit, ported from projects/portalbox.
//
// Starts a self-hosted access point that serves a login page and records what
// the client submits. Portal HTML and capture logs live on the microSD card
// when one is mounted, otherwise in the LittleFS partition.
//
// Radio ownership: the hub's own Wi-Fi link is suspended while the AP is up
// (linkSuspend/linkResume). The Wi-Fi setup portal, the LAN scan, and the fan
// client all pause for the same reason - one radio, one access point.
#pragma once

#include <Arduino.h>

void pbBegin();
void pbLoop(uint32_t now);

// Access point
bool     pbActive();
bool     pbStart();                 // false if the AP could not be created
void     pbStop();
uint32_t pbRevision();              // bumped on every visible change

const String &pbSsid();
void          pbSetSsid(const String &ssid);
uint8_t       pbChannel();
void          pbSetChannel(uint8_t channel);
String        pbApIp();
uint8_t       pbClients();

// Portal page library
const char *pbBackendName();        // "sd", "flash", or "none"
size_t      pbPortalCount();
String      pbPortalAt(size_t index);
size_t      pbPortalSize(size_t index);
String      pbSelectedPortal();
bool        pbSelectPortal(const String &name);
bool        pbSelectNextPortal();   // cycles the library, false if empty
bool        pbDeletePortal(const String &name);
size_t      pbSyncFromFlash();      // copies flash pages the card lacks, never overwrites
bool        pbClone(const String &url, const String &pageName);  // fetch a real portal page; needs the hub online
uint64_t    pbStoreUsedBytes();
uint64_t    pbStoreTotalBytes();

// Captures
uint32_t pbCaptureCount();
String   pbLastCaptureEmail();
void     pbDumpCaptures(Stream &out);
void     pbClearCaptures();

// Capture beep (speaker pin is not bench-verified on CH-01; off by default)
bool pbBeepEnabled();
void pbSetBeep(bool on);
