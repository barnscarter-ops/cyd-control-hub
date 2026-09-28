# Wiring and power

## Current board facts

`CH-01` is the owner-confirmed 3.5-inch ESP32-32E N4 resistive CYD. The existing firmware targets the LCDWiki E32R35T family: ST7796 display and XPT2046 touch. The project agent guide records the internal HSPI pins and the known external connector labels.

With the rear of the photographed board facing up, USB-C left and ESP32 module right, external labels are:

| Connector | Printed order / orientation | Status |
| --- | --- | --- |
| I2C | GND, IO25 (SCL), IO32 (SDA), 3.3 V, starting nearest the speaker side | Photo-derived; do not treat as bench-verified. |
| SPI | IO21 (CS), IO18 (SCK), IO19 (MISO), IO23 (MOSI), same orientation | Photo-derived. |
| UART | 5 V (triangle/pin 1), GND, TXD, RXD | Photo-derived. |
| BAT | `+` nearest the UART connector; `-` at the other pin | Photo-derived; battery chemistry/charging behavior must be verified before use. |

## Power rule

The installed hub uses a regulated 5 V USB supply through its intended USB-C input. A future carrier may distribute only low-voltage signals and must not route fan/load power through the CYD, breadboard rails, or small signal headers. Any external supply, fuse, connector rating, wire gauge, and strain relief needs a separate approved design and bench check.

## LAN integration

The fan controller has no wire connection to the hub. It joins the same controlled 2.4 GHz LAN and exposes the versioned interface in `design/architecture/server-fan-api-v1.md`. Wi-Fi credentials and its address are runtime configuration; never record them here or in source.
