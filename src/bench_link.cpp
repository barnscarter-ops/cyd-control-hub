#include "bench_link.h"

#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>

// Architecture mirrors fan_link.cpp (manual HTTP/1.1 request + header/body
// parsing over AsyncClient), trimmed to GET-only: v1 of the bench-supply API
// has no /control route, so there is no command queue.
namespace {

constexpr uint32_t POLL_INTERVAL_MS = 3000;
constexpr uint32_t REQUEST_TIMEOUT_MS = 1500;
constexpr uint32_t STALE_AFTER_MS = 10000;
constexpr size_t MAX_HEADER_BYTES = 1024;
constexpr size_t MAX_BODY_BYTES = 3072;

struct Snapshot {
  bool present = false;
  String deviceId;
  String deviceName;
  bool meterOk = false;
  float volts = 0.0f;
  float amps = 0.0f;
  float watts = 0.0f;
  float mah = 0.0f;
  float wh = 0.0f;
  bool airValid = false;
  float airC = 0.0f;
  float humidityPct = 0.0f;
  bool fanOn = false;
  float fanDutyPct = 0.0f;
  uint32_t fanRpm = 0;
  bool failsafe = false;
  uint32_t controllerUpdatedMs = 0;
  uint32_t receivedMs = 0;
};

AsyncClient client;
String host;
uint16_t port = 80;
BenchLinkState state = BenchLinkState::NotConfigured;
String lastError;
Snapshot snapshot;
uint32_t revision = 0;
uint32_t nextPollMs = 0;
uint32_t requestStartedMs = 0;
bool requestActive = false;
String requestBytes, responseHeader, responseBody;
bool headersComplete = false;
bool contentLengthKnown = false;
size_t contentLength = 0;
int httpStatus = 0;
bool transportBusy = false;

void touchRevision() { revision++; }
void setState(BenchLinkState next) {
  if (state != next) { state = next; touchRevision(); }
}
void setError(const String &message, BenchLinkState next = BenchLinkState::Fault) {
  if (lastError != message) { lastError = message; touchRevision(); }
  setState(next);
  if (message.length()) Serial.printf("[BNC ] %s\n", message.c_str());
}
String trimCopy(String value) { value.trim(); return value; }

bool parseStatusLine(const String &line) {
  if (!line.startsWith("HTTP/1.0 ") && !line.startsWith("HTTP/1.1 ")) return false;
  const int space = line.indexOf(' ');
  if (space < 0 || line.length() < (unsigned)(space + 4)) return false;
  httpStatus = line.substring(space + 1, space + 4).toInt();
  return httpStatus >= 100 && httpStatus <= 599;
}

bool parseHeaders() {
  const int firstEnd = responseHeader.indexOf("\r\n");
  if (firstEnd < 0 || !parseStatusLine(responseHeader.substring(0, firstEnd))) return false;
  int start = firstEnd + 2;
  while (start < (int)responseHeader.length()) {
    int end = responseHeader.indexOf("\r\n", start);
    if (end < 0) end = responseHeader.length();
    const int colon = responseHeader.indexOf(':', start);
    if (colon > start && colon < end) {
      String name = responseHeader.substring(start, colon);
      name.toLowerCase();
      const String value = trimCopy(responseHeader.substring(colon + 1, end));
      if (name == "content-length") {
        if (!value.length()) return false;
        for (size_t i = 0; i < value.length(); i++) if (!isDigit(value[i])) return false;
        const size_t parsedLength = (size_t)value.toInt();
        if (contentLengthKnown && parsedLength != contentLength) return false;
        contentLength = parsedLength;
        contentLengthKnown = true;
        if (contentLength > MAX_BODY_BYTES) return false;
      } else if (name == "transfer-encoding") {
        String encoding = value;
        encoding.toLowerCase();
        if (encoding.indexOf("chunked") >= 0) {
          setError("Chunked HTTP responses are not supported");
          return false;
        }
      }
    }
    start = end + 2;
  }
  return true;
}

void resetResponse() {
  responseHeader = responseBody = "";
  headersComplete = contentLengthKnown = false;
  contentLength = 0;
  httpStatus = 0;
}
void finishRequest() { requestActive = false; requestBytes = ""; }

String jsonErrorMessage(JsonDocument &doc) {
  const char *code = doc["error"]["code"] | "http_error";
  const char *message = doc["error"]["message"] | "Bench-supply rejected request";
  return String(code) + ": " + message;
}

bool applyStatusJson(JsonDocument &doc, uint32_t now) {
  const char *version = doc["api_version"] | "";
  JsonObject device = doc["device"];
  JsonObject meter = doc["meter"];
  JsonObject sensors = doc["sensors"];
  JsonObject fan = doc["fan"];
  if (strcmp(version, "1.0") != 0) { setError("Unsupported bench-supply API version"); return false; }
  if (device.isNull() || !device["id"].is<const char *>() || !device["name"].is<const char *>() ||
      meter.isNull() || !meter["ok"].is<bool>() || sensors.isNull() ||
      !sensors["air_valid"].is<bool>() || fan.isNull() || !fan["on"].is<bool>() ||
      !fan["failsafe"].is<bool>()) {
    setError("Incomplete bench-supply status payload");
    return false;
  }

  Snapshot next;
  next.present = true;
  next.deviceId = device["id"].as<const char *>();
  next.deviceName = device["name"].as<const char *>();
  next.meterOk = meter["ok"] | false;
  if (next.meterOk) {
    if ((!meter["volts"].is<float>() && !meter["volts"].is<int>()) ||
        (!meter["amps"].is<float>() && !meter["amps"].is<int>()) ||
        (!meter["watts"].is<float>() && !meter["watts"].is<int>())) {
      setError("Valid meter status is missing a reading");
      return false;
    }
    next.volts = meter["volts"].as<float>();
    next.amps = meter["amps"].as<float>();
    next.watts = meter["watts"].as<float>();
    next.mah = meter["mah"] | 0.0f;
    next.wh = meter["wh"] | 0.0f;
  }
  next.airValid = sensors["air_valid"] | false;
  if (next.airValid) {
    if (!sensors["air_c"].is<float>() && !sensors["air_c"].is<int>()) {
      setError("Valid sensor status has no air temperature");
      return false;
    }
    next.airC = sensors["air_c"].as<float>();
    next.humidityPct = sensors["humidity_pct"] | 0.0f;
  }
  next.fanOn = fan["on"] | false;
  next.fanDutyPct = fan["duty_pct"] | 0.0f;
  next.fanRpm = fan["rpm"] | 0UL;
  next.failsafe = fan["failsafe"] | false;
  if (!doc["updated_ms"].is<uint32_t>()) {
    setError("Bench-supply status has no update time");
    return false;
  }
  next.controllerUpdatedMs = doc["updated_ms"].as<uint32_t>();
  next.receivedMs = now;
  snapshot = next;
  lastError = "";
  setState(BenchLinkState::Online);
  touchRevision();
  return true;
}

void completeResponse() {
  if (!requestActive) return;
  finishRequest();
  if (httpStatus < 200 || httpStatus >= 300) {
    DynamicJsonDocument errorDoc(768);
    if (!deserializeJson(errorDoc, responseBody) &&
        String((const char *)(errorDoc["api_version"] | "")) == "1.0")
      setError("HTTP " + String(httpStatus) + " - " + jsonErrorMessage(errorDoc));
    else setError("Bench-supply returned HTTP " + String(httpStatus));
    client.close();
    return;
  }
  DynamicJsonDocument doc(3072);
  const DeserializationError error = deserializeJson(doc, responseBody);
  if (error) setError("Invalid bench-supply JSON: " + String(error.c_str()));
  else applyStatusJson(doc, millis());
  client.close();
}

void consumeResponseBytes(const char *data, size_t len) {
  if (!requestActive) return;
  if (!headersComplete) {
    responseHeader.concat(data, len);
    const int separator = responseHeader.indexOf("\r\n\r\n");
    if (separator < 0) {
      if (responseHeader.length() > MAX_HEADER_BYTES) {
        finishRequest(); setError("Bench-supply HTTP headers are too large"); client.close();
      }
      return;
    }
    if ((size_t)separator > MAX_HEADER_BYTES) {
      finishRequest(); setError("Bench-supply HTTP headers are too large"); client.close(); return;
    }
    const String firstBody = responseHeader.substring(separator + 4);
    responseHeader.remove(separator);
    headersComplete = true;
    if (!parseHeaders()) {
      if (requestActive) {
        finishRequest();
        if (!lastError.length()) setError("Malformed bench-supply HTTP response");
      }
      client.close(); return;
    }
    responseBody = firstBody;
  } else responseBody.concat(data, len);

  if (responseBody.length() > MAX_BODY_BYTES ||
      (contentLengthKnown && responseBody.length() > contentLength)) {
    finishRequest(); setError("Bench-supply HTTP body length is invalid"); client.close(); return;
  }
  if (contentLengthKnown && responseBody.length() == contentLength) completeResponse();
}

void onDisconnect(void *, AsyncClient *) {
  transportBusy = false;
  if (!requestActive) return;
  if (headersComplete && !contentLengthKnown) { completeResponse(); return; }
  finishRequest();
  setError("Bench-supply connection closed before the response completed",
           snapshot.present ? BenchLinkState::Stale : BenchLinkState::Fault);
}
void onError(void *, AsyncClient *source, int8_t error) {
  transportBusy = false;
  if (!requestActive) return;
  finishRequest();
  setError("Bench-supply connection error: " + String(source->errorToString(error)),
           snapshot.present ? BenchLinkState::Stale : BenchLinkState::Fault);
  source->close();
}

void startStatusRequest() {
  resetResponse();
  requestActive = true;
  transportBusy = true;
  requestStartedMs = millis();
  setState(BenchLinkState::Requesting);
  const String authority = host + (port == 80 ? "" : ":" + String(port));
  requestBytes = "GET /api/v1/status HTTP/1.1\r\nHost: " + authority +
                 "\r\nAccept: application/json\r\nConnection: close\r\n\r\n";
  if (!client.connect(host.c_str(), port)) {
    transportBusy = false;
    finishRequest();
    setError("Could not start bench-supply connection",
             snapshot.present ? BenchLinkState::Stale : BenchLinkState::Fault);
  }
}

}  // namespace

void benchLinkBegin() {
  Preferences preferences;
  preferences.begin("benchlink", true);
  host = preferences.getString("host", "");
  port = preferences.getUShort("port", 80);
  preferences.end();
  client.onConnect([](void *, AsyncClient *source) {
    const size_t written = source->write(requestBytes.c_str(), requestBytes.length());
    if (written != requestBytes.length()) {
      finishRequest(); setError("Could not send complete bench-supply request"); source->close();
    }
  }, nullptr);
  client.onData([](void *, AsyncClient *, void *data, size_t len) {
    consumeResponseBytes(static_cast<const char *>(data), len);
  }, nullptr);
  client.onDisconnect(onDisconnect, nullptr);
  client.onError(onError, nullptr);
  client.onTimeout([](void *, AsyncClient *, uint32_t) {
    transportBusy = false;
    if (!requestActive) return;
    finishRequest();
    setError("Bench-supply send timeout", snapshot.present ? BenchLinkState::Stale : BenchLinkState::Fault);
    client.close();
  }, nullptr);
  client.setAckTimeout(REQUEST_TIMEOUT_MS);
  setState(host.length() ? BenchLinkState::Offline : BenchLinkState::NotConfigured);
}

void benchLinkConfigure(const char *newHost, uint16_t newPort) {
  if (transportBusy || requestActive) { finishRequest(); client.close(); transportBusy = false; }
  host = newHost;
  host.trim();
  port = newPort ? newPort : 80;
  Preferences preferences;
  preferences.begin("benchlink", false);
  preferences.putString("host", host);
  preferences.putUShort("port", port);
  preferences.end();
  snapshot = Snapshot();
  lastError = "";
  nextPollMs = 0;
  setState(host.length() ? BenchLinkState::Offline : BenchLinkState::NotConfigured);
  touchRevision();
}

void benchLinkLoop(uint32_t now) {
  if (!host.length()) { setState(BenchLinkState::NotConfigured); return; }
  if (WiFi.status() != WL_CONNECTED) {
    if (requestActive) { finishRequest(); client.close(); transportBusy = false; }
    setState(BenchLinkState::Offline);
    return;
  }
  if (requestActive) {
    if (now - requestStartedMs > REQUEST_TIMEOUT_MS) {
      finishRequest(); client.close(); transportBusy = false;
      setError("Bench-supply response timeout", snapshot.present ? BenchLinkState::Stale : BenchLinkState::Fault);
    }
    return;
  }
  if (transportBusy) return;
  if (snapshot.present && now - snapshot.receivedMs > STALE_AFTER_MS && state == BenchLinkState::Online)
    setState(BenchLinkState::Stale);
  if ((int32_t)(now - nextPollMs) >= 0) {
    nextPollMs = now + POLL_INTERVAL_MS;
    startStatusRequest();
  }
}

BenchLinkState benchLinkState() { return state; }
const char *benchLinkLabel() {
  switch (state) {
    case BenchLinkState::NotConfigured: return "SETUP";
    case BenchLinkState::Offline: return "OFFLINE";
    case BenchLinkState::Requesting: return snapshot.present ? "UPDATING" : "LINKING";
    case BenchLinkState::Online:
      if (!snapshot.meterOk) return "METER FAULT";
      if (snapshot.failsafe) return "FAILSAFE";
      return "ONLINE";
    case BenchLinkState::Stale: return "STALE";
    case BenchLinkState::Fault: return "ERROR";
  }
  return "ERROR";
}
const String &benchLinkError() { return lastError; }
String benchLinkHost() { return host + (port == 80 ? "" : ":" + String(port)); }
uint32_t benchLinkRevision() { return revision; }
uint32_t benchLinkAgeMs(uint32_t now) { return snapshot.present ? now - snapshot.receivedMs : UINT32_MAX; }
bool benchLinkHasSnapshot() { return snapshot.present; }
bool benchLinkRequestActive() { return requestActive; }
bool benchLinkMeterOk() { return snapshot.present && snapshot.meterOk; }
float benchLinkVolts() { return snapshot.volts; }
float benchLinkAmps() { return snapshot.amps; }
float benchLinkWatts() { return snapshot.watts; }
float benchLinkMah() { return snapshot.mah; }
float benchLinkWh() { return snapshot.wh; }
bool benchLinkAirValid() { return snapshot.present && snapshot.airValid; }
float benchLinkAirC() { return snapshot.airC; }
float benchLinkHumidityPct() { return snapshot.humidityPct; }
bool benchLinkFanOn() { return snapshot.fanOn; }
float benchLinkFanDutyPct() { return snapshot.fanDutyPct; }
uint32_t benchLinkFanRpm() { return snapshot.fanRpm; }
bool benchLinkFailsafe() { return snapshot.present && snapshot.failsafe; }
uint32_t benchLinkControllerUpdatedMs() { return snapshot.controllerUpdatedMs; }
