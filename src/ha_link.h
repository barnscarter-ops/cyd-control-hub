#pragma once

#include <Arduino.h>

// Async HTTP client for a Home Assistant instance's REST API.
//
// Reads a small, user-chosen set of entities with GET /api/states/<entity_id>
// (round-robin, one request per poll tick, like fan_link does for status) and
// actuates them with POST /api/services/<domain>/<service>. Authentication is a
// long-lived access token sent as `Authorization: Bearer <token>`; the host,
// port, token, and entity list all live in NVS (namespace `halink`), never in
// tracked source — same rule as the saved Wi-Fi credentials and the fan host.
//
// v1 surfaces entity state and one tap action per actionable entity (toggle a
// switch/light/input_boolean/fan/cover, run a script/scene, press a button,
// trigger an automation). Sensors and other read-only domains are display-only.

enum class HaLinkState : uint8_t { NotConfigured, Offline, Requesting, Online, Stale, Fault };

constexpr uint8_t HA_MAX_ENTITIES = 6;

void haLinkBegin();
void haLinkLoop(uint32_t now);

// Host/port are stored together; the token is set separately so it is never
// echoed alongside the host. Passing an empty host clears the configuration.
void haLinkConfigure(const char *host, uint16_t port = 8123);
void haLinkSetToken(const char *token);  // empty clears the stored token
bool haLinkHasToken();

HaLinkState haLinkState();
const char *haLinkLabel();
const String &haLinkError();
String haLinkHost();
uint32_t haLinkRevision();
bool haLinkHasSnapshot();
bool haLinkRequestActive();
uint8_t haLinkQueuedActions();

// Entity configuration (persisted). add/remove/clear renumber the slots.
uint8_t haLinkEntityCount();
bool haLinkAddEntity(const char *entityId);     // false if full, duplicate, or malformed
bool haLinkRemoveEntity(const char *entityId);  // false if not present
void haLinkClearEntities();
String haLinkEntityId(uint8_t index);

// Entity runtime state (valid after at least one successful poll of that slot).
String haLinkEntityName(uint8_t index);      // friendly_name, else the entity id
String haLinkEntityState(uint8_t index);     // "on" / "off" / "42.5" / "-" if unknown
String haLinkEntityUnit(uint8_t index);      // unit_of_measurement, else ""
bool haLinkEntityHasState(uint8_t index);    // a state has been read for this slot
bool haLinkEntityActionable(uint8_t index);  // tapping it will toggle/run/press/trigger
uint32_t haLinkEntityAgeMs(uint8_t index, uint32_t now);

// Queue a tap action for the entity. Returns false if the entity is not
// actionable, the queue is full, or Wi-Fi/config is unavailable. A queued
// action is not an applied one — state only changes after the service call
// succeeds and the entity is re-polled.
bool haLinkActivate(uint8_t index);
