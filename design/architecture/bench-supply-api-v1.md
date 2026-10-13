# Bench Supply LAN API v1

This client contract describes the HTTP status API exposed by the ESP32
workstand bench-supply meter firmware (`esp32-workstand/firmware/bench-meter`).
v1 is **monitor-only**: the bench-supply meter owns the INA226 readings, the
temperature-controlled fan curve, and the fail-safe decision. The Control Hub
only displays reported state.

## Transport and compatibility

- Base URL: `http://<bench-supply>/api/v1`
- Media type: `application/json`
- Authentication is not implemented in the prototype; use only on the trusted LAN.
- Additive fields may appear in API v1 and clients ignore unknown fields.
- The Hub uses asynchronous requests with short deadlines. The bench-supply
  meter keeps its own fan/failsafe logic running locally regardless of Wi-Fi
  or Hub polling state.

## GET `/status`

Response `200`:

```json
{
  "api_version": "1.0",
  "device": {"id": "bench-supply-01", "name": "Bench Supply"},
  "meter": {
    "ok": true,
    "volts": 12.34,
    "amps": 1.234,
    "watts": 15.22,
    "mah": 123.4,
    "wh": 1.512
  },
  "sensors": {"air_valid": true, "air_c": 28.5, "humidity_pct": 45.0},
  "fan": {"on": true, "duty_pct": 50.0, "rpm": 1200, "failsafe": false},
  "updated_ms": 123456
}
```

`meter.ok` is false when the INA226 has not been found yet; the numeric meter
fields are then JSON `null`. `sensors.air_valid` is false when the DHT11 has
not reported a plausible sample within the sensor timeout; `air_c` and
`humidity_pct` are then JSON `null`. If the firmware's optional LM35 heatsink
sensor (`USE_LM35`) is enabled, `sensors` additionally carries `sink_valid`
and `sink_c` with the same null-when-invalid convention. `fan.failsafe` is
true when no valid temperature sensor has reported within the sensor timeout,
in which case the fan is forced to 100% regardless of mode. `updated_ms` is
the device's own uptime; the Hub computes response age from its own receive
time.

## PUT `/control`

**Not implemented in v1.** The bench-supply meter is monitor-only: there is
no remote control of the fan or any other actuator. This route is reserved
for a future version once an accepted control contract exists; until then any
request to it returns `404` like any other unknown route.

## GET `/health`

Returns `200` while normal or `503` while the fan fail-safe is engaged:

```json
{"api_version":"1.0","status":"ok","updated_ms":123456}
```

`status` is `ok` or `failsafe`. The Hub polls full `/status`; `/health` is not
a substitute for the full display snapshot.

## Errors

Unknown routes (including `/control` in v1) return `404`:

```json
{"api_version":"1.0","error":{"code":"not_found","message":"route not found"}}
```

## Hub behavior

The Hub accumulates fragmented HTTP headers and body data, honors a valid
`Content-Length`, and otherwise completes a response when the connection
closes. It validates the HTTP status, API version, and full status shape
before replacing the last confirmed snapshot. Wi-Fi loss immediately marks
the endpoint offline. Since v1 has no control route, the Hub's `bench_link`
client has no command queue — it is a pure read-only poller, modeled on
`fan_link` with the write path removed.
