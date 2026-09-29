/* Mirrors components.json (file:// safe). Keep in sync. */
window.DIAGRAM_COMPONENTS = [
 {
  "id": "CH-01",
  "name": "ESP32-32E N4 3.5-inch resistive CYD",
  "model_or_value": "5 V USB input; 320 x 480 ST7796; XPT2046 touch",
  "purpose": "Runs the dashboard and displays controller state.",
  "connections": "Plugs into the CH-04 carrier mechanically; USB-C remains service-accessible.",
  "inventory_status": "use-owned — 1 recorded in source inventory; condition not tested.",
  "evidence": "hardware/parts.csv; CYD component reference"
 },
 {
  "id": "CH-02",
  "name": "5 V USB supply and USB-C lead",
  "model_or_value": "Regulated 5 V; current rating and condition to inspect",
  "purpose": "Powers CH-01 through its intended USB-C port.",
  "connections": "Enters the enclosure through a retained service opening.",
  "inventory_status": "inspect-owned — no suitable supply/lead quantity confirmed.",
  "evidence": "hardware/parts.csv"
 },
 {
  "id": "CH-04",
  "name": "Custom low-voltage carrier PCB",
  "model_or_value": "Pending measured mounting pattern and connector selection",
  "purpose": "Provides mechanical support and reviewed low-voltage service connections.",
  "connections": "No fan/load power; optional keyed low-voltage peripheral connectors.",
  "inventory_status": "obtain-required — no carrier exists in recorded inventory.",
  "evidence": "hardware/parts.csv"
 },
 {
  "id": "CH-05",
  "name": "Printed CYD bezel/backplate and M3 hardware",
  "model_or_value": "Dimensions, material, and hardware fit pending measurement",
  "purpose": "Protects board, relieves cable strain, and permits USB service.",
  "connections": "Standoffs secure CH-01 to CH-04/backplate.",
  "inventory_status": "inspect-owned — kit hardware must be measured; printed part proposed.",
  "evidence": "hardware/printed-parts.md"
 },
 {
  "id": "CH-06",
  "name": "Server Fan Controller",
  "model_or_value": "Separate 2.4 GHz LAN node; API v1",
  "purpose": "Owns temperature, fan PWM, tachometer, and fail-safe behavior.",
  "connections": "Wi-Fi HTTP/JSON only; no carrier electrical connection.",
  "inventory_status": "use-owned — separate project decides its physical inventory.",
  "evidence": "design/architecture/server-fan-api-v1.md"
 }
];
