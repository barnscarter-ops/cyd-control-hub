# Handoff

**Date:** 2026-09-30

## Where the firmware is

The dashboard keeps its four cards. `WIFI PENTESTER` no longer does nothing on a tap: holding it for 600 ms opens a new `PortalBox` screen, and a short tap only flashes the card. PortalBox is the captive-portal toolkit from `../portalbox`, ported into this firmware as `src/portalbox.*` — own access point, wildcard DNS, `/get` capture contract, portal library and capture log on the microSD card with a LittleFS fallback, and the same screen-level controls the original had (start/stop, next portal, clear, dump, beep).

While that access point is up the hub's own link is `LinkState::Suspended`: the station radio is off, the setup portal refuses to open, the LAN scan stops, and fan polling pauses. Stopping the portal restores the link and its state machine. The contract is written down in `design/architecture/portalbox-v1.md`.

The Server Fan screen and its asynchronous client are unchanged from the previous handoff.

## First bench run — 2026-09-30

Flashed over USB serial on this machine (COM23) with `pio run -t upload`. What the boot log establishes:

| Observation | Evidence |
| --- | --- |
| Firmware flashes and boots | `Hash of data verified`, `[BOOT] Control Hub starting`, chip `ESP32-D0WD-V3` |
| Display comes up in landscape | `[TFT ] ST7796 ready: 480x320` |
| NVS survived the partition change | `[TOUCH] Loaded calibration: 298 3456 399 3160 7` and `[WIFI] 3 saved network(s)` — no recalibration and no re-adding, because the `nvs` partition kept its offset. This corrects the earlier assumption that the new table would wipe them. |
| microSD mounts on CS IO5 | `store=sd` in the `[PB  ]` line: the CH-07 card works and the pin inherited from the portalbox project was right |
| PortalBox initializes and the UI builds | `[PB  ] PortalBox ready: store=sd portals=0 captures=0`, `[UI  ] Screen -> dashboard` |
| Wi-Fi link state machine runs | `State -> OFFLINE`, `No saved network in range - opening setup portal`, AP `ControlHub-A87C` |

One real defect surfaced and was fixed the same day:

```
E esp_littlefs: partition "spiffs" could not be found
E LittleFS.cpp: Mounting LittleFS failed! Error: 261
```

Arduino's `LittleFS.begin()` looks for a partition labelled `spiffs` by default, and `partitions.csv` labels ours `littlefs`. The flash portal library was therefore invisible — `portals=0` was misleading rather than empty — and with a card mounted PortalBox could not seed the card from flash either. `pbBegin()` now mounts with the explicit label, reports `store=none` plus a warning when neither backend is available, and logs the mounted card size and flash usage. The portalbox project carries the same latent bug in its own partition table.

`esp_core_dump_flash: No core dump partition found` at boot is expected noise: this table has no coredump partition, so post-mortem backtraces are not saved to flash.

## Next bench work

1. Re-flash the fixed firmware. Order matters: run `pio run -t uploadfs` first so `data/portals/Default.html` lands in LittleFS, then `pio run -t upload`, because the upload resets the board and that reset is when the flash-to-card seed runs. The `[PB  ]` lines should then report `portals=1`, list `Default.html`, and print the card size and flash usage.
2. Hold the WIFI PENTESTER card and confirm the screen opens on a hold and not on a tap.
3. Start the portal, join `Free WiFi` from a phone, and confirm the login page pops by itself. Submit a test entry, then check the counter, the last-email line, and `pb capture dump`.
4. Stop the portal and confirm the hub rejoins its network, the LAN scan runs again, and the fan card returns to live values.
5. Only then set `pb ssid` / `pb ch` for real use, and bench-measure the speaker pin before enabling the beep.

## Still unverified

Nothing in this feature has been exercised past initialization: no access point has been started, no page served, no client joined, no capture written, no portal file listed, and the hold gesture has not been touched. The radio handover and the resume path are unexercised, and the Server Fan screen's live API exchange is still unverified from the previous handoff.
