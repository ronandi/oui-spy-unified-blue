# Cardputer ADV — On-device GPS (UART/NMEA) + GPS-UTC

**Date:** 2026-07-01
**Status:** Proposed (awaiting review)
**Depends on:** Cardputer ADV support (shipped); pairs with the status display.

## 1. Goal

Read a real GNSS fix from a UART GPS (the **Cap LoRa-1262's ATGM336H**, or any
NEO-6M/BN-220-class module) and populate the existing `currentGps`/`gpsValid`
state — so detections carry **on-device coordinates without a phone**, the status
screen's `GPS` row shows a live fix, and detection times can use **GPS UTC**.

Today `currentGps` is written **only** from the phone app over BLE
(`ble_gatt.cpp:616`); `PIN_GPS_RX/TX` exist but nothing reads them.

## 2. Proven recipe (from Plume, verified working on the ADV + this exact Cap)

- `HardwareSerial(2)`, **ESP RX = GPIO 15, ESP TX = GPIO 13**, `SERIAL_8N1`.
- Baud **auto-detect**: try `9600` (ATGM336H default) → `115200` → `38400`.
- `setRxBufferSize(256)`; parse with **TinyGPSPlus** (`mikalhart/TinyGPSPlus`).

## 3. Scope

**In:** a GPS reader task (compile-gated `OUISPY_HW_GPS`) that fills `currentGps`
(lat/lon/alt/speed/heading/sats) + `timestamp_ms` = **GPS UTC epoch-ms** on a
valid fix; enabled on the **node Cardputer env**.

**Out (non-goals):** LoRa messaging/SX1262 driver (separate feature), moving-map
UI, any change to the `DetectionEvent`/GATT wire format (we reuse the existing
`GpsData`), runtime enable/disable from the app (compile-on for now).

## 4. Two-writer arbitration (the real design point)

Both the BLE callback and the GPS task would write the whole `GpsData`. Policy:

- The GPS task, on each **valid fix**, writes `currentGps` and stamps a
  `g_gpsOnboardFreshMs = millis()`.
- The BLE `GpsReceiveCallbacks::onWrite` **skips** its write while on-device GPS
  is fresh (`millis() - g_gpsOnboardFreshMs < GPS_ONBOARD_TTL_MS`, e.g. 10 s).
- Result: **on-device fix takes precedence; phone GPS is the fallback** when there's
  no recent local fix. At most one writer is active at a time, so no torn whole-struct
  race in practice. (A short critical section around the two `memcpy`s is the belt-and-
  suspenders option if needed.)

*Open decision — confirm this priority:* on-device-wins (recommended) vs
phone-always-wins-when-connected.

## 5. Components

- `platformio.ini` (node Cardputer env only): `-DOUISPY_HW_GPS=1`,
  `-DPIN_GPS_RX=15 -DPIN_GPS_TX=13`, `lib_deps += mikalhart/TinyGPSPlus`,
  `build_src_filter += <gps_reader.cpp> <gps_reader.h>`.
- `src/gps_reader.{h,cpp}` — guarded by `OUISPY_HW_GPS`; no-op inline elsewhere.
  - `gpsReaderInit()` — open `Serial2` on PIN_GPS_RX/TX, run baud auto-detect,
    spawn `GpsReaderTask` (prio 1, core 1, ~3 KB stack).
  - task: feed bytes to TinyGPSPlus; on `gps.location.isUpdated()` + valid, write
    `currentGps` (+ UTC epoch if `gps.date/time` valid) and bump `g_gpsOnboardFreshMs`.
- `ble_gatt.cpp` — in `GpsReceiveCallbacks::onWrite`, guard the write with the
  freshness check (only compiled when `OUISPY_HW_GPS`).
- `main_unified.cpp` — call `gpsReaderInit()` in `setup()` (no-op unless flagged).
- `g_gpsOnboardFreshMs` — a `volatile uint32_t` owned by `gps_reader.cpp`,
  read by `ble_gatt.cpp` (extern, guarded).

## 6. GPS-UTC (item 4)

TinyGPSPlus exposes `gps.date`/`gps.time`. On a valid fix with a sane year, set
`currentGps.timestamp_ms` to the **UTC epoch in ms** (fall back to the existing
millis-based value if no date). This gives detections/log lines real wall-clock
time and lets the status screen show UTC. No struct change — `GpsData` already
has an `int64 timestamp_ms`.

## 7. Risks

| Risk | Mitigation |
| --- | --- |
| No GPS attached (bare Cardputer) | Task simply never gets a fix; negligible overhead; `GPS no fix` on screen |
| UART pins vs Cap | 15/13 are the Cap's GPS lines (confirmed); harmless if no Cap |
| Baud/module variance | Auto-detect 9600/115200/38400 (Plume's field-proven set) |
| currentGps race | Freshness gate keeps one active writer; optional critical section |
| RAM | TinyGPSPlus is light (~1 KB state); one 3 KB task; no framebuffer |

## 8. Acceptance criteria

1. Node Cardputer env builds with `OUISPY_HW_GPS`; other envs unchanged/byte-identical.
2. With the Cap LoRa-1262 attached, `currentGps`/`gpsValid` reflect a real fix;
   the status `GPS` row shows coordinates.
3. Detection stamping and app control unaffected; phone GPS still works when no
   local fix.
4. On-device fix takes precedence over phone GPS while fresh.
