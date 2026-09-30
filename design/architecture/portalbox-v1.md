# PortalBox v1 — captive-portal toolkit inside Control Hub

PortalBox is the captive-portal audit toolkit from the sibling `../portalbox`
project, ported into this firmware as `src/portalbox.*` on 2026-09-30. The hub
owns display, input, and radio arbitration; PortalBox owns the access point and
the HTTP contract while it runs.

This document is the interface between the hub UI, the captive-portal engine,
and the storage layer. It describes intended behavior, not bench-verified
behavior: nothing here has been exercised on hardware yet.

## Entry point

| Action | Result |
| --- | --- |
| Tap `WIFI PENTESTER` | Highlight only (140 ms). The toolkit never opens from a stray tap. |
| Hold `WIFI PENTESTER` for 600 ms | Opens the `PortalBox` screen. |
| Serial console | `screen portalbox` |

The card's meta line reads `Hold to open` while the AP is down and
`<n> clients` while it is up; the value line reads `READY` or `LIVE`.

## Radio ownership

One radio, one access point. PortalBox and the hub's Wi-Fi features are mutually
exclusive:

| Owner | Access point | DNS hijack | Hub link |
| --- | --- | --- | --- |
| `wifi_link` | `ControlHub-XXXX` setup portal (WiFiManager) | yes, by WiFiManager | STA scanning / connected |
| `portalbox` | operator-named, open AP (`Free WiFi` by default) | yes, wildcard to the AP IP | suspended |

- `pbStart()` closes an open setup portal, then calls `linkSuspend()`, which
  turns the station radio off, cancels a pending scan, and sets
  `LinkState::Suspended`. The dashboard pill reads `PENTEST AP`.
- `linkOpenPortal()` refuses to open the setup portal while suspended, so
  Wi-FiManager cannot claim the radio underneath PortalBox.
- `pbStop()` calls `linkResume()`: station mode is restored, the scan restarts,
  and the setup portal reopens automatically only when no network is saved.
- LAN scanning and fan polling stop on their own: both refuse to work without a
  station connection, and the suspended state is visible on the dashboard and
  the fan card rather than shown as a fault.

## HTTP contract

The toolkit is a lab tool for an isolated test network. It serves an open AP and
does not authenticate or encrypt anything.

| Route | Method | Behavior |
| --- | --- | --- |
| `/` | GET | Streams the selected portal page; the built-in fallback page when no file is selected or present. |
| `/get` | GET | Records `email` and `password` query arguments, then serves the acknowledgement page. Same argument contract as the portalbox project and its Marauder-compatible clients. |
| `/ack` | GET | Acknowledgement page. |
| `/generate_204`, `/gen_204`, `/hotspot-detect.html`, `/library/test/success.html`, `/ncsi.txt`, `/connecttest.txt`, `/redirect`, `/canonical.html`, `/success.txt`, `/fwlink` | GET | `302` to `http://<ap-ip>/`, which is what makes the login page pop on a joining device. |
| anything else | GET | `302` to the portal root (wildcard DNS plus not-found redirect). |

Portal HTML is streamed from the filesystem, so a page is not limited by RAM.

## Storage

| Item | Location |
| --- | --- |
| Portal library | `/portals/<Name>.html` |
| Capture log | `/captures/all.csv` and `/captures/<SelectedPortal>.csv` |
| Preferred backend | microSD card on the onboard slot (`CS = IO5`, VSPI), when a card mounts |
| Fallback backend | the LittleFS partition |
| Seed | the first card mount copies `/portals` from flash to the card |

The LittleFS mount names its partition explicitly —
`LittleFS.begin(true, "/littlefs", 10, "littlefs")`. Arduino's default mount
looks for a partition labelled `spiffs`, and `partitions.csv` labels ours
`littlefs`, so omitting that argument fails with
`partition "spiffs" could not be found` and every portal-library read silently
falls back to the built-in page. (The portalbox project carries the same latent
bug in its own partition table.)

Capture rows are `epoch,ip,portal,"email","password"` with commas, quotes, and
newlines sanitized out of the submitted values. `epoch` is `0` until NTP has set
the clock, which needs a station connection, so rows captured while the AP is up
are untimed unless the hub was online before the portal started.

Flash layout (`partitions.csv`) is one application slot plus LittleFS, because
this board is flashed over USB serial and does not need OTA:

| Partition | Offset | Size |
| --- | --- | --- |
| `nvs` | `0x9000` | 24 KB — saved networks, touch calibration, and Wi-FiManager data |
| `factory` (app) | `0x10000` | 1.69 MB |
| `littlefs` | `0x1c0000` | 2.25 MB |

`board_build.filesystem = littlefs` and `board_build.partitions = partitions.csv`
in `platformio.ini`; `pio run -t uploadfs` seeds `data/portals/Default.html`.
The `nvs` partition keeps its offset across this table change, so on CH-01 the
touch calibration and the saved Wi-Fi networks survived the first flash
(2026-09-30). Re-do them only if the `nvs` offset or size changes.

## Screen controls

| Control | Action |
| --- | --- |
| `Start Portal` / `Stop Portal` | `pbStart()` / `pbStop()`; the button face follows the live state. |
| `Next Portal` | Cycles the portal library. |
| `Clear Log` | Deletes every file in `/captures` and resets the counter. |
| `Dump Log` | Streams `all.csv` to the serial console (the only place the captured passwords are shown). |
| `Beep Off` / `Beep On` | Runtime capture beep. Off by default because the speaker pin is unverified on this board. |

Cards show SSID, AP address, channel and clients on the left, and backend,
serving page, capture count and last captured email on the right. Passwords are
never drawn on the panel.

SSID and channel are set from the serial console instead of an on-screen
keyboard: `pb ssid <name>` and `pb ch <1-13>`.

## Console commands

| Command | Effect |
| --- | --- |
| `pb start` / `pb stop` | Start or stop the access point. |
| `pb ssid <name>` | Access-point name used at the next start. |
| `pb ch <1-13>` | Access-point channel used at the next start. |
| `pb portal list` | List the portal library and the page currently served. |
| `pb portal select <name>` | Serve a specific page. |
| `pb capture dump` / `pb capture clear` | Print or delete the capture log. |
| `pb beep on` / `pb beep off` | Capture beep. |
| `status` | Includes portal state, backend, portal count, capture count, and clients. |

## Not carried over from the portalbox project

- The Flipper command link on `UART2` (IO21/IO22). Those pins are not verified
  as free on this board, and the hub's own serial console covers the same
  commands.
- `portal push <name> <bytes>` binary upload over serial. Portal pages are
  deployed with `uploadfs` or by writing to the card.
- `time set <epoch>`. The hub sets the clock from NTP when it is online.
- The dedicated always-on status UI, replaced by the hub screen.

## Known limits

- The access point is open and the toolkit is unauthenticated; it is only for a
  test network the operator controls. Never leave it running on a production LAN
  or a network with other people's traffic.
- The radio handover is one-way per session: the hub link returns only when the
  portal stops.
- The SD card, the speaker pin, the hold gesture's feel on a resistive panel, and
  the HTTP capture path are all unverified on hardware.
