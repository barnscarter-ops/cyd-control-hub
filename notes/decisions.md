# Decisions

## 2026-09-28 — Inventory-first build standard

Use the owned CYD as the dashboard. Inspect existing cables, supply, wire, and mounting hardware before obtaining anything. Do not reuse an on-hand part if its rating, physical fit, strain relief, or reliability is uncertain.

## 2026-09-30 — PortalBox is part of the hub, opened by a hold

PortalBox is ported into the Control Hub firmware instead of running on a second board: the inventory records one CYD, and the hub is the device that is actually powered on and mounted. It has no dashboard card of its own — holding the existing `WIFI PENTESTER` card for 600 ms opens it — so the dashboard stays four cards and a toolkit that starts an access point is never one stray tap away.

The hub's own Wi-Fi link is suspended while the portal AP is up. One radio cannot serve a station link, the Wi-Fi setup portal, and this AP at once, and a half-connected hub would report a link state that is not true. LAN scanning and fan polling pause with it and resume when the portal stops.

Storage is the microSD card when one mounts and LittleFS otherwise, matching the portalbox project. The partition table is fixed at one application slot plus LittleFS because this board is flashed over USB serial, not OTA.

Captured passwords stay in the CSV on the card. The screen shows the capture count and the most recent email only; the serial console dumps the file when the operator asks for it.

## 2026-09-28 — Fan controller separation

Server cooling stays in its own controller project. Control Hub reads status and sends requests through API v1; it does not share a power path or take responsibility for thermal fail-safe behavior.
