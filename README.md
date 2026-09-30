# Control Hub

**Stage:** firmware prototype; working-assembly proposal pending measurement and bench verification.

Control Hub is a touch dashboard for the owned 3.5-inch CYD display board. It provisions Wi-Fi, shows network state and LAN devices, and is the local user interface for separately built controllers. It does not control safety-critical outputs directly.

## Current build

The PlatformIO firmware is organized as polling modules: display/UI, touch, Wi-Fi provisioning, LAN discovery, fan-controller client, PortalBox, and serial console. The Server Fan card opens touch controls for auto target and manual duty. The asynchronous client polls reported temperature, mode, per-fan RPM/duty/health, and fail-safe state; it shows offline, stale, and request errors without claiming a submitted command was applied. The controller endpoint is configured with `fan host <ip-or-name> [port]` and stored in device NVS rather than tracked source. Its network interface is defined in [design/architecture/server-fan-api-v1.md](design/architecture/server-fan-api-v1.md).

Holding the **WIFI PENTESTER** card on the dashboard opens **PortalBox**, the captive-portal audit toolkit ported from the sibling `../portalbox` project. It hosts its own access point, redirects captive-portal probes to a login page streamed from the microSD card (LittleFS when no card is mounted), and appends submissions to `/captures/all.csv`. The hub's own Wi-Fi link, the LAN scan, and fan polling are suspended while that AP is up, because one radio cannot do both. The HTTP contract, storage layout, screen controls, and console commands are defined in [design/architecture/portalbox-v1.md](design/architecture/portalbox-v1.md).

The CYD is confirmed owned in the inventory at `../../../Inventory` from the project root. Its display/touch pin configuration is recorded in `platformio.ini` and the component reference cited below. No physical wiring, power source, or enclosure fit has been re-verified for this project at this path.

## Requirements

- Keep the display responsive while Wi-Fi, LAN discovery, and fan-controller requests run.
- Treat the fan controller as the authority for temperature protection and fail-safe operation.
- Use on-hand parts only when ratings, connector fit, strain relief, and reliability meet the build need.
- Do not store Wi-Fi passwords or network addresses in tracked source.

## Navigate

- [hardware/parts.csv](hardware/parts.csv) — bill of materials and inventory decisions.
- [procurement/needed.md](procurement/needed.md) — physical checks and items that must be obtained.
- [hardware/wiring.md](hardware/wiring.md) — known board interfaces and proposed power practice.
- [design/diagrams/breadboard/index.html](design/diagrams/breadboard/index.html) — fixed prototype arrangement.
- [design/diagrams/pcb/index.html](design/diagrams/pcb/index.html) — fixed proposed working assembly.
- [design/architecture/server-fan-api-v1.md](design/architecture/server-fan-api-v1.md) — versioned LAN interface.
- [design/architecture/portalbox-v1.md](design/architecture/portalbox-v1.md) — PortalBox access point, HTTP, and storage contract.
- [partitions.csv](partitions.csv) — 4 MB flash split: one application slot plus LittleFS.
- [data/portals/Default.html](data/portals/Default.html) — portal page seeded into LittleFS by `pio run -t uploadfs`.
- [notes/handoff.md](notes/handoff.md) — exact next bench work.

## Next action

Bench-check PortalBox on the CYD first: hold the WIFI PENTESTER card, confirm the access point appears, join it from a phone, and watch the capture counter and log. Then confirm the hub link, LAN scan, and fan polling resume when the portal is stopped. After that, build and bench-check the fan-client screen with the separate controller, then physically inspect the CYD power input, USB supply, and available mounting hardware before choosing the enclosure and connector strategy from `hardware/parts.csv`.

**Verification status:** the firmware compiles for `env:cyd35` (RAM 15.9% of 327 KB, flash 68.8% of the 1.69 MB application slot). It has not been uploaded, and no access point, capture, portal file, touch gesture, storage, or radio-handover behavior has been exercised on hardware. The microSD card is owner-reported only, and the one serial port present on the bench machine (COM5) is not a CYD USB-serial bridge.
