#include "fan_link.h"

#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>

namespace {

constexpr uint32_t POLL_INTERVAL_MS = 3000;
constexpr uint32_t REQUEST_TIMEOUT_MS = 1500;
constexpr uint32_t STALE_AFTER_MS = 10000;
constexpr size_t MAX_HEADER_BYTES = 1024;
constexpr size_t MAX_BODY_BYTES = 3072;
constexpr uint8_t CONTROL_QUEUE_CAPACITY = 4;
constexpr uint8_t MAX_FANS = 4;

enum class RequestKind : uint8_t { None, Status, Control };
struct ControlCommand { String json; };
struct Snapshot {
  bool present = false;
  bool sensorValid = false;
  float temperatureC = 0.0f;
  String mode;
  String deviceId;
  String deviceName;
  String failsafeReason;
  float targetC = 0.0f;
  float manualDutyPct = 0.0f;
  float dutyPct = 0.0f;
  bool fansHealthy = false;
  uint8_t fanCount = 0;
  uint32_t fanRpm[MAX_FANS] = {};
  float fanDutyPct[MAX_FANS] = {};
  bool fanHealthy[MAX_FANS] = {};
  uint32_t controllerUpdatedMs = 0;
  uint32_t receivedMs = 0;
};

AsyncClient client;
String host;
uint16_t port = 80;
FanLinkState state = FanLinkState::NotConfigured;
String lastError;
Snapshot snapshot;
uint32_t revision = 0;
uint32_t nextPollMs = 0;
uint32_t requestStartedMs = 0;
RequestKind requestKind = RequestKind::None;
String requestBytes, responseHeader, responseBody;
bool headersComplete = false;
bool contentLengthKnown = false;
size_t contentLength = 0;
int httpStatus = 0;
bool transportBusy = false;
ControlCommand controlQueue[CONTROL_QUEUE_CAPACITY];
uint8_t queueHead = 0, queueCount = 0;

void touchRevision() { revision++; }
void setState(FanLinkState next) {
  if (state != next) { state = next; touchRevision(); }
}
void setError(const String &message, FanLinkState next = FanLinkState::Fault) {
  if (lastError != message) { lastError = message; touchRevision(); }
  setState(next);
  if (message.length()) Serial.printf("[FAN ] %s\n", message.c_str());
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
void finishRequest() { requestKind = RequestKind::None; requestBytes = ""; }

String jsonErrorMessage(JsonDocument &doc) {
  const char *code = doc["error"]["code"] | "http_error";
  const char *message = doc["error"]["message"] | "Controller rejected request";
  return String(code) + ": " + message;
}

bool applyStatusJson(JsonDocument &doc, uint32_t now) {
  const char *version = doc["api_version"] | "";
  JsonObject device = doc["device"];
  JsonObject controller = doc["controller"];
  JsonObject sensors = doc["sensors"];
  JsonArray fans = doc["fans"];
  const char *mode = controller["mode"] | "";
  if (strcmp(version, "1.0") != 0) { setError("Unsupported fan-controller API version"); return false; }
  if (device.isNull() || !device["id"].is<const char *>() ||
      !device["name"].is<const char *>() || controller.isNull() ||
      sensors.isNull() || !sensors["valid"].is<bool>() || fans.isNull() ||
      (strcmp(mode, "auto") && strcmp(mode, "manual") && strcmp(mode, "failsafe"))) {
    setError("Incomplete fan-controller status payload");
    return false;
  }

  Snapshot next;
  next.present = true;
  next.deviceId = device["id"].as<const char *>();
  next.deviceName = device["name"].as<const char *>();
  next.sensorValid = sensors["valid"] | false;
  if (next.sensorValid) {
    if (!sensors["temperature_c"].is<float>() && !sensors["temperature_c"].is<int>()) {
      setError("Valid sensor status has no temperature");
      return false;
    }
    next.temperatureC = sensors["temperature_c"].as<float>();
  }
  next.mode = mode;
  if (!controller["failsafe_reason"].isNull() &&
      !controller["failsafe_reason"].is<const char *>()) {
    setError("Fan-controller status has invalid fail-safe reason");
    return false;
  }
  next.failsafeReason = controller["failsafe_reason"].isNull()
                            ? String("") : String(controller["failsafe_reason"].as<const char *>());
  next.targetC = controller["target_c"] | 0.0f;
  next.manualDutyPct = controller["manual_duty_pct"] | 0.0f;
  if (!controller["target_c"].is<float>() && !controller["target_c"].is<int>()) {
    setError("Fan-controller status has no auto target");
    return false;
  }
  if (!controller["manual_duty_pct"].is<float>() && !controller["manual_duty_pct"].is<int>()) {
    setError("Fan-controller status has no manual duty");
    return false;
  }
  if (!doc["updated_ms"].is<uint32_t>()) {
    setError("Fan-controller status has no update time");
    return false;
  }
  next.controllerUpdatedMs = doc["updated_ms"].as<uint32_t>();
  next.fansHealthy = fans.size() > 0;
  float dutyTotal = 0.0f;
  for (JsonObject fan : fans) {
    if (!fan["id"].is<const char *>() || !fan["rpm"].is<uint32_t>() ||
        (!fan["duty_pct"].is<float>() && !fan["duty_pct"].is<int>()) ||
        !fan["healthy"].is<bool>()) {
      setError("Incomplete fan status entry");
      return false;
    }
    next.fansHealthy = next.fansHealthy && (fan["healthy"] | false);
    dutyTotal += fan["duty_pct"] | 0.0f;
    if (next.fanCount < MAX_FANS) {
      next.fanRpm[next.fanCount] = fan["rpm"] | 0UL;
      next.fanDutyPct[next.fanCount] = fan["duty_pct"] | 0.0f;
      next.fanHealthy[next.fanCount] = fan["healthy"] | false;
      next.fanCount++;
    }
  }
  next.dutyPct = fans.size() ? dutyTotal / fans.size() : 0.0f;
  next.receivedMs = now;
  snapshot = next;
  lastError = "";
  setState(FanLinkState::Online);
  touchRevision();
  return true;
}

void completeResponse() {
  if (requestKind == RequestKind::None) return;
  const RequestKind completedKind = requestKind;
  finishRequest();
  if (httpStatus < 200 || httpStatus >= 300) {
    DynamicJsonDocument errorDoc(768);
    if (!deserializeJson(errorDoc, responseBody) &&
        String((const char *)(errorDoc["api_version"] | "")) == "1.0")
      setError("HTTP " + String(httpStatus) + " - " + jsonErrorMessage(errorDoc));
    else setError("Fan controller returned HTTP " + String(httpStatus));
    client.close();
    return;
  }
  DynamicJsonDocument doc(3072);
  const DeserializationError error = deserializeJson(doc, responseBody);
  if (error) setError("Invalid fan-controller JSON: " + String(error.c_str()));
  else {
    applyStatusJson(doc, millis());
    if (completedKind == RequestKind::Control && state == FanLinkState::Online)
      Serial.println(F("[FAN ] Control response confirmed by controller"));
  }
  client.close();
}

void consumeResponseBytes(const char *data, size_t len) {
  if (requestKind == RequestKind::None) return;
  if (!headersComplete) {
    responseHeader.concat(data, len);
    const int separator = responseHeader.indexOf("\r\n\r\n");
    if (separator < 0) {
      if (responseHeader.length() > MAX_HEADER_BYTES) {
        finishRequest(); setError("Fan-controller HTTP headers are too large"); client.close();
      }
      return;
    }
    if ((size_t)separator > MAX_HEADER_BYTES) {
      finishRequest(); setError("Fan-controller HTTP headers are too large"); client.close(); return;
    }
    const String firstBody = responseHeader.substring(separator + 4);
    responseHeader.remove(separator);
    headersComplete = true;
    if (!parseHeaders()) {
      if (requestKind != RequestKind::None) {
        finishRequest();
        if (!lastError.length()) setError("Malformed fan-controller HTTP response");
      }
      client.close(); return;
    }
    responseBody = firstBody;
  } else responseBody.concat(data, len);

  if (responseBody.length() > MAX_BODY_BYTES ||
      (contentLengthKnown && responseBody.length() > contentLength)) {
    finishRequest(); setError("Fan-controller HTTP body length is invalid"); client.close(); return;
  }
  if (contentLengthKnown && responseBody.length() == contentLength) completeResponse();
}

void onDisconnect(void *, AsyncClient *) {
  transportBusy = false;
  if (requestKind == RequestKind::None) return;
  if (headersComplete && !contentLengthKnown) { completeResponse(); return; }
  finishRequest();
  setError("Fan-controller connection closed before the response completed",
           snapshot.present ? FanLinkState::Stale : FanLinkState::Fault);
}
void onError(void *, AsyncClient *source, int8_t error) {
  transportBusy = false;
  if (requestKind == RequestKind::None) return;
  finishRequest();
  setError("Fan-controller connection error: " + String(source->errorToString(error)),
           snapshot.present ? FanLinkState::Stale : FanLinkState::Fault);
  source->close();
}

void startRequest(RequestKind kind, const String &body = "") {
  resetResponse();
  requestKind = kind;
  transportBusy = true;
  requestStartedMs = millis();
  setState(FanLinkState::Requesting);
  const String authority = host + (port == 80 ? "" : ":" + String(port));
  if (kind == RequestKind::Control) {
    requestBytes = "PUT /api/v1/control HTTP/1.1\r\nHost: " + authority + "\r\n";
    requestBytes += "Accept: application/json\r\nContent-Type: application/json\r\nConnection: close\r\n";
    requestBytes += "Content-Length: " + String(body.length()) + "\r\n\r\n" + body;
  } else {
    requestBytes = "GET /api/v1/status HTTP/1.1\r\nHost: " + authority +
                   "\r\nAccept: application/json\r\nConnection: close\r\n\r\n";
  }
  if (!client.connect(host.c_str(), port)) {
    transportBusy = false;
    finishRequest();
    setError("Could not start fan-controller connection",
             snapshot.present ? FanLinkState::Stale : FanLinkState::Fault);
  }
}

bool enqueueControl(const String &json) {
  if (!host.length()) {
    setError("Configure the fan-controller host before queuing a command",
             FanLinkState::NotConfigured);
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    setError("Wi-Fi is offline; command was not queued", FanLinkState::Offline);
    return false;
  }
  if (queueCount >= CONTROL_QUEUE_CAPACITY) {
    setError("Control queue full; command was not queued", snapshot.present ? state : FanLinkState::Fault);
    return false;
  }
  controlQueue[(queueHead + queueCount) % CONTROL_QUEUE_CAPACITY].json = json;
  queueCount++;
  touchRevision();
  Serial.printf("[FAN ] Control queued (%u pending)\n", queueCount);
  return true;
}

}  // namespace

void fanLinkBegin() {
  Preferences preferences;
  preferences.begin("fanlink", true);
  host = preferences.getString("host", "");
  port = preferences.getUShort("port", 80);
  preferences.end();
  client.onConnect([](void *, AsyncClient *source) {
    const size_t written = source->write(requestBytes.c_str(), requestBytes.length());
    if (written != requestBytes.length()) {
      finishRequest(); setError("Could not send complete fan-controller request"); source->close();
    }
  }, nullptr);
  client.onData([](void *, AsyncClient *, void *data, size_t len) {
    consumeResponseBytes(static_cast<const char *>(data), len);
  }, nullptr);
  client.onDisconnect(onDisconnect, nullptr);
  client.onError(onError, nullptr);
  client.onTimeout([](void *, AsyncClient *, uint32_t) {
    transportBusy = false;
    if (requestKind == RequestKind::None) return;
    finishRequest();
    setError("Fan-controller send timeout", snapshot.present ? FanLinkState::Stale : FanLinkState::Fault);
    client.close();
  }, nullptr);
  client.setAckTimeout(REQUEST_TIMEOUT_MS);
  setState(host.length() ? FanLinkState::Offline : FanLinkState::NotConfigured);
}

void fanLinkConfigure(const char *newHost, uint16_t newPort) {
  if (transportBusy || requestKind != RequestKind::None) { finishRequest(); client.close(); transportBusy = false; }
  host = newHost;
  host.trim();
  port = newPort ? newPort : 80;
  Preferences preferences;
  preferences.begin("fanlink", false);
  preferences.putString("host", host);
  preferences.putUShort("port", port);
  preferences.end();
  snapshot = Snapshot();
  lastError = "";
  queueHead = queueCount = 0;
  nextPollMs = 0;
  setState(host.length() ? FanLinkState::Offline : FanLinkState::NotConfigured);
  touchRevision();
}

void fanLinkLoop(uint32_t now) {
  if (!host.length()) { setState(FanLinkState::NotConfigured); return; }
  if (WiFi.status() != WL_CONNECTED) {
    const bool controlResultUnknown = requestKind == RequestKind::Control;
    const bool queuedControlsCanceled = queueCount > 0;
    for (uint8_t i = 0; i < CONTROL_QUEUE_CAPACITY; i++) controlQueue[i].json = "";
    queueHead = queueCount = 0;
    if (requestKind != RequestKind::None) { finishRequest(); client.close(); transportBusy = false; }
    if (controlResultUnknown)
      setError("Wi-Fi lost; in-flight control result is unknown", FanLinkState::Offline);
    else if (queuedControlsCanceled)
      setError("Wi-Fi lost; queued controls were canceled", FanLinkState::Offline);
    else setState(FanLinkState::Offline);
    return;
  }
  if (requestKind != RequestKind::None) {
    if (now - requestStartedMs > REQUEST_TIMEOUT_MS) {
      finishRequest(); client.close(); transportBusy = false;
      setError("Fan-controller response timeout", snapshot.present ? FanLinkState::Stale : FanLinkState::Fault);
    }
    return;
  }
  if (transportBusy) return;
  if (snapshot.present && now - snapshot.receivedMs > STALE_AFTER_MS && state == FanLinkState::Online)
    setState(FanLinkState::Stale);
  if (queueCount) {
    const String json = controlQueue[queueHead].json;
    controlQueue[queueHead].json = "";
    queueHead = (queueHead + 1) % CONTROL_QUEUE_CAPACITY;
    queueCount--;
    touchRevision();
    startRequest(RequestKind::Control, json);
    return;
  }
  if ((int32_t)(now - nextPollMs) >= 0) {
    nextPollMs = now + POLL_INTERVAL_MS;
    startRequest(RequestKind::Status);
  }
}

FanLinkState fanLinkState() { return state; }
const char *fanLinkLabel() {
  switch (state) {
    case FanLinkState::NotConfigured: return "SETUP";
    case FanLinkState::Offline: return "OFFLINE";
    case FanLinkState::Requesting: return snapshot.present ? "UPDATING" : "LINKING";
    case FanLinkState::Online:
      if (!snapshot.sensorValid) return "SENSOR FAULT";
      if (!snapshot.fansHealthy) return "FAN FAULT";
      if (snapshot.mode == "failsafe") return "FAILSAFE";
      return "ONLINE";
    case FanLinkState::Stale: return "STALE";
    case FanLinkState::Fault: return "ERROR";
  }
  return "ERROR";
}
const String &fanLinkError() { return lastError; }
String fanLinkHost() { return host + (port == 80 ? "" : ":" + String(port)); }
uint32_t fanLinkRevision() { return revision; }
uint32_t fanLinkAgeMs(uint32_t now) { return snapshot.present ? now - snapshot.receivedMs : UINT32_MAX; }
bool fanLinkHasSnapshot() { return snapshot.present; }
bool fanLinkRequestActive() { return requestKind != RequestKind::None; }
uint8_t fanLinkQueuedControls() { return queueCount; }
bool fanLinkSensorValid() { return snapshot.present && snapshot.sensorValid; }
float fanLinkTemperatureC() { return snapshot.temperatureC; }
String fanLinkMode() { return snapshot.present ? snapshot.mode : String("-"); }
String fanLinkFailsafeReason() { return snapshot.failsafeReason; }
float fanLinkTargetC() { return snapshot.targetC; }
float fanLinkManualDutyPct() { return snapshot.manualDutyPct; }
float fanLinkDutyPct() { return snapshot.dutyPct; }
size_t fanLinkFanCount() { return snapshot.fanCount; }
bool fanLinkFanHealthy() { return snapshot.present && snapshot.fansHealthy; }
uint32_t fanLinkFanRpm(size_t index) { return index < snapshot.fanCount ? snapshot.fanRpm[index] : 0; }
float fanLinkFanDutyPct(size_t index) {
  return index < snapshot.fanCount ? snapshot.fanDutyPct[index] : 0.0f;
}
bool fanLinkFanHealthy(size_t index) {
  return index < snapshot.fanCount && snapshot.fanHealthy[index];
}
uint32_t fanLinkControllerUpdatedMs() { return snapshot.controllerUpdatedMs; }
bool fanLinkSetAuto(float targetC) {
  return targetC >= 20.0f && targetC <= 80.0f &&
         enqueueControl("{\"mode\":\"auto\",\"target_c\":" + String(targetC, 1) + "}");
}
bool fanLinkSetManual(float dutyPct) {
  return dutyPct >= 0.0f && dutyPct <= 100.0f &&
         enqueueControl("{\"mode\":\"manual\",\"manual_duty_pct\":" + String(dutyPct, 1) + "}");
}
