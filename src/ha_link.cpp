#include "ha_link.h"

#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>

// The HTTP transport here is deliberately the same shape as fan_link.cpp:
// a single AsyncClient, a header/body accumulator with hard size caps, and one
// request in flight at a time. The differences are Home Assistant specific:
// a Bearer token on every request, GET /api/states/<id> reads cycled across the
// configured entities one per poll, and POST /api/services/<domain>/<service>
// for tap actions. State JSON is parsed through a filter so a chatty entity's
// attributes can never blow the parse buffer.

namespace {

constexpr uint32_t POLL_INTERVAL_MS = 1200;    // one entity read per tick, round-robin
constexpr uint32_t REQUEST_TIMEOUT_MS = 2500;  // HA is a heavier host than the fan MCU
constexpr uint32_t STALE_AFTER_MS = 15000;
constexpr size_t MAX_HEADER_BYTES = 1536;
constexpr size_t MAX_BODY_BYTES = 6144;  // a single entity's state; filtered on parse
constexpr uint8_t ACTION_QUEUE_CAPACITY = 4;

enum class RequestKind : uint8_t { None, State, Service };

struct Entity {
  String id;
  String name;   // friendly_name, else id
  String state;  // last read state, "" until first read
  String unit;   // unit_of_measurement, else ""
  bool hasState = false;
  uint32_t receivedMs = 0;
};

AsyncClient client;
String host;
uint16_t port = 8123;
String token;
HaLinkState state = HaLinkState::NotConfigured;
String lastError;
uint32_t revision = 0;
bool sawAnyState = false;
uint32_t lastGoodMs = 0;

Entity entities[HA_MAX_ENTITIES];
uint8_t entityCount = 0;
uint8_t pollIndex = 0;  // next entity slot to read

uint32_t nextPollMs = 0;
uint32_t requestStartedMs = 0;
RequestKind requestKind = RequestKind::None;
uint8_t requestEntity = 0;  // slot the in-flight request refers to
String requestBytes, responseHeader, responseBody;
bool headersComplete = false;
bool contentLengthKnown = false;
size_t contentLength = 0;
int httpStatus = 0;
bool transportBusy = false;

// A queued tap action, by entity slot. Applied state only changes after the
// service call returns 2xx and the entity is re-read.
uint8_t actionQueue[ACTION_QUEUE_CAPACITY];
uint8_t queueHead = 0, queueCount = 0;

void touchRevision() { revision++; }
void setState(HaLinkState next) {
  if (state != next) { state = next; touchRevision(); }
}
void setError(const String &message, HaLinkState next = HaLinkState::Fault) {
  if (lastError != message) { lastError = message; touchRevision(); }
  setState(next);
  if (message.length()) Serial.printf("[HA  ] %s\n", message.c_str());
}
String trimCopy(String value) { value.trim(); return value; }

bool configured() { return host.length() && token.length(); }

// Lowercase, exactly one dot, domain + object id of sane characters.
bool validEntityId(const String &id) {
  const int dot = id.indexOf('.');
  if (dot <= 0 || dot >= (int)id.length() - 1) return false;
  if (id.length() > 64) return false;
  for (size_t i = 0; i < id.length(); i++) {
    const char c = id[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
    if (!ok) return false;
  }
  return id.indexOf('.', dot + 1) < 0;  // only one dot
}

String entityDomain(const String &id) {
  const int dot = id.indexOf('.');
  return dot > 0 ? id.substring(0, dot) : String("");
}

// Maps an entity's domain to the service that a tap should call. Empty domain
// means the entity is display-only (sensors, binary_sensors, etc.).
bool actionFor(const String &id, String &domain, String &service) {
  const String d = entityDomain(id);
  if (d == "switch" || d == "light" || d == "fan" || d == "input_boolean" || d == "cover") {
    domain = d; service = "toggle"; return true;
  }
  if (d == "script") { domain = "script"; service = "turn_on"; return true; }
  if (d == "scene") { domain = "scene"; service = "turn_on"; return true; }
  if (d == "button") { domain = "button"; service = "press"; return true; }
  if (d == "automation") { domain = "automation"; service = "trigger"; return true; }
  return false;
}

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
        contentLength = (size_t)value.toInt();
        contentLengthKnown = true;
        if (contentLength > MAX_BODY_BYTES) return false;
      } else if (name == "transfer-encoding") {
        String encoding = value; encoding.toLowerCase();
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

// Pulls only state + friendly_name + unit out of a /api/states/<id> payload,
// so the parse buffer stays tiny no matter how large the attributes are.
bool applyStateJson(const String &body, uint8_t slot, uint32_t now) {
  if (slot >= entityCount) return false;
  DynamicJsonDocument filter(96);
  filter["state"] = true;
  filter["attributes"]["friendly_name"] = true;
  filter["attributes"]["unit_of_measurement"] = true;
  DynamicJsonDocument doc(512);
  const DeserializationError error =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (error) { setError("Invalid HA state JSON: " + String(error.c_str())); return false; }
  if (!doc["state"].is<const char *>()) { setError("HA state payload has no state"); return false; }
  Entity &e = entities[slot];
  e.state = doc["state"].as<const char *>();
  e.name = doc["attributes"]["friendly_name"] | e.id.c_str();
  e.unit = doc["attributes"]["unit_of_measurement"] | "";
  e.hasState = true;
  e.receivedMs = now;
  sawAnyState = true;
  lastGoodMs = now;
  lastError = "";
  setState(HaLinkState::Online);
  touchRevision();
  return true;
}

void completeResponse() {
  if (requestKind == RequestKind::None) return;
  const RequestKind kind = requestKind;
  const uint8_t slot = requestEntity;
  finishRequest();
  if (httpStatus == 401 || httpStatus == 403) {
    setError("HA rejected the token (HTTP " + String(httpStatus) + ") - set `ha token`");
    client.close();
    return;
  }
  if (httpStatus < 200 || httpStatus >= 300) {
    setError("Home Assistant returned HTTP " + String(httpStatus),
             sawAnyState ? HaLinkState::Stale : HaLinkState::Fault);
    client.close();
    return;
  }
  if (kind == RequestKind::State) {
    applyStateJson(responseBody, slot, millis());
  } else {
    // A service call succeeded; re-read that entity next so its state refreshes.
    lastError = "";
    lastGoodMs = millis();
    if (slot < entityCount) { pollIndex = slot; nextPollMs = 0; }
    Serial.println(F("[HA  ] Service call accepted"));
    touchRevision();
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
        finishRequest(); setError("HA HTTP headers are too large"); client.close();
      }
      return;
    }
    if ((size_t)separator > MAX_HEADER_BYTES) {
      finishRequest(); setError("HA HTTP headers are too large"); client.close(); return;
    }
    const String firstBody = responseHeader.substring(separator + 4);
    responseHeader.remove(separator);
    headersComplete = true;
    if (!parseHeaders()) {
      if (requestKind != RequestKind::None) {
        finishRequest();
        if (!lastError.length()) setError("Malformed HA HTTP response");
      }
      client.close(); return;
    }
    responseBody = firstBody;
  } else responseBody.concat(data, len);

  if (responseBody.length() > MAX_BODY_BYTES ||
      (contentLengthKnown && responseBody.length() > contentLength)) {
    finishRequest();
    setError("HA response too large (entity has too many attributes)",
             sawAnyState ? HaLinkState::Stale : HaLinkState::Fault);
    client.close();
    return;
  }
  if (contentLengthKnown && responseBody.length() == contentLength) completeResponse();
}

void onDisconnect(void *, AsyncClient *) {
  transportBusy = false;
  if (requestKind == RequestKind::None) return;
  if (headersComplete && !contentLengthKnown) { completeResponse(); return; }
  finishRequest();
  setError("HA connection closed before the response completed",
           sawAnyState ? HaLinkState::Stale : HaLinkState::Fault);
}
void onError(void *, AsyncClient *source, int8_t error) {
  transportBusy = false;
  if (requestKind == RequestKind::None) return;
  finishRequest();
  setError("HA connection error: " + String(source->errorToString(error)),
           sawAnyState ? HaLinkState::Stale : HaLinkState::Fault);
  source->close();
}

void startRequest(RequestKind kind, uint8_t slot) {
  if (slot >= entityCount) { setState(sawAnyState ? state : HaLinkState::Online); return; }
  resetResponse();
  requestKind = kind;
  requestEntity = slot;
  transportBusy = true;
  requestStartedMs = millis();
  setState(HaLinkState::Requesting);
  const String authority = host + (port == 80 ? "" : ":" + String(port));
  const String auth = "Authorization: Bearer " + token + "\r\n";
  if (kind == RequestKind::Service) {
    String domain, service;
    if (!actionFor(entities[slot].id, domain, service)) {
      transportBusy = false; finishRequest(); return;
    }
    const String body = "{\"entity_id\":\"" + entities[slot].id + "\"}";
    requestBytes = "POST /api/services/" + domain + "/" + service + " HTTP/1.1\r\n";
    requestBytes += "Host: " + authority + "\r\n" + auth;
    requestBytes += "Accept: application/json\r\nContent-Type: application/json\r\nConnection: close\r\n";
    requestBytes += "Content-Length: " + String(body.length()) + "\r\n\r\n" + body;
  } else {
    requestBytes = "GET /api/states/" + entities[slot].id + " HTTP/1.1\r\n";
    requestBytes += "Host: " + authority + "\r\n" + auth;
    requestBytes += "Accept: application/json\r\nConnection: close\r\n\r\n";
  }
  if (!client.connect(host.c_str(), port)) {
    transportBusy = false;
    finishRequest();
    setError("Could not start HA connection", sawAnyState ? HaLinkState::Stale : HaLinkState::Fault);
  }
}

void clearActionQueue() { queueHead = queueCount = 0; }

void loadConfig() {
  Preferences p;
  p.begin("halink", true);
  host = p.getString("host", "");
  port = p.getUShort("port", 8123);
  token = p.getString("token", "");
  entityCount = p.getUChar("n", 0);
  if (entityCount > HA_MAX_ENTITIES) entityCount = HA_MAX_ENTITIES;
  for (uint8_t i = 0; i < entityCount; i++) {
    entities[i] = Entity();
    entities[i].id = p.getString(("e" + String(i)).c_str(), "");
    entities[i].name = entities[i].id;
  }
  p.end();
}

void saveEntities() {
  Preferences p;
  p.begin("halink", false);
  p.putUChar("n", entityCount);
  for (uint8_t i = 0; i < entityCount; i++) p.putString(("e" + String(i)).c_str(), entities[i].id);
  // Clear any now-unused slots so a shrink does not leave stale ids behind.
  for (uint8_t i = entityCount; i < HA_MAX_ENTITIES; i++) p.remove(("e" + String(i)).c_str());
  p.end();
}

}  // namespace

void haLinkBegin() {
  loadConfig();
  client.onConnect([](void *, AsyncClient *source) {
    const size_t written = source->write(requestBytes.c_str(), requestBytes.length());
    if (written != requestBytes.length()) {
      finishRequest(); setError("Could not send complete HA request"); source->close();
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
    setError("HA send timeout", sawAnyState ? HaLinkState::Stale : HaLinkState::Fault);
    client.close();
  }, nullptr);
  client.setAckTimeout(REQUEST_TIMEOUT_MS);
  setState(configured() ? HaLinkState::Offline : HaLinkState::NotConfigured);
}

void haLinkConfigure(const char *newHost, uint16_t newPort) {
  if (transportBusy || requestKind != RequestKind::None) {
    finishRequest(); client.close(); transportBusy = false;
  }
  host = newHost; host.trim();
  port = newPort ? newPort : 8123;
  Preferences p;
  p.begin("halink", false);
  p.putString("host", host);
  p.putUShort("port", port);
  p.end();
  sawAnyState = false;
  lastError = "";
  clearActionQueue();
  nextPollMs = 0;
  pollIndex = 0;
  for (uint8_t i = 0; i < entityCount; i++) { entities[i].hasState = false; entities[i].state = ""; }
  setState(configured() ? HaLinkState::Offline : HaLinkState::NotConfigured);
  touchRevision();
}

void haLinkSetToken(const char *newToken) {
  token = newToken; token.trim();
  Preferences p;
  p.begin("halink", false);
  p.putString("token", token);
  p.end();
  lastError = "";
  setState(configured() ? HaLinkState::Offline : HaLinkState::NotConfigured);
  touchRevision();
}

bool haLinkHasToken() { return token.length() > 0; }

void haLinkLoop(uint32_t now) {
  if (!configured()) { setState(HaLinkState::NotConfigured); return; }
  if (WiFi.status() != WL_CONNECTED) {
    clearActionQueue();
    if (requestKind != RequestKind::None) { finishRequest(); client.close(); transportBusy = false; }
    setState(HaLinkState::Offline);
    return;
  }
  if (requestKind != RequestKind::None) {
    if (now - requestStartedMs > REQUEST_TIMEOUT_MS) {
      finishRequest(); client.close(); transportBusy = false;
      setError("HA response timeout", sawAnyState ? HaLinkState::Stale : HaLinkState::Fault);
    }
    return;
  }
  if (transportBusy) return;
  if (sawAnyState && now - lastGoodMs > STALE_AFTER_MS && state == HaLinkState::Online)
    setState(HaLinkState::Stale);

  if (queueCount) {
    const uint8_t slot = actionQueue[queueHead];
    queueHead = (queueHead + 1) % ACTION_QUEUE_CAPACITY;
    queueCount--;
    touchRevision();
    startRequest(RequestKind::Service, slot);
    return;
  }
  if (entityCount == 0) return;  // nothing to poll until an entity is added
  if ((int32_t)(now - nextPollMs) >= 0) {
    nextPollMs = now + POLL_INTERVAL_MS;
    const uint8_t slot = pollIndex;
    pollIndex = (pollIndex + 1) % entityCount;
    startRequest(RequestKind::State, slot);
  }
}

HaLinkState haLinkState() { return state; }
const char *haLinkLabel() {
  switch (state) {
    case HaLinkState::NotConfigured: return host.length() ? "NEEDS TOKEN" : "SETUP";
    case HaLinkState::Offline: return "OFFLINE";
    case HaLinkState::Requesting: return sawAnyState ? "UPDATING" : "LINKING";
    case HaLinkState::Online: return "ONLINE";
    case HaLinkState::Stale: return "STALE";
    case HaLinkState::Fault: return "ERROR";
  }
  return "ERROR";
}
const String &haLinkError() { return lastError; }
String haLinkHost() { return host.length() ? host + (port == 80 ? "" : ":" + String(port)) : String(""); }
uint32_t haLinkRevision() { return revision; }
bool haLinkHasSnapshot() { return sawAnyState; }
bool haLinkRequestActive() { return requestKind != RequestKind::None; }
uint8_t haLinkQueuedActions() { return queueCount; }

uint8_t haLinkEntityCount() { return entityCount; }

bool haLinkAddEntity(const char *entityId) {
  String id = entityId; id.trim(); id.toLowerCase();
  if (!validEntityId(id)) { setError("Not a valid entity id: " + id, state); return false; }
  if (entityCount >= HA_MAX_ENTITIES) { setError("Entity list is full (max 6)", state); return false; }
  for (uint8_t i = 0; i < entityCount; i++) if (entities[i].id == id) return false;
  entities[entityCount] = Entity();
  entities[entityCount].id = id;
  entities[entityCount].name = id;
  entityCount++;
  saveEntities();
  nextPollMs = 0;
  touchRevision();
  return true;
}

bool haLinkRemoveEntity(const char *entityId) {
  String id = entityId; id.trim(); id.toLowerCase();
  for (uint8_t i = 0; i < entityCount; i++) {
    if (entities[i].id == id) {
      for (uint8_t j = i; j + 1 < entityCount; j++) entities[j] = entities[j + 1];
      entityCount--;
      entities[entityCount] = Entity();
      if (pollIndex >= entityCount) pollIndex = 0;
      saveEntities();
      touchRevision();
      return true;
    }
  }
  return false;
}

void haLinkClearEntities() {
  for (uint8_t i = 0; i < HA_MAX_ENTITIES; i++) entities[i] = Entity();
  entityCount = 0;
  pollIndex = 0;
  saveEntities();
  touchRevision();
}

String haLinkEntityId(uint8_t index) { return index < entityCount ? entities[index].id : String(""); }
String haLinkEntityName(uint8_t index) {
  if (index >= entityCount) return "";
  return entities[index].name.length() ? entities[index].name : entities[index].id;
}
String haLinkEntityState(uint8_t index) {
  if (index >= entityCount || !entities[index].hasState) return "-";
  return entities[index].state;
}
String haLinkEntityUnit(uint8_t index) { return index < entityCount ? entities[index].unit : String(""); }
bool haLinkEntityHasState(uint8_t index) { return index < entityCount && entities[index].hasState; }
bool haLinkEntityActionable(uint8_t index) {
  if (index >= entityCount) return false;
  String d, s;
  return actionFor(entities[index].id, d, s);
}
uint32_t haLinkEntityAgeMs(uint8_t index, uint32_t now) {
  if (index >= entityCount || !entities[index].hasState) return UINT32_MAX;
  return now - entities[index].receivedMs;
}

bool haLinkActivate(uint8_t index) {
  if (index >= entityCount) return false;
  if (!haLinkEntityActionable(index)) return false;
  if (!configured()) { setError("Set the HA host and token first", HaLinkState::NotConfigured); return false; }
  if (WiFi.status() != WL_CONNECTED) { setError("Wi-Fi is offline; action not queued", HaLinkState::Offline); return false; }
  if (queueCount >= ACTION_QUEUE_CAPACITY) { setError("HA action queue full", state); return false; }
  actionQueue[(queueHead + queueCount) % ACTION_QUEUE_CAPACITY] = index;
  queueCount++;
  touchRevision();
  Serial.printf("[HA  ] Action queued for %s (%u pending)\n", entities[index].id.c_str(), queueCount);
  return true;
}
