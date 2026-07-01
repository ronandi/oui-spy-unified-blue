# M5 Cardputer ADV Support — Design

**Date:** 2026-06-30
**Status:** Proposed (awaiting review)
**Target hardware:** [M5Stack Cardputer ADV](https://shop.m5stack.com/products/m5stack-cardputer-adv-version-esp32-s3) — SKU K132-Adv

## 1. Goal

Add first-class support for the M5Stack Cardputer ADV so it can be built, flashed,
and OTA-updated as an OUI-SPY device, consistent with the existing XIAO-S3 and
S3-DevKitC boards.

## 2. Hardware facts that drive the design

From M5's official spec/pinmap:

| Property        | Value                                                        |
| --------------- | ----------------------------------------------------------- |
| Core module     | **Stamp-S3A**, based on **ESP32-S3FN8**                      |
| Flash           | **8 MB**                                                     |
| **PSRAM**       | **None** (FN8 has no embedded PSRAM)                         |
| USB             | ESP32-S3 native USB-Serial/JTAG (VID `0x303A`)              |
| Onboard RGB LED | WS2812, **power-gated by PWR_EN on GPIO 38** (shared w/ LCD backlight) |
| RGB data pin    | GPIO 21 (Stamp-S3/S3A convention — **validate on first flash**) |
| Display         | ST7789V2 1.14" (G33–G38) — *not used by this firmware*       |
| Keyboard        | TCA8418 I²C matrix (G8/G9 SDA/SCL, G11 INT) — *not used*     |
| Download mode   | Power switch OFF, hold G0, apply power                       |

### The critical difference

**Every existing S3 environment in `platformio.ini` assumes octal PSRAM** —
`-DBOARD_HAS_PSRAM`, `board_build.arduino.memory_type = qio_opi`,
`-mfix-esp32-psram-cache-issue`. The Cardputer ADV has **no PSRAM**. Copying an
existing S3 env verbatim would attempt to initialize non-existent octal PSRAM at
boot. The port's correctness hinges on a no-PSRAM memory config
(`qio_qspi`, no `BOARD_HAS_PSRAM`).

## 3. Scope (confirmed with user)

**In scope — "Node + Manager", full integration:**

1. **Node** build env (`v3_app_controlled_cardputer_adv`) — the detection sensor:
   runs the engines (`detector`, `flock_ble`, `flock_wifi`, `foxhunter`,
   `skyspy`, `unipwn`), app-controlled over BLE GATT. Primary deliverable; the
   ADV's 1750 mAh battery makes it a natural portable node.
2. **Manager** build env (`v3_node_manager_cardputer_adv`) — the ESP-NOW mesh RX
   aggregator (`OUISPY_ROLE_MANAGER=1`, `manager/manager_main.cpp`, no engines).
   Near-free once the node env exists; keeps the ADV selectable in both roles
   like every other S3 board.
3. **Status LED wired up** — drive PWR_EN (GPIO 38) high at boot so the existing
   NeoPixel status code works on the WS2812 data pin.
4. **Full integration** — custom board JSON, both build envs, companion app board
   list, and README/docs.

**Out of scope (non-goals):**

- Driving the ST7789 LCD or TCA8418 keyboard. This firmware is headless
  (app-controlled); no display/input layer exists. A standalone-UI build is a
  separate, much larger effort.
- Audio (ES8311 codec), IMU (BMI270), IR, SD card, battery ADC fuel-gauge.
  These have no consumer in the current firmware. The simple `PIN_BUZZER` GPIO
  tone path does **not** map to the ADV's I²S codec, so audible buzzer feedback
  is not provided in v1 (LED is the status channel).

## 4. Board identity

New identifier: **`OUISPY_BOARD = "cardputer_adv"`**.

This single string is a cross-component contract:
- Embedded in the BLE GATT device-info string (`ble_gatt.cpp:1600`, `:1688`).
- Becomes the `{board}` token in the companion OTA asset name
  `oui-spy-{role}-{board}-v{ver}.bin` (`ota_service.dart`).
- Must be byte-identical between the `-DOUISPY_BOARD` flag and the app's
  `_nodeBoards` list, or OTA cannot resolve the asset.

## 5. Components & changes

### 5.1 `boards/m5stack-cardputer-adv.json` (new)

Self-contained custom board (mirrors the existing
`boards/esp32-s3-devkitc1-n16r8.json` pattern; PlatformIO isn't installed
locally and the M5 board def can't be assumed present). Key fields vs. the
devkitc JSON:

- `memory_type`: **`qio_qspi`** (was `qio_opi`)
- `extra_flags`: **drop `-DBOARD_HAS_PSRAM`**; keep `-DARDUINO_ESP32S3_DEV`
- **no** `psram_type`
- `flash_size` / `maximum_size`: **8 MB** (`8388608`)
- `partitions`: 8 MB layout (reuse repo `partitions.csv`, set at env level)
- `hwids`: `["0x303A", "0x1001"]` (S3 native USB)

### 5.2 `platformio.ini` — two new envs

**`[env:v3_app_controlled_cardputer_adv]`** — node. Modeled on
`v3_app_controlled` (XIAO S3, also 8 MB) but:
- `board = m5stack-cardputer-adv`
- **remove** `-DBOARD_HAS_PSRAM`, `-mfix-esp32-psram-cache-issue`,
  `board_build.arduino.memory_type = qio_opi`
- add `-DARDUINO_USB_MODE=1` (native USB, like devkitc)
- `-DOUISPY_BOARD=\"cardputer_adv\"`
- `-DPIN_NEOPIXEL=21`, `-DPIN_LED_PWR_EN=38`, `-DOUISPY_BOARD_CARDPUTER_ADV=1`
- `board_build.partitions = partitions.csv`, `flash_size = 8MB`
- identical `build_src_filter` and `lib_deps` to the node family

**`[env:v3_node_manager_cardputer_adv]`** — manager. Same board/memory config as
above, but with the manager flags/sources: `-DOUISPY_ROLE_MANAGER=1`,
`build_src_filter` swapping `main_unified.cpp` → `manager/manager_main.cpp`
(mirrors `v3_node_manager_s3`).

### 5.3 Firmware — LED power-enable hook

The ADV's WS2812 is dark unless PWR_EN (GPIO 38) is high. Add a minimal,
board-gated init so the existing status-LED code lights up:

- In `protocol.h`, default `PIN_LED_PWR_EN` (unset elsewhere; only the ADV
  defines it).
- In `main_unified.cpp`'s LED init path (near `OUISPY_LED_INIT`, ~line 37) and
  the manager's equivalent, add:
  ```c
  #ifdef OUISPY_BOARD_CARDPUTER_ADV
    pinMode(PIN_LED_PWR_EN, OUTPUT);
    digitalWrite(PIN_LED_PWR_EN, HIGH);   // power the WS2812 / backlight rail
  #endif
  ```
  This runs before the first `neopixelWrite(PIN_NEOPIXEL, ...)`. No effect on
  other boards.

### 5.4 Companion app

- `device_config.dart:4231` — add `'cardputer_adv'` to `_nodeBoards`.
- No `ota_service.dart` logic change needed — board flows through as a string;
  only the list of selectable boards expands. Verify the OTA asset-name builder
  consumes the new id unchanged.

### 5.5 `flash.py`

Board-agnostic (flashes any `.bin` in `firmware/`; already `FLASH_SIZE=8MB`).
No code change required for function; update any board-label/banner listing if
one enumerates supported boards.

### 5.6 Docs

- `README.md` — add the Cardputer ADV to the supported-boards table with its
  env names, flash/PSRAM note, and download-mode instructions (power OFF + G0).

## 6. Data flow (unchanged from existing boards)

```
[ADV scans BLE/WiFi] → engines → det_spool → BLE GATT → companion app
                                         └→ ESP-NOW mesh → manager (aggregator)
```

The ADV slots into the existing node/manager topology with no protocol changes.

## 7. Risks & validation

| Risk | Mitigation |
| ---- | ---------- |
| WS2812 data pin assumed GPIO 21 | **Confirmed** = GPIO 21 via M5Unified + Plume (`neopixelWrite(21,…)`); PWR_EN = GPIO 38. No longer a risk. |
| No-PSRAM spool sizing | `det_spool` cap may be tuned lower for 8 MB / no-PSRAM RAM budget; confirm against XIAO-S3 (also 8 MB) which already runs without issue |
| OTA asset id mismatch | Single source of truth: `cardputer_adv` used identically in flag + app list |
| Can't build locally (no pio) | **RESOLVED** — built locally via mise+pio (2026-07-01): node `[SUCCESS]` RAM 33.4% / Flash 43.6%, manager RAM 33.9% / Flash 36.3%. See README "Toolchain (mise + PlatformIO)". |

## 8. Acceptance criteria

1. `pio run -e v3_app_controlled_cardputer_adv` compiles cleanly.
2. `pio run -e v3_node_manager_cardputer_adv` compiles cleanly.
3. Flashed ADV boots (no PSRAM-init boot loop), advertises over BLE, and the
   companion app connects and drives engines.
4. Onboard RGB reflects status (PWR_EN high).
5. `cardputer_adv` selectable in the app's node-board picker; OTA asset resolves.
6. README documents the board and its download-mode entry.

## 9. Reference implementations & confirmed hardware facts

Cross-checked against two working ADV projects: **[Plume](https://github.com/zmattmanz/plume)**
(standalone Flock/Raven detector for the ADV) and **[Bruce](https://github.com/BruceDevices/firmware)**.

- **No-PSRAM/8 MB/qio** board config: matches Bruce's `m5stack-cardputer-adv.json` and
  M5's own recommended env (`board = esp32-s3-devkitc-1` + M5Cardputer lib).
- **RGB LED = GPIO 21** (WS2812; Bruce labels the part SK6812 RGBW — a 3-byte
  `neopixelWrite` still lights it, W channel unused). **PWR_EN = GPIO 38**, shared
  with the LCD backlight — headless firmware must drive it high explicitly.
- **No piezo** — audible feedback on the ADV is the ES8311 I²S codec via
  `M5Cardputer.Speaker.tone()`. GPIO-buzzer path is compiled out (`OUISPY_NO_BUZZER`).
- **On-device GPS recipe** (for the future feature; e.g. Cap LoRa-1262 ATGM336H):
  `HardwareSerial(2)`, **ESP RX = GPIO 15, ESP TX = GPIO 13**, baud auto-detect
  starting 9600 (then 115200/38400), `TinyGPSPlus`, `setRxBufferSize(256)`.
  (Note: RX/TX are 15/13 — the reverse of a naive reading of the Cap-bus labels.)
- **No-PSRAM heap caution**: after WiFi+NimBLE, free internal heap is tight; validate
  PCAP (2×16 KB internal) and the `det_spool` internal fallback cap on hardware.

## 10. Detection cross-pollination — research notes

oui-spy already shares lineage with Plume (`flock_oui.h` credits `zmattmanz/flock-detection`)
and is a superset on detection (67 OUIs, active wildcard-probe TX, addr1/2/3 OUI match,
mfg `0x09C8`, Raven UUIDs, TN-serial, Penguin-decimal). Deltas evaluated:

- **Plume's "pending" OUIs — DO NOT ADD** (IEEE-registry verified, all false-positive risks):
  - `4c:6e:44` → *IEEE Registration Authority* (MA-M/MA-S **shared block**; a /24 match
    over-matches unrelated vendors).
  - `d8:a0:d8` → **unregistered** (not in IEEE DB; speculative).
  - `a0:b7:65` → **Espressif Inc.** (matches *every* ESP32/ESP8266 device — huge FP surface).
  oui-spy correctly excludes all three.
- **SSID patterns — nothing to add**: Plume's `OFS_IoT`/`PFS_` are already caught by
  oui-spy's existing `"FS_"` pattern, since the matcher is `strcasestr()`
  (case-insensitive **substring**) — `OFS_IoT`/`PFS_` both contain `FS_`. oui-spy's
  substring set subsumes Plume's entire SSID list.
- **`00:09:01` (Shenzhen Shixuntong / XUNTONG, Penguin battery)**: low value as a WiFi OUI —
  the Penguin is already caught via **BLE mfg ID `0x09C8`** which oui-spy matches.
- **5 GHz reality (corrects an earlier over-claim)**: Flock Falcon V2 uses a LiteOn
  802.11 a/b/g/n/**ac** chipset (FCC ID WCBN3510A) so the hardware is 5 GHz-*capable*, but
  field observations put Flock WiFi on **2.4 GHz ch 1/6/11**, WiFi is a **setup/maintenance**
  interface (not always-on), and backhaul is **cellular (LTE)**. A 5 GHz sniffer (needs an
  **ESP32-C5**, dual-band; C6 is 2.4 GHz-only) is edge-case completeness, **not** a critical
  gap — 2.4 GHz WiFi + BLE (already covered) catches Flock in practice. Source:
  ryanohoro.com Falcon teardown, Wikipedia.
- **Deferred**: graded 0–100 confidence in the detection payload; GPS-UTC timestamping for
  detections (adopt with the on-device-GPS feature — prefer GPS UTC, fall back to monotonic).
