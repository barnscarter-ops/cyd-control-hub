// Wi-Fi connectivity: saved-network list, auto-connect, and captive-portal setup.
//
// Up to MAX_SAVED networks are kept in NVS (newest first). The link scans for
// any saved network in range and joins the strongest. If none is found, a
// WiFiManager captive portal is opened so a phone can add a new network.
#pragma once

#include <Arduino.h>

enum class LinkState : uint8_t {
  Searching,   // scanning for a saved network
  Connecting,  // joining a saved network
  Online,      // connected with an IP
  Offline,     // no saved network in range (retrying in the background)
};

static const size_t MAX_SAVED = 5;

void linkBegin();
void linkLoop(uint32_t now);

LinkState   linkState();
const char *linkStateLabel();

// Captive portal (setup access point)
bool          linkPortalActive();
void          linkOpenPortal();   // on demand, e.g. to add another network
void          linkClosePortal();
const String &linkPortalSsid();
const String &linkPortalPass();
uint8_t       linkPortalClients();

// Saved networks
size_t        linkSavedCount();
const String &linkSavedSsid(size_t index);
void          linkAddNetwork(const String &ssid, const String &pass);  // same as saving in the portal
void          linkForgetAll();

// Bumped on every user-visible change so the UI knows when to redraw.
uint32_t linkRevision();
