#include "gps_reader.h"
#ifdef OUISPY_HW_GPS

#include <Arduino.h>
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>
#include <string.h>
#include "protocol.h"

extern volatile GpsData currentGps;
extern volatile bool     gpsValid;

volatile uint32_t g_gpsOnboardFreshMs = 0;

static TinyGPSPlus  s_gps;
static HardwareSerial s_serial(2);   // UART2; pins from PIN_GPS_RX / PIN_GPS_TX

// UTC civil date/time -> Unix epoch milliseconds (Howard Hinnant's days_from_civil;
// avoids <time.h>/timezone surprises on the MCU).
static int64_t utc_to_epoch_ms(uint16_t y, uint8_t mo, uint8_t d,
                               uint8_t h, uint8_t mi, uint8_t s) {
    int yy = (int)y - (mo <= 2);
    int era = (yy >= 0 ? yy : yy - 399) / 400;
    unsigned yoe = (unsigned)(yy - era * 400);
    unsigned doy = (153u * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
    int64_t secs = days * 86400 + (int64_t)h * 3600 + (int64_t)mi * 60 + s;
    return secs * 1000;
}

static void open_at(uint32_t baud) {
    s_serial.end();
    s_serial.begin(baud, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    s_serial.setRxBufferSize(256);
}

static void publish_fix(void) {
    GpsData g = {};
    g.latitude  = s_gps.location.lat();
    g.longitude = s_gps.location.lng();
    g.altitude  = s_gps.altitude.isValid()  ? (float)s_gps.altitude.meters() : 0.0f;
    g.speed     = s_gps.speed.isValid()      ? (float)s_gps.speed.kmph()      : 0.0f;
    g.heading   = s_gps.course.isValid()     ? (float)s_gps.course.deg()      : 0.0f;
    g.accuracy  = s_gps.hdop.isValid()       ? (float)s_gps.hdop.hdop()       : 0.0f;
    g.satellite_count = s_gps.satellites.isValid() ? (uint8_t)s_gps.satellites.value() : 0;
    if (s_gps.date.isValid() && s_gps.time.isValid() && s_gps.date.year() >= 2020) {
        g.timestamp_ms = utc_to_epoch_ms(s_gps.date.year(), s_gps.date.month(),
                                         s_gps.date.day(), s_gps.time.hour(),
                                         s_gps.time.minute(), s_gps.time.second());
    }
    memcpy((void*)&currentGps, &g, sizeof(GpsData));
    gpsValid = true;
    g_gpsOnboardFreshMs = millis();
}

static void GpsReaderTask(void* pv) {
    (void)pv;
    const uint32_t bauds[] = { 9600, 115200, 38400 };  // ATGM336H default first
    int bi = 0;
    open_at(bauds[0]);
    uint32_t lastByteMs = millis();

    for (;;) {
        bool any = false;
        while (s_serial.available()) {
            any = true;
            if (s_gps.encode((char)s_serial.read()) &&
                s_gps.location.isUpdated() && s_gps.location.isValid()) {
                publish_fix();
            }
        }
        if (any) lastByteMs = millis();

        // No traffic and no fix yet -> module may be at a different baud; cycle.
        if (!s_gps.location.isValid() && (millis() - lastByteMs) > 3000u) {
            bi = (bi + 1) % (int)(sizeof(bauds) / sizeof(bauds[0]));
            open_at(bauds[bi]);
            lastByteMs = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void gpsReaderInit(void) {
    xTaskCreatePinnedToCore(GpsReaderTask, "GpsReader", 3072, NULL, 1, NULL, 1);
    Serial.printf("[GPS] on-device reader started (RX=%d TX=%d)\n", PIN_GPS_RX, PIN_GPS_TX);
}

#endif // OUISPY_HW_GPS
