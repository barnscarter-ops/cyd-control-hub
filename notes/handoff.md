# Handoff

**Date:** 2026-09-30

## Where the firmware is

The dashboard keeps its four cards. `WIFI PENTESTER` opens a `PortalBox` screen on a **tap-tap-hold** gesture (two taps within 400 ms, then a 1500 ms hold); any other pattern only flashes the card, and the card's meta line reads `Locked` so the gesture is not printed on screen. PortalBox is the captive-portal toolkit from `../portalbox`, ported into this firmware as `src/portalbox.*` — own access point, wildcard DNS, `/get` capture contract, portal library and capture log on the microSD card with a LittleFS fallback, and the same screen-level controls the original had (start/stop, next portal, clear, dump, beep).

While that access point is up the hub's own link is `LinkState::Suspended`: the station radio is off, the setup portal refuses to open, the LAN scan stops, and fan polling pauses. Stopping the portal restores the link and its state machine. The contract is written down in `design/architecture/portalbox-v1.md`.

The display is landscape-only but flippable 180° from a rotate-icon button on the Dashboard header and the Network header (and the `flip` console command), persisted in NVS namespace `display`. The Server Fan screen and its asynchronous client are unchanged from the previous handoff.

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

## Second bench run — 2026-09-30 (full loop proven)

Flashed over USB serial (COM23, then COM24 after a replug) with `uploadfs` then `upload`. Everything in the previous "unverified" list has now been exercised on hardware:

| Claim | Evidence |
| --- | --- |
| Portal pages seed from flash to card | `portals=4` after the card already held `Airport.html`/`Default.html` from the first run — no re-seed line, matching the sync's never-overwrite rule |
| Page cloning works online | `[PB ] Cloned 773 B with 1 asset(s)` (Smoke.html from example.com) and `[PB ] Cloned 1448 B with 0 asset(s)` (Form.html from httpbin.org/forms/post) |
| AP serves the selected page | `[PB ] Portal AP up: ssid="Free WiFi" ch=6 ip=192.168.4.1 portal=Form.html` |
| A phone joins and submits | `[PB ] Capture 1 from 192.168.4.2 portal=Form.html` and `Capture 2 from 192.168.4.3` |
| Capture log is real | `pb capture dump` printed `1790778400,192.168.4.2,Form.html,...` — `epoch` non-zero because NTP had set the clock while online before `pb start`, exactly as designed |
| Radio handover back works | After `pb stop`: `State -> SUSPENDED` → `Portal AP down` → `State -> SEARCHING` → `ONLINE` on the saved network |
| Tap-tap-hold opens PortalBox | `[UI ] tap-tap-hold on WIFI PENTESTER - opening PortalBox` after several quick taps + hold; single taps only logged `Card tapped` |
| 180° flip works | `[TFT ] Rotation -> landscape inverted` then back to `landscape`; `tap 350 24` toggled it both ways |

One cosmetic `esp_wifi_get_mac failed with 12289` appeared during the `pb stop` handover and recovered to ONLINE immediately — radio-mode-switch noise, not a fault.

## Fixed this run

1. **`/get` field mapping.** `handleGet()` matched email-ish/password-ish fields by testing `!email.length()`, so an *empty* submission to an email field left `email` empty and the "unknown shape" fallback dumped every other field into the email column. It now tracks `sawEmail`/`sawPass` booleans (a field *name* was seen, even if empty) and only falls back when neither was seen. Documented in `portalbox-v1.md`.
2. **Tap-tap-hold gesture.** Replaced the old 600 ms hold with the hidden tap-tap-hold sequence above; `LONG_PRESS_MS` is 1500 ms and `TAP_WINDOW_MS` is 400 ms.
3. **PortalBox hint overlap.** The bottom status line collided with the `pb ssid | pb ch` reminder; it now `fitText`s the status to the space left of the reminder.
4. **Display flip.** `displayBegin()`/`displayFlip()` in `gfx.*`, touch mirrored in `touch_input.cpp` for the 180° rotation, rotate-icon buttons on Dashboard + Network, `flip` console command.

## Still unverified / next bench work

- **Touch alignment after flip has not been physically confirmed.** The mirror transform is correct by construction (`x = SCREEN_W-1-x`, `y = SCREEN_H-1-y`) and the button/command both toggle rotation, but the resistive panel mapping after a real 180° rotation still needs a finger test: flip, then confirm the back button and cards register in the right places.
- The speaker pin is still unmeasured — keep beep off.
- The Server Fan screen's live API exchange remains unverified from the previous handoff.
- `pb ssid` / `pb ch` have not been set for real use; the AP has only run as the default `Free WiFi`.
