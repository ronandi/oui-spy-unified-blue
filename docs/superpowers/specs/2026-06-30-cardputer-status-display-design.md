# Cardputer ADV — Glanceable Status Display

**Date:** 2026-06-30
**Status:** Proposed (awaiting review)
**Depends on:** Cardputer ADV board support (shipped)

## 1. Goal

A **read-only** status screen on the Cardputer ADV's 240×135 LCD so the device is
usable at a glance **without the phone** — see that it's alive, which engines run,
how much it's caught, the last hit, GPS, and battery. The companion app remains the
sole **control** surface; the screen is output only.

## 2. Scope

**In:** a status screen (mock):
```
┌ OUI-SPY  cardputer_adv    [APP ●] ┐
│ ENGINES  DET  FLK-B  FLK-W  SKY   │
│ DETECTIONS        1,284           │
│ LAST   Flock cam   -63dBm   12s   │
│ GPS    37.77,-122.41    ● fix     │
│ BATT   ███████░   82%             │
└───────────────────────────────────┘
```

**Out (non-goals for v1):** keyboard input, any on-device config, detection-feed
list, visualizations/animations, a full-screen sprite framebuffer, and a manager
screen (node first; manager status is a later extension).

## 3. Hard constraint: no-PSRAM RAM budget

The ADV has no PSRAM; oui-spy already runs WiFi + NimBLE + engines + spool in
~320 KB internal SRAM. Therefore:
- **No framebuffer sprite** (Plume's is ~64 KB). We **direct-draw** text/primitives
  straight to the ST7789 — a few KB.
- **Redraw-on-change** at ~1 Hz from a low-priority task so there's no flicker and
  no radio starvation.
- **Acceptance gate:** after integration, free heap with all node engines enabled
  must stay comfortably positive (target ≥ 30 KB headroom). If the display library
  pushes it under, we drop to M5GFX-only or trim.

## 4. Library choice (the open decision)

| Option | Pros | Cons |
| --- | --- | --- |
| **M5GFX only** (recommended) | Lightest; display-only; smallest RAM/flash | Battery = DIY ADC on G10 (needs divider/cal); no power API |
| **M5Unified / M5Cardputer** | `M5.Power.getBatteryVoltage()` free; proven panel init (Plume uses it); autodetects ADV | Heavier RAM/flash; pulls power/IMU/keyboard we don't need |

**Decision: M5Unified** (battery included). Rationale — **Plume is an existence proof**:
it runs the full M5Unified/M5Cardputer stack **plus a 64 KB sprite** + WiFi-promisc +
NimBLE + SD + GPS on this exact no-PSRAM board. We use **direct-draw, no sprite**, so we
are *lighter than Plume on the heaviest item* and reclaim ~64 KB. The no-PSRAM heap is
therefore not a real doubt — it's a routine first-flash check (§3), not a blocker. Using
M5Unified matches the proven stack and gives `M5.Power.getBatteryLevel()` for free.

## 5. Design

### 5.1 Build gating
- New flag `-DOUISPY_HAS_DISPLAY=1` on `v3_app_controlled_cardputer_adv` only.
  Headless boards and the manager env are unaffected.
- `lib_deps += m5stack/M5GFX` (or `M5Unified`) — Cardputer node env only.
- `build_src_filter += <ui_status.cpp>` (+ header) — compiled only where the flag
  is set (guarded internally with `#ifdef OUISPY_HAS_DISPLAY`).

### 5.2 New files
- `src/ui_status.h` / `src/ui_status.cpp` — display init + render task. Entire body
  guarded by `#ifdef OUISPY_HAS_DISPLAY` so it's a no-op TU on other boards.
  - `uiStatusInit()` — init M5GFX (ST7789V2, rotation), raise backlight (this also
    powers the shared LED rail — makes `OUISPY_BOARD_POWER_INIT` redundant here but
    both are idempotent), spawn `UiStatusTask` (prio 1, core 1, ~2 KB stack).
  - `UiStatusTask` — every ~1 s, read state, redraw only changed fields.

### 5.3 Data sources (all existing or one-line taps)
| Field | Source |
| --- | --- |
| Active engines | `active_engine_mask` / engine_registry (add small accessor) |
| Detection total | new `volatile uint32_t g_totalDetections`, `++` in drain loop (`main_unified.cpp:287`) |
| Last detection | new `g_lastDet` (name/rssi/engine/ms) cached in the same drain loop |
| App connected | new accessor `blePhoneConnected()` returning `ble_gatt.cpp`'s `phoneConnected` |
| GPS | existing `currentGps` / `gpsValid` |
| Battery | M5.Power (if M5Unified) or deferred |

The UI only **reads** these; the detection/BLE paths are untouched aside from two
counter/cache writes in the existing drain loop.

### 5.4 Render approach
- Direct `M5GFX` text draws; keep a `struct` of last-rendered values; only repaint a
  line when its value changed (kills flicker, minimizes SPI traffic).
- Display SPI (G35/36/37) is independent of SD/LoRa SPI (G40/14/39) — no bus contention.

## 6. Risks

| Risk | Mitigation |
| --- | --- |
| RAM: library + engines exceed no-PSRAM heap | M5GFX-only + no sprite; measure free heap; acceptance gate ≥30 KB; fall back to fewer default engines or trim |
| Render task starves radio (core 0) | Task on core 1, prio 1, 1 Hz, direct-draw only |
| M5GFX ADV panel autodetect | Confirmed working in Plume; if flaky, pin an explicit ST7789V2 panel config (pins known: MOSI 35 / SCK 36 / CS 37 / DC 34 / RST 33 / BL 38) |
| Backlight always on = battery draw | Acceptable for v1; dim/off-timer is a later nicety |

## 7. Acceptance criteria

1. `v3_app_controlled_cardputer_adv` compiles with `OUISPY_HAS_DISPLAY`; other envs
   unchanged and still build.
2. On the ADV: status screen shows engines/count/last/GPS and updates live.
3. Free heap with all node engines enabled stays ≥ 30 KB.
4. App control still works identically; detection throughput unaffected.
5. Headless boards produce byte-identical firmware to before (feature fully gated).
