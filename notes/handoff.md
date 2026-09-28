# Handoff

**Date:** 2026-09-28

The dashboard now has a Server Fan screen with touch controls for auto target and manual duty. `fan_link` stores the endpoint in NVS, polls the separate controller asynchronously, queues controls behind an active request, parses full API v1 status, and exposes confirmed temperature, effective mode, fail-safe reason, per-fan RPM/duty/health, stale/offline state, and request errors to the UI. Submitted values stay labeled as queued until a successful controller response supplies the applied state.

The endpoint is set from the serial console with `fan host <ip-or-name> [port]`. Build and exercise fragmented/content-length/connection-close responses against the actual server, verify touch coordinates and screen layout on the CYD, then bench-check its supply and enclosure dimensions before fabrication.

No claims are made here about compilation, upload, screen operation, live network exchange, controller command behavior, or physical fit at the new repository location.
