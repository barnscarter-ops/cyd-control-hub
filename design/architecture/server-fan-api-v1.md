# Server Fan Controller LAN API v1

This client contract mirrors the authoritative sibling document at
`../server-fan-controller/design/architecture/http-api-v1.md` as reviewed on
2026-09-28. The fan controller owns sensing, PWM, tachometer health, and all
fail-safe decisions. The Control Hub displays reported state and never treats a
submitted command as applied until the controller returns a successful full
status response.

## Transport and compatibility

- Base URL: `http://<controller>/api/v1`
- Media type: `application/json`
- Authentication is not implemented in the prototype; use only on the trusted LAN.
- Additive fields may appear in API v1 and clients ignore unknown fields.
- The Hub uses asynchronous requests with short deadlines. The controller keeps
  cooling locally before Wi-Fi joins and through all network failures.

## GET `/status`

Response `200`:

```json
{
  "api_version": "1.0",
  "device": {"id": "server-fan-controller-01", "name": "Server Fan Controller"},
  "controller": {
    "mode": "auto",
    "failsafe_reason": null,
    "target_c": 35.0,
    "manual_duty_pct": 50.0
  },
  "sensors": {"temperature_c": 31.25, "valid": true},
  "fans": [
    {"id": "fan-1", "rpm": 1420, "duty_pct": 48.0, "healthy": true},
    {"id": "fan-2", "rpm": 1395, "duty_pct": 48.0, "healthy": true}
  ],
  "updated_ms": 123456
}
```

`controller.mode` is the effective `auto`, `manual`, or `failsafe` mode.
`failsafe_reason` is null during normal operation, or one of
`pwm_init_failed`, `sensor_invalid`, `sensor_stale`, `over_temperature`,
`fan_1_stall`, or `fan_2_stall`. When the sensor is invalid,
`temperature_c` is JSON `null` and `valid` is false. `updated_ms` is controller
uptime; the Hub computes response age from its own receive time.

## PUT `/control`

Accepted auto request:

```json
{"mode":"auto","target_c":35.0}
```

Accepted manual request:

```json
{"mode":"manual","manual_duty_pct":60.0}
```

- `mode` is required and must be `auto` or `manual`.
- `target_c`, when present, is 20.0 through 80.0 degrees C.
- `manual_duty_pct`, when present, is 0.0 through 100.0.
- The controller validates the complete proposed update before applying fields.
- Success is HTTP `200` with the same full status shape as GET `/status`.
- A local fail-safe may override manual mode and duty at any time. The response,
  rather than the submitted value, is the applied state displayed by the Hub.
- Manual 0% is accepted for service, but an invalid or stale sensor or a measured
  fan-inlet ambient of 40 degrees C overrides it to maximum command. That ceiling
  is provisional pending installed sensor placement and rack-temperature tests.

## GET `/health`

Returns `200` while normal or `503` while in fail-safe:

```json
{"api_version":"1.0","status":"ok","updated_ms":123456}
```

`status` is `ok` or `failsafe`. The Hub polls full `/status`; `/health` is not a
substitute for the full display snapshot.

## Errors

Invalid JSON, fields, or ranges return `400`; unknown routes return `404`:

```json
{"api_version":"1.0","error":{"code":"invalid_range","message":"target_c must be 20..80"}}
```

## Hub behavior

The Hub accumulates fragmented HTTP headers and body data, honors a valid
`Content-Length`, and otherwise completes a response when the connection closes.
It validates the HTTP status, API version, and full status shape before replacing
the last confirmed snapshot. User control requests are queued behind another
request only while Wi-Fi is connected. Queue overflow and request failures are
visible errors; neither is shown as an applied command. Wi-Fi loss immediately
marks the endpoint offline and cancels commands that had not started, so stale
control intent is not replayed after an outage. An interrupted in-flight control
is reported with an unknown result, and an old snapshot is marked stale.
