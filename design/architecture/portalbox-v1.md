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
| Tap `WIFI PENTESTER` | Highlight only. A stray tap never opens the toolkit. |
| Tap-tap-hold `WIFI PENTESTER` | Two taps within 400 ms, then a 1500 ms hold, opens the `PortalBox` screen. Any other pattern only flashes the card. |
| Serial console | `screen portalbox` |

The card's meta line reads `Locked` while the AP is down and
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
| Sync | every boot, and on demand, copies flash pages the card does not already have |

The LittleFS mount names its partition explicitly —
`LittleFS.begin(true, "/littlefs", 10, "littlefs")`. Arduino's default mount
looks for a partition labelled `spiffs`, and `partitions.csv` labels ours
`littlefs`, so omitting that argument fails with
`partition "spiffs" could not be found` and every portal-library read silently
falls back to the built-in page. (The portalbox project carries the same latent
bug in its own partition table.)

Capture rows are `epoch,ip,portal,"email","password","extra"` with commas,
quotes, and newlines sanitized out of the submitted values. Any form field is
accepted: email-ish (`email`/`user`/`login`/`account`) and password-ish
(`pass`/`pwd`/`pin`/`code`) names map to the email and password columns and
everything else is kept in the extra column, so a cloned page's field names do
not have to match the Marauder contract. The email/password match is decided by
whether a field *name* was seen, not whether it held a value — an empty
submission to an email field still lands in the email column (empty), and only
a page with no email-ish or password-ish field at all falls back to dumping
everything into the email column. `epoch` is `0` until NTP
has set the clock, which needs a station connection, so rows captured while the
AP is up are untimed unless the hub was online before the portal started.

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

## Deploying portal pages

`data/portals/` in this repo is the source of truth for the pages that ship with
the firmware; the card is the source of truth at runtime.

| Step | Command | Effect |
| --- | --- | --- |
| 1. Author | add `Name.html` to `data/portals/` | filename is the page name shown on the device and in `pb portal list`; letters, digits, `.`, `-`, `_` only, 40 characters max |
| 2. Image | `pio run -t uploadfs` | builds the LittleFS image from `data/` and writes it to the flash partition |
| 3. Ship | `pio run -t upload` | flashes the application; the reset that follows runs the sync |
| 4. Sync | `pb portal sync`, or **Sync Pages** on the device | copies every flash page the card does not already have |

Sync never overwrites. A page already on the card wins, so pages authored or
edited directly on the card survive a re-seed — which also means a page edited
in `data/` will not replace a card copy on its own. To replace one, delete it
first (`pb portal delete <name>`) and sync again, or edit it on the card.

The library holds at most 16 pages (longest name first come, first served).
`pb portal list` prints the backend, page count, used and total space, the page
currently served, and every page with its size. On the device, the PORTAL
LIBRARY card shows the backend, count, served page, and last captured address,
and **Next Portal** cycles the library one page at a time.

## Cloning a captive portal page

`pb clone <url> [Name.html]` fetches a real portal page and stores a local copy
that the AP can serve with no internet behind it:

- the HTML is fetched (HTTP or HTTPS, no certificate validation) with the
  forms rewritten to `action="/get"` so submissions still reach the capture
  handler;
- `<link rel=stylesheet>` files are fetched and inlined as `<style>`;
- `<img>`, `<script>`, and `<source>` files are saved under
  `/portals/assets/` and their references rewritten to the local
  `/assets/<name>` route, which the server streams with the right content type;
- `<base>` tags are dropped so they cannot re-point the relative links;
- a `<!-- cloned from <url> -->` comment marks the copy, and the page is
  selected and served immediately.

The clone needs the hub online, so the sequence is: join the target network,
clone, then start the AP. `pb clone` is a blocking console command (like
`wifi list`); the display does not update while it runs.

Limits: the page is capped at 48 KB, each asset at 24 KB, and 12 assets per
clone. Anything over the cap is truncated and reported. A JavaScript-heavy
single-page portal will still render from its saved assets, but anything it
loads from its origin server at runtime cannot work behind an AP with no
internet. Classic form-based splash pages are the target this feature is built
for.

## Screen controls

| Control | Action |
| --- | --- |
| `Start Portal` / `Stop Portal` | `pbStart()` / `pbStop()`; the button face follows the live state. |
| `Next Portal` | Cycles the portal library. |
| `Sync Pages` | Copies every flash page the card lacks; never overwrites a card page. |
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
| `pb clone <url> [Name.html]` | Fetch a real portal page into the library; needs the hub online. |
| `pb portal sync` | Copy flash pages the card lacks; prints the number copied. |
| `pb portal delete <name>` | Remove a page from the active store. |
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
