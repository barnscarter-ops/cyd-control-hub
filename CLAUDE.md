# Control Hub

Home-automation dashboard firmware for a 3.5" CYD (Cheap Yellow Display): ESP32-WROOM-32E + ST7796 SPI panel (320x480 native, used landscape 480x320). PlatformIO + Arduino framework.

## Commands

PlatformIO CLI is not on PATH on this machine; use the full path (or the VS Code PlatformIO toolbar):

```sh
~/.platformio/penv/Scripts/pio.exe run                      # build
~/.platformio/penv/Scripts/pio.exe run -t upload            # flash
~/.platformio/penv/Scripts/pio.exe device monitor           # serial @ 115200
~/.platformio/penv/Scripts/pio.exe run -t upload -t monitor # flash + monitor
```

Single env: `cyd35`.

## Layout

- `platformio.ini` — board, libs, and **all TFT_eSPI config** via `build_flags`.
- `src/main.cpp` — display init, dashboard drawing, non-blocking loop.
- `include/`, `lib/`, `test/` — standard PlatformIO dirs (currently empty).

## Display config rules

- TFT_eSPI is configured only through `build_flags` with `USER_SETUP_LOADED=1`. Never edit the library's `User_Setup.h` / `User_Setup_Select.h` in `.pio/libdeps` — it is overwritten on reinstall and breaks portability.
- Board is the LCDWiki **E32R35T** (ST7796U + XPT2046 resistive touch). Pins: SCLK 14, MOSI 13, MISO 12 (HSPI, `USE_HSPI_PORT`), CS 15, DC 2, RST on EN (`TFT_RST=-1`), BL 27 (active HIGH). The generic CYD pinout (18/19/23) gives a black screen on this board.
- Touch shares the TFT SPI bus: `TOUCH_CS` 33, `TOUCH_IRQ` 36 (active LOW while pressed, input-only). Reference: https://www.lcdwiki.com/3.5inch_ESP32-32E_Display
- `TFT_WIDTH/HEIGHT` are the panel's portrait-native 320/480; `setRotation(1)` gives 480x320.
- Only fonts 1 (GLCD), 2, and 4 are loaded. Using another built-in font number requires adding its `LOAD_FONTn` flag.
- `tft.init()` turns the backlight on itself; `setup()` blanks it until the first frame is drawn.

## Touch

- Calibration (5 x `uint16_t`) is stored in NVS (`Preferences`, namespace `touch`, key `cal`). First boot runs the 4-corner calibration; holding the screen during boot forces re-calibration.
- `loop()` polls touch every 30 ms, gated by `TOUCH_IRQ` so SPI is only read while pressed. Taps are edge-triggered (once per press) and hit-tested against `tiles[]` -> `onTileTapped()`.
- A `getBytesLength(): ... NOT_FOUND` log on first boot is expected (no stored calibration yet).

## UI conventions

- Palette constants in `main.cpp`: `COLOR_BG` black, `COLOR_TEXT` white, `COLOR_MUTED` slate wireframes, `COLOR_CYAN` structural accents, `COLOR_MATRIX` green live status. Don't introduce new colors without a reason.
- Minimalist wireframe look: 1px `drawRect` outlines, no filled panels. `+` anchors (GLCD font, via `drawAnchor()`) mark structural intersections.
- Grid geometry: columns X=20/250, rows Y=60/185, tiles 210x105. Tiles live in the `tiles[]` array.
- Update a tile at runtime with `setTileStatus(index, text)` — it redraws only the status line (avoid full-screen redraws; they flicker).
- Always pass a background color to `setTextColor` for text that can change, so old glyphs are overwritten.

## Loop rules

- `loop()` must stay non-blocking: no `delay()`, no blocking network calls. Use `millis()` timers (see the heartbeat pattern).
- Serial log lines use a short tag prefix: `[BOOT]`, `[TFT ]`, `[UI  ]`, `[TOUCH]`, `[HB  ]`.

## Libraries

- `bodmer/TFT_eSPI` — display.
- `bblanchon/ArduinoJson` — included for upcoming home-automation payload parsing; not used yet.
