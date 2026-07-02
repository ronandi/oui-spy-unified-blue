# Wire Extension — GPS in Detections (device ⇄ app contract)

**Date:** 2026-07-01
**Status:** Proposed (coordinated device + app change; NOT yet implemented)
**Depends on:** on-device GPS reader (shipped, display-only).

## 1. Why this needs both sides (no "device-only" shortcut)

On-device GPS currently feeds only the LCD. To geo-stamp *detections* the payload
must carry the detector's position — but you **cannot** just append bytes on the
device, because the app's decoder dispatches on **exact payload length**:

```dart
// companion/lib/core/ble/ble_protocol.dart:37
const v31Sizes = {31:false,36:true,37:true,96:false,101:true,23:false,28:true,47:false,52:true,74:true,155:true};
final isV31 = v31Sizes[bytes.length] ?? (bytes.length >= 19 && engine == Engine.wardrive);
final headerLen = isV31 ? 19 : 14;
```

Appending bytes changes `bytes.length`, the map misses, and every non-wardrive
engine falls back to the wrong 14-byte header → all fields shift by 5 → corrupt.
So GPS-in-detections is a **coordinated contract**: the device emits a trailer AND
the app learns the new lengths. Because the deployed app is a **fixed external
TestFlight build**, rollout must be coordinated with whoever owns that build — the
device MUST NOT emit the trailer until the app that parses it is deployed.

## 2. Wire format (the trailer)

Appended **after** each engine's existing extension, only when the emitting node
has a valid on-device fix. Fixed 17 bytes:

| Offset (from trailer start) | Field | Type |
| --- | --- | --- |
| 0 | `gps_flags` (bit0 = present, bit1 = approx) | `uint8` |
| 1 | latitude | `float64` LE |
| 9 | longitude | `float64` LE |

New total lengths = existing + 17 (e.g. DETECTOR 52→69, WARDRIVE 74→91,
FLOCK 37→54, UNIPWN 28→45, SKYSPY 155→172). Latitude/longitude reuse the exact
encoding already used for `GpsData`/ODID coords (`float64` LE), so the app's
existing helpers apply.

> Rationale for a length-based trailer over a flag bit: the app *already* keys on
> length, so any GPS variant needs new `v31Sizes` entries regardless — an explicit
> `+17` per engine is the least surprising.

## 3. Device side (`ble_gatt.cpp` `packDetection`)

- Guard by `OUISPY_HW_GPS`. When `gpsValid` (read under `g_gpsMux`) and the node
  has a fresh local fix, append the 17-byte trailer and add 17 to `len`.
- `gps_flags.approx = 0` for a live on-device fix (contrast the app's phone
  "last-known" tag which is approx=true).
- No change to the `DetectionEvent` struct or the spool format — the trailer is
  built at notify time from `currentGps`, so stored/mesh-forwarded detections are
  untouched.

## 4. App side (`companion/`)

1. **`ble_protocol.dart`** — add the `+17` lengths to `v31Sizes` (mapped `true`),
   **or** (preferred, future-proof) make `isV31` tolerant: treat any length whose
   `(len - 17)` is a known size, or `len >= perEngineBase`, as v31 and slice the
   trailer off the end. Then in `decodeDetection`, if a trailer is present, read
   `gps_flags`/lat/lon into the returned `Detection`.
2. **`wardrive_state.dart:_tagGps`** — already the right shape:
   ```dart
   return (lat: d.latitude, lon: d.longitude, acc: d.accuracy, approx: false);
   ```
   With the device now supplying `d.latitude/longitude`, on-device fixes flow into
   the DB (`approxGps=false`) and the Wardrive Map — no phone GPS required. The
   existing `offlineGpsTag` phone-last-known path stays as the fallback.

## 5. Backward compatibility & rollout

- **App must ship first (or together).** An old app + new device = corrupt
  detections (§1). Gate the device emission behind a capability the app advertises,
  or simply don't enable `OUISPY_HW_GPS` trailer emission until the parsing app is
  deployed.
- Old device + new app is safe (new app still recognizes the legacy lengths).
- Verifiable from source: the change is localized to `packDetection`,
  `v31Sizes`/`decodeDetection`, and `_tagGps`.

## 6. Non-goals

- No `DetectionEvent` struct change, no spool/mesh format change (trailer is
  notify-time only).
- Not enabled until the app side is deployed by the TestFlight owner.
- Does not change the phone-GPS push (`CHR_GPS_RECEIVE`) or the `suppressAlerts`
  piggyback.
