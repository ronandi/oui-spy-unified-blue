#include "ui_status.h"
#ifdef OUISPY_HAS_DISPLAY

// Uses the same library Plume runs on the ADV, so panel/battery are proven.
#include <M5Cardputer.h>
#include <Arduino.h>
#include <string.h>
#include "protocol.h"
#include "engine_registry.h"

// --- cosmetic taps published by main_unified.cpp's detection drain loop ---
extern volatile uint32_t g_totalDetections;
extern volatile uint8_t  g_lastDetEngine;
extern volatile int8_t   g_lastDetRssi;
extern volatile uint32_t g_lastDetMs;
extern volatile uint8_t  g_lastDetMac[6];

// --- state owned elsewhere ---
bool blePhoneConnected(void);            // ble_gatt.cpp
extern volatile GpsData currentGps;      // main_unified.cpp
extern volatile bool     gpsValid;

// RGB565
#define COL_BG    0x0000
#define COL_TEXT  0xFFFF
#define COL_DIM   0x8410
#define COL_OK    0x07E0
#define COL_ALERT 0xFD20   // amber
#define COL_HDR   0x05FF

static const char* engineShort(uint8_t id) {
    switch (id) {
        case ENGINE_DETECTOR:   return "DET";
        case ENGINE_FLOCK_BLE:  return "FLK-B";
        case ENGINE_FLOCK_WIFI: return "FLK-W";
        case ENGINE_FOXHUNTER:  return "FOX";
        case ENGINE_SKYSPY:     return "SKY";
        case ENGINE_UNIPWN:     return "UNI";
        case ENGINE_WARDRIVE:   return "WAR";
        case ENGINE_PCAP:       return "PCAP";
        default:                return "?";
    }
}

// Per-row text cache: repaint a row only when its content changes. No frame
// buffer, no per-frame full clear -> flicker-free on the no-PSRAM ADV.
static char s_cache[6][48];

static void drawRow(int idx, int y, uint16_t fg, const char* text) {
    if (strncmp(s_cache[idx], text, sizeof(s_cache[idx])) == 0) return;
    strncpy(s_cache[idx], text, sizeof(s_cache[idx]) - 1);
    s_cache[idx][sizeof(s_cache[idx]) - 1] = '\0';
    auto& d = M5Cardputer.Display;
    d.fillRect(0, y, d.width(), 16, COL_BG);
    d.setTextSize(1);
    d.setTextColor(fg, COL_BG);
    d.setCursor(4, y + 3);
    d.print(text);
}

static void renderStatus(void) {
    char buf[48];

    // 0 — header + app link
    snprintf(buf, sizeof(buf), "OUI-SPY %s  %s",
             OUISPY_BOARD, blePhoneConnected() ? "APP" : "---");
    drawRow(0, 2, COL_HDR, buf);

    // 1 — active engines
    uint8_t mask = engineGetActiveMask();
    if (mask == 0) {
        drawRow(1, 20, COL_DIM, "ENG  (idle)");
    } else {
        int n = snprintf(buf, sizeof(buf), "ENG");
        for (uint8_t i = 0; i < ENGINE_COUNT && n < (int)sizeof(buf) - 1; i++)
            if ((mask >> i) & 1)
                n += snprintf(buf + n, sizeof(buf) - n, " %s", engineShort(i));
        drawRow(1, 20, COL_OK, buf);
    }

    // 2 — detection total
    snprintf(buf, sizeof(buf), "DET  %lu", (unsigned long)g_totalDetections);
    drawRow(2, 38, COL_TEXT, buf);

    // 3 — last hit (age ticks each second)
    if (g_lastDetMs) {
        unsigned long age = (millis() - g_lastDetMs) / 1000UL;
        snprintf(buf, sizeof(buf), "LAST %s %02X%02X %ddBm %lus",
                 engineShort(g_lastDetEngine), g_lastDetMac[4], g_lastDetMac[5],
                 (int)g_lastDetRssi, age);
        drawRow(3, 56, COL_ALERT, buf);
    } else {
        drawRow(3, 56, COL_DIM, "LAST --");
    }

    // 4 — GPS (from phone app, or on-device GPS once added)
    if (gpsValid) {
        snprintf(buf, sizeof(buf), "GPS  %.4f,%.4f",
                 currentGps.latitude, currentGps.longitude);
        drawRow(4, 74, COL_TEXT, buf);
    } else {
        drawRow(4, 74, COL_DIM, "GPS  no fix");
    }

    // 5 — battery
    int batt = M5Cardputer.Power.getBatteryLevel();  // 0..100, <0 if unknown
    if (batt >= 0) snprintf(buf, sizeof(buf), "BATT %d%%", batt);
    else           snprintf(buf, sizeof(buf), "BATT --");
    drawRow(5, 92, COL_TEXT, buf);
}

static void UiStatusTask(void* pv) {
    (void)pv;
    for (;;) {
        renderStatus();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void uiStatusInit(void) {
    M5Cardputer.begin();
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(160);
    M5Cardputer.Display.fillScreen(COL_BG);
    memset(s_cache, 0, sizeof(s_cache));
    // Low priority on core 1 so it never competes with the radios (core 0).
    xTaskCreatePinnedToCore(UiStatusTask, "UiStatus", 3072, NULL, 1, NULL, 1);
    Serial.println("[UI] Status display started");
}

#endif // OUISPY_HAS_DISPLAY
