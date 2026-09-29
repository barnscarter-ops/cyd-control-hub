/* Mirrors components.json (file:// safe). Keep in sync. */
window.DIAGRAM_COMPONENTS = [
 {
  "id": "CH-01",
  "name": "ESP32-32E N4 3.5-inch resistive CYD",
  "model_or_value": "5 V USB input; 320 x 480 ST7796; XPT2046 touch",
  "purpose": "Runs the dashboard and displays controller state.",
  "connections": "USB-C power; Wi-Fi LAN to CH-06; no load-power connection.",
  "inventory_status": "use-owned — 1 recorded in source inventory; condition not tested.",
  "evidence": "hardware/parts.csv; CYD component reference"
 },
 {
  "id": "CH-02",
  "name": "5 V USB supply and USB-C lead",
  "model_or_value": "Regulated 5 V; current rating and condition to inspect",
  "purpose": "Powers CH-01 through its intended USB-C port.",
  "connections": "Wall supply to USB-C on CH-01 only.",
  "inventory_status": "inspect-owned — no suitable supply/lead quantity confirmed.",
  "evidence": "hardware/parts.csv"
 },
 {
  "id": "CH-03",
  "name": "TUOFENG 22 AWG solid-wire kit",
  "model_or_value": "22 AWG solid insulated wire; remaining stock unknown",
  "purpose": "Temporary low-current signal links only.",
  "connections": "Optional external I2C/SPI test link; not load power.",
  "inventory_status": "inspect-owned — owner-reported kit.",
  "evidence": "hardware/parts.csv"
 },
 {
  "id": "CH-06",
  "name": "Server Fan Controller",
  "model_or_value": "Separate 2.4 GHz LAN node; API v1",
  "purpose": "Owns temperature, fan PWM, tachometer, and fail-safe behavior.",
  "connections": "Wi-Fi HTTP/JSON only: /api/v1/status and /api/v1/control.",
  "inventory_status": "use-owned — separate project decides its physical inventory.",
  "evidence": "design/architecture/server-fan-api-v1.md"
 }
];
