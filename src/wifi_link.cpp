#include "wifi_link.h"

#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>

// Central time with US DST rules (POSIX TZ string).
static const char *TIMEZONE = "CST6CDT,M3.2.0,M11.1.0";
static const char *HOSTNAME = "control-hub";

static const uint32_t SCAN_RETRY_MS        = 10000;
static const uint32_t CONNECT_TIMEOUT_MS   = 15000;
static const uint8_t  SCANS_BEFORE_PORTAL  = 2;
static const uint32_t MANUAL_PORTAL_IDLE_MS = 5UL * 60UL * 1000UL;

struct SavedNetwork {
  String ssid;
  String pass;
};

static WiFiManager  wm;
static SavedNetwork saved[MAX_SAVED];
static size_t       savedCount = 0;

static LinkState state        = LinkState::Searching;
static uint32_t  stateSinceMs = 0;
static uint32_t  nextScanMs   = 0;
static bool      scanPending  = false;
static uint8_t   failedScans  = 0;
static bool      timeStarted  = false;

static bool     portalActive   = false;
static bool     portalManual   = false;  // opened by the user rather than by failover
static bool     portalSaved    = false;  // set by WiFiManager's save callback
static uint32_t portalOpenedMs = 0;
static String   apSsid;
static String   apPass;

static uint32_t revision = 0;

// ---------------------------------------------------------------------------
// Saved network store (NVS namespace "wifi": n, s0..s4, p0..p4)
// ---------------------------------------------------------------------------
static void loadSaved() {
  Preferences prefs;
  prefs.begin("wifi", false);  // read-write so a missing namespace is created, not logged as an error
  savedCount = min((size_t)prefs.getUChar("n", 0), MAX_SAVED);
  for (size_t i = 0; i < savedCount; i++) {
    saved[i].ssid = prefs.getString(("s" + String(i)).c_str(), "");
    saved[i].pass = prefs.getString(("p" + String(i)).c_str(), "");
  }
  prefs.end();
}

static void storeSaved() {
  Preferences prefs;
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.putUChar("n", savedCount);
  for (size_t i = 0; i < savedCount; i++) {
    prefs.putString(("s" + String(i)).c_str(), saved[i].ssid);
    prefs.putString(("p" + String(i)).c_str(), saved[i].pass);
  }
  prefs.end();
}

// Adds or refreshes a network, moving it to the front of the list.
static void rememberNetwork(const String &ssid, const String &pass) {
  size_t existing = savedCount;
  for (size_t i = 0; i < savedCount; i++) {
    if (saved[i].ssid == ssid) existing = i;
  }
  size_t last = existing < savedCount ? existing : min(savedCount, MAX_SAVED - 1);
  for (size_t i = last; i > 0; i--) saved[i] = saved[i - 1];
  saved[0] = {ssid, pass};
  if (existing == savedCount && savedCount < MAX_SAVED) savedCount++;
  storeSaved();
  Serial.printf("[WIFI] Saved network \"%s\" (%u stored)\n", ssid.c_str(), (unsigned)savedCount);
}

// ---------------------------------------------------------------------------
// State helpers
// ---------------------------------------------------------------------------
static void setState(LinkState next) {
  if (state == next) return;
  state        = next;
  stateSinceMs = millis();
  revision++;
  Serial.printf("[WIFI] State -> %s\n", linkStateLabel());
}

static void startScan() {
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
  WiFi.scanDelete();
  WiFi.scanNetworks(true /* async */);
  scanPending = true;
}

static void handleScanResult(uint32_t now) {
  const int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  scanPending = false;

  int     best     = -1;
  int32_t bestRssi = -1000;
  for (int16_t i = 0; i < n; i++) {
    const String ssid = WiFi.SSID(i);
    for (size_t j = 0; j < savedCount; j++) {
      if (ssid == saved[j].ssid && WiFi.RSSI(i) > bestRssi) {
        best     = (int)j;
        bestRssi = WiFi.RSSI(i);
      }
    }
  }
  WiFi.scanDelete();

  if (best >= 0) {
    Serial.printf("[WIFI] Found \"%s\" (%ld dBm), connecting\n", saved[best].ssid.c_str(),
                  (long)bestRssi);
    WiFi.begin(saved[best].ssid.c_str(), saved[best].pass.c_str());
    setState(LinkState::Connecting);
    return;
  }

  failedScans++;
  nextScanMs = now + SCAN_RETRY_MS;
  setState(LinkState::Offline);
  if (!portalActive && failedScans >= SCANS_BEFORE_PORTAL) {
    Serial.println(F("[WIFI] No saved network in range - opening setup portal"));
    portalManual = false;
    linkOpenPortal();
  }
}

static void onOnline() {
  failedScans = 0;
  setState(LinkState::Online);
  Serial.printf("[WIFI] Online: \"%s\" ip=%s rssi=%d\n", WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  if (!timeStarted) {
    configTzTime(TIMEZONE, "pool.ntp.org", "time.google.com");
    timeStarted = true;
  }
  if (portalActive && !portalManual) linkClosePortal();
}

static void handlePortalSave() {
  portalSaved = false;
  const String ssid = wm.getWiFiSSID(true);
  const String pass = wm.getWiFiPass(true);
  if (ssid.length() == 0) return;

  linkAddNetwork(ssid, pass);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void linkBegin() {
  loadSaved();
  Serial.printf("[WIFI] %u saved network(s)\n", (unsigned)savedCount);
  for (size_t i = 0; i < savedCount; i++) Serial.printf("[WIFI]   %u: %s\n", (unsigned)i, saved[i].ssid.c_str());

  const uint64_t mac = ESP.getEfuseMac();
  char buf[24];
  snprintf(buf, sizeof(buf), "ControlHub-%02X%02X", (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  apSsid = buf;
  snprintf(buf, sizeof(buf), "hub%05lu", (unsigned long)((mac >> 16) % 100000UL));
  apPass = buf;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(false);  // reconnection is handled by linkLoop()

  wm.setDebugOutput(false);
  wm.setConfigPortalBlocking(false);
  wm.setBreakAfterConfig(true);
  wm.setSaveConnect(false);       // just store; linkLoop() joins it when in range
  wm.setSaveConnectTimeout(2);
  wm.setConfigPortalTimeout(0);   // lifetime is managed here
  wm.setHostname(HOSTNAME);
  wm.setTitle("Control Hub");
  wm.setDarkMode(true);
  wm.setShowInfoErase(false);
  wm.setShowInfoUpdate(false);
  std::vector<const char *> menu = {"wifi", "exit"};
  wm.setMenu(menu);
  wm.setCustomHeadElement(
      "<style>"
      "body{background:#000}"
      "button,input[type=submit]{background:#00dce8;color:#000;border-radius:8px;font-weight:600}"
      "a{color:#00dce8}"
      "input{border-radius:6px}"
      "</style>");
  wm.setSaveConfigCallback([]() { portalSaved = true; });

  if (savedCount == 0) {
    Serial.println(F("[WIFI] No saved networks - opening setup portal"));
    setState(LinkState::Offline);
    linkOpenPortal();
  }
  nextScanMs = 0;
}

void linkLoop(uint32_t now) {
  if (portalActive) {
    wm.process();
    if (portalSaved) handlePortalSave();
    if (portalManual && state == LinkState::Online && linkPortalClients() == 0 &&
        now - portalOpenedMs > MANUAL_PORTAL_IDLE_MS) {
      Serial.println(F("[WIFI] Setup portal idle - closing"));
      linkClosePortal();
    }
  }

  switch (state) {
    case LinkState::Searching:
    case LinkState::Offline:
      if (savedCount == 0) break;
      if (scanPending) {
        handleScanResult(now);
      } else if ((int32_t)(now - nextScanMs) >= 0 && linkPortalClients() == 0) {
        // Don't scan while a phone is on the portal: it would disrupt the AP.
        startScan();
      }
      break;

    case LinkState::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        onOnline();
      } else if (now - stateSinceMs > CONNECT_TIMEOUT_MS) {
        Serial.println(F("[WIFI] Connect timed out"));
        WiFi.disconnect();
        nextScanMs = now + 2000;
        setState(LinkState::Offline);
      }
      break;

    case LinkState::Online:
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[WIFI] Connection lost"));
        nextScanMs = now + 1000;
        setState(LinkState::Searching);
      }
      break;
  }
}

LinkState linkState() { return state; }

const char *linkStateLabel() {
  switch (state) {
    case LinkState::Searching:  return "SEARCHING";
    case LinkState::Connecting: return "CONNECTING";
    case LinkState::Online:     return "ONLINE";
    case LinkState::Offline:    return "OFFLINE";
  }
  return "";
}

bool linkPortalActive() { return portalActive; }

void linkOpenPortal() {
  if (portalActive) return;
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
    WiFi.scanDelete();
    scanPending = false;
  }
  portalManual   = portalManual || state == LinkState::Online;
  wm.startConfigPortal(apSsid.c_str(), apPass.c_str());
  portalActive   = true;
  portalOpenedMs = millis();
  revision++;
  Serial.printf("[WIFI] Setup portal open: SSID \"%s\" password \"%s\" ip=%s\n",
                apSsid.c_str(), apPass.c_str(), WiFi.softAPIP().toString().c_str());
}

void linkClosePortal() {
  if (!portalActive) return;
  wm.stopConfigPortal();
  WiFi.mode(WIFI_STA);
  portalActive = false;
  portalManual = false;
  revision++;
  Serial.println(F("[WIFI] Setup portal closed"));
}

const String &linkPortalSsid() { return apSsid; }
const String &linkPortalPass() { return apPass; }

uint8_t linkPortalClients() {
  return portalActive ? WiFi.softAPgetStationNum() : 0;
}

size_t linkSavedCount() { return savedCount; }

const String &linkSavedSsid(size_t index) {
  static const String empty;
  return index < savedCount ? saved[index].ssid : empty;
}

void linkAddNetwork(const String &ssid, const String &pass) {
  rememberNetwork(ssid, pass);
  linkClosePortal();
  if (state == LinkState::Online) return;  // keep the current link; the new one is a fallback
  failedScans = 0;
  nextScanMs  = 0;  // look for it right away
  setState(LinkState::Searching);
}

void linkForgetAll() {
  savedCount = 0;
  storeSaved();
  WiFi.disconnect(false, true);
  Serial.println(F("[WIFI] Forgot all saved networks"));
  setState(LinkState::Offline);
  portalManual = false;
  linkOpenPortal();
}

uint32_t linkRevision() { return revision; }
