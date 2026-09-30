# Handoff

**Date:** 2026-09-30

## Where the firmware is

The dashboard keeps its four cards. `WIFI PENTESTER` no longer does nothing on a tap: holding it for 600 ms opens a new `PortalBox` screen, and a short tap only flashes the card. PortalBox is the captive-portal toolkit from `../portalbox`, ported into this firmware as `src/portalbox.*` — own access point, wildcard DNS, `/get` capture contract, portal library and capture log on the microSD card with a LittleFS fallback, and the same screen-level controls the original had (start/stop, next portal, clear, dump, beep).

While that access point is up the hub's own link is `LinkState::Suspended`: the station radio is off, the setup portal refuses to open, the LAN scan stops, and fan polling pauses. Stopping the portal restores the link and its state machine. The contract is written down in `design/architecture/portalbox-v1.md`.

The Server Fan screen and its asynchronous client are unchanged from the previous handoff.

## Next bench work

1. Flash the new partition table and firmware, then re-run touch calibration and re-add Wi-Fi networks: the app/LittleFS split rewrites NVS.
2. Watch the boot log for `[PB  ]` lines. They report the storage backend, the portal library it found, and the restored capture count. This is the first real check of the CH-07 microSD card.
3. Run `pio run -t uploadfs` so `data/portals/Default.html` lands in LittleFS, then hold the card for the gesture and confirm the screen opens only on a hold.
4. Start the portal, join `Free WiFi` from a phone, and confirm the login page pops by itself. Submit a test entry and check the counter, the last-email line, and `pb capture dump`.
5. Stop the portal and confirm the hub rejoins its network, the LAN scan runs again, and the fan card returns to live values.
6. Only then set `pb ssid` / `pb ch` for real use, and bench-measure the speaker pin before enabling the beep.

## What is not verified

The firmware compiles for `env:cyd35` (RAM 15.9%, flash 68.8% of the 1.69 MB app slot). It has not been uploaded. No access point, capture, portal file, touch hold, storage mount, or radio handover has been exercised on hardware. The microSD card is owner-reported only, the speaker pin is inherited from the portalbox project, and the board was not connected during this work: `COM22`, the port recorded in `CLAUDE.md`, is not present, and the one port that is (COM5, VID_291A:PID_8355) is not a CYD USB-serial bridge.
