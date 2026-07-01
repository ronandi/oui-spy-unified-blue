#pragma once
// ============================================================================
// On-device UART/NMEA GPS reader (e.g. Cap LoRa-1262 ATGM336H on the ADV).
// Populates the shared currentGps/gpsValid + GPS-UTC. Compile-gated by
// OUISPY_HW_GPS; a no-op inline on boards that don't build it.
// ============================================================================
#ifdef OUISPY_HW_GPS
#include <stdint.h>

// A local fix takes precedence over phone-pushed GPS for this long after the
// last valid on-device fix (ms). Consumed by ble_gatt.cpp's GPS write callback.
#ifndef GPS_ONBOARD_TTL_MS
#define GPS_ONBOARD_TTL_MS 10000u
#endif

extern volatile uint32_t g_gpsOnboardFreshMs;  // millis() of last valid local fix (0 = never)
void gpsReaderInit(void);
#else
static inline void gpsReaderInit(void) {}
#endif
