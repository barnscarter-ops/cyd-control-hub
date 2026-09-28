# Control Hub — Agent Guide

Home-automation dashboard firmware for a 3.5" CYD (Cheap Yellow Display): ESP32-WROOM-32E + ST7796 SPI panel (320x480 native, used landscape 480x320). PlatformIO + Arduino framework.

## Commands

PlatformIO CLI may not be on PATH; use its installed path or the VS Code PlatformIO toolbar. Detect the board's current serial port before each upload or monitor session; an earlier port is not proof of the current connection.

```sh
~/.platformio/penv/Scripts/pio.exe run                      # build
~/.platformio/penv/Scripts/pio.exe run -t upload            # flash after identifying board/port
~/.platformio/penv/Scripts/pio.exe device monitor           # serial @ 115200, with detected port
~/.platformio/penv/Scripts/python.exe tools/snap.py <detected-port> out.png [--cmd "screen network"]  # screenshot
python tools/gen_fonts.py                                   # regenerate include/fonts/*.h (needs Pillow)
```

Single env: `cyd35`.

## Layout

- `platformio.ini` — board, libs, and **all TFT_eSPI config** via `build_flags`.
- `src/main.cpp` — glue only: `setup()` inits each module once, `loop()` polls them.
- `src/gfx.*` — the global `tft`, smooth-font helpers (`text`, `fitText`), and primitives (`card`, `pill`, `button`, `signalBars`, `headerRule`, `logoMark`, `iconBadge`). Every helper takes a `TFT_eSPI&` so it draws to the panel or a sprite.
- `src/ui.*` — screens (Dashboard, Network, Setup, Fan) and navigation. `uiTick()` redraws only what changed.
- `src/fan_link.*` — asynchronous HTTP client for the separate server fan controller's versioned API.
- `src/wifi_link.*` — saved networks, auto-connect state machine, WiFiManager captive portal, NTP.
- `src/lan_scan.*` — ARP sweep of the subnet to list connected devices.
- `src/touch_input.*` — XPT2046 calibration and tap polling.
- `src/console.*` — serial debug commands.
- `include/theme.h` — palette and layout geometry. `include/fonts/` — generated VLW smooth fonts (Inter, JetBrains Mono; both OFL).
- `tools/` — `gen_fonts.py` (font generator), `snap.py` (serial screenshot to PNG).

## Display config rules

- TFT_eSPI is configured only through `build_flags` with `USER_SETUP_LOADED=1`. Never edit the library's `User_Setup.h` / `User_Setup_Select.h` in `.pio/libdeps` — it is overwritten on reinstall and breaks portability.
- Board is the LCDWiki **E32R35T** (ST7796U + XPT2046 resistive touch). Pins: SCLK 14, MOSI 13, MISO 12 (HSPI, `USE_HSPI_PORT`), CS 15, DC 2, RST on EN (`TFT_RST=-1`), BL 27 (active HIGH). The generic CYD pinout (18/19/23) gives a black screen on this board.
- Touch shares the TFT SPI bus: `TOUCH_CS` 33, `TOUCH_IRQ` 36 (active LOW while pressed, input-only). Reference: https://www.lcdwiki.com/3.5inch_ESP32-32E_Display
- `TFT_WIDTH/HEIGHT` are the panel's portrait-native 320/480; `setRotation(1)` gives 480x320.
- `SPI_READ_FREQUENCY` must stay at 10–14 MHz: at 6 MHz and 20 MHz readback (`readRect`, `readPixel`) corrupts bits. Anything that reads pixels depends on this.
- `tft.init()` turns the backlight on itself; `setup()` blanks it until the first frame is drawn.

## UI conventions

- Text uses the smooth fonts via `text(g, s, x, y, Font::..., fg, bg, datum)`; `Font::Ui*` (Inter) for labels, `Font::Mono*` (JetBrains Mono) for IPs, MACs, and numbers. Built-in fonts are only used by `calibrateTouch()`. To add glyphs or sizes, edit `tools/gen_fonts.py` and regenerate — the fonts cover ASCII plus `· ° … •` only.
- Always pass the real background color as `bg`: anti-aliasing blends against it, and it avoids slow pixel readback.
- Colors and geometry come from `include/theme.h`. Cards are `COLOR_PANEL` with a 1px `COLOR_EDGE` border, radius 10; accent is `COLOR_CYAN`; status colors are `COLOR_MATRIX` (good), `COLOR_AMBER` (pending), `COLOR_RED` (fault).
- Anything that redraws after the first frame goes through `offscreen(x, y, w, h, fn)` in `ui.cpp`: it renders to a temporary sprite and pushes it in one go (no flicker), falling back to direct drawing if heap is short. Keep sprites around row/card size — no full-screen sprites (not enough RAM).
- Dashboard cards are the `CARDS[]` table; their live values come from `cardValue(i)`. Card taps highlight for 140 ms, then act in `releaseCard()`.

## Wi-Fi

- No credentials in code. Up to 5 networks are stored in NVS (namespace `wifi`, keys `n`, `s0..s4`, `p0..p4`), newest first. `wifi_link` scans, joins the strongest saved network in range, and rescans every 10 s when none is found or the link drops (`WiFi.setAutoReconnect(false)` — reconnection is ours).
- WiFiManager runs **non-blocking** (`wm.process()` in `linkLoop`) and only collects credentials: its save callback hands them to `linkAddNetwork()`; our state machine does the connecting. The portal opens automatically when nothing is saved or after 2 failed scans, and on demand from Network > Add Wi-Fi. AP is `ControlHub-XXXX` with a password derived from the MAC (both shown on the Setup screen with a Wi-Fi QR code from the IDF `esp_qrcode` component).
- ESP32 is 2.4 GHz only: iPhone hotspots need **Maximize Compatibility**; the Windows hotspot band must be 2.4 GHz or Any.
- Time: `configTzTime` with US Central (`CST6CDT,M3.2.0,M11.1.0`) once online.

## LAN scan

- `lan_scan` sends ARP requests in batches of 8 every 120 ms (two passes, capped at a /24) and harvests the lwIP ARP table. lwIP calls must go through `tcpip_api_call` (core locking is off). The ARP table holds only 10 entries, so devices are collected incrementally into our own list (max 64).
- Device type is a guess: gateway, this hub, "Private MAC" (randomized, locally administered bit), a small OUI table, else "Device". Devices that ignore ARP (rare) won't show.

## Touch

- Calibration (5 x `uint16_t`) is stored in NVS (`Preferences`, namespace `touch`, key `cal`). First boot runs the 4-corner calibration; holding the screen during boot forces re-calibration.
- `touchTapped()` polls every 30 ms, gated by `TOUCH_IRQ` so SPI is only read while pressed, and fires once per press.

## Serial console

Type `help` at 115200 baud. Commands: `status`, `snap` (RLE screen dump for `tools/snap.py`), `tap x y`, `screen dashboard|network|setup|fan`, `scan`, `portal`, `close`, `wifi add <ssid> <pass>`, `wifi list` (blocking scan), `wifi forget`, `fan host <ip-or-name> [port]`, `reboot`.

## Loop rules

- `loop()` must stay non-blocking: no `delay()`, no blocking network calls. Use `millis()` timers. (Exceptions: `setup()`, touch calibration, and debug console commands.)
- Serial log lines use a short tag prefix: `[BOOT]`, `[TFT ]`, `[UI  ]`, `[TOUCH]`, `[WIFI]`, `[LAN ]`, `[CON ]`, `[HB  ]`.

## Libraries

- `bodmer/TFT_eSPI` — display and touch.
- `tzapu/WiFiManager` — captive portal.
- `bblanchon/ArduinoJson` — parses server fan controller status and control responses.
