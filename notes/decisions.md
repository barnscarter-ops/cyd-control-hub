# Decisions

## 2026-09-28 — Inventory-first build standard

Use the owned CYD as the dashboard. Inspect existing cables, supply, wire, and mounting hardware before obtaining anything. Do not reuse an on-hand part if its rating, physical fit, strain relief, or reliability is uncertain.

## 2026-09-28 — Fan controller separation

Server cooling stays in its own controller project. Control Hub reads status and sends requests through API v1; it does not share a power path or take responsibility for thermal fail-safe behavior.
