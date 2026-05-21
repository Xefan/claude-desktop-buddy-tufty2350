#include "rtc.h"

#include <ctime>

#include "pico/stdlib.h"
#include "common/pimoroni_i2c.hpp"
#include "drivers/pcf85063a/pcf85063a.hpp"

namespace {

// Tufty 2350 board pins (BW_RTC_I2C_SDA/SCL).
constexpr uint RTC_SDA = 4;
constexpr uint RTC_SCL = 5;

// Heap-allocated on first rtcInit so we can defer hardware touch until after
// POWER_EN goes high — pimoroni::I2C's constructor calls init() inline.
pimoroni::I2C*       i2c = nullptr;
pimoroni::PCF85063A* pcf = nullptr;
bool                 synced = false;

} // namespace

void rtcInit() {
    if (i2c) return;
    i2c = new pimoroni::I2C(RTC_SDA, RTC_SCL);
    pcf = new pimoroni::PCF85063A(i2c);
    pcf->init();
}

bool rtcValid() { return synced; }

void rtcSetEpoch(uint32_t epoch, int32_t tz_offset_sec) {
    if (!pcf) return;
    // Adding tz_offset to the UTC epoch and feeding the sum to gmtime_r
    // yields the LOCAL date/time components — the chip stores them verbatim.
    time_t local = (time_t)epoch + (time_t)tz_offset_sec;
    struct tm lt;
    gmtime_r(&local, &lt);

    datetime_t dt = {};
    dt.year  = (int16_t)(lt.tm_year + 1900);
    dt.month = (int8_t)(lt.tm_mon + 1);
    dt.day   = (int8_t)lt.tm_mday;
    dt.dotw  = (int8_t)lt.tm_wday;        // 0=Sunday, matches pico-sdk
    dt.hour  = (int8_t)lt.tm_hour;
    dt.min   = (int8_t)lt.tm_min;
    dt.sec   = (int8_t)lt.tm_sec;
    pcf->set_datetime(&dt);
    synced = true;
}

bool rtcRead(RtcNow* out) {
    if (!pcf || !synced) return false;
    datetime_t dt = pcf->get_datetime();
    out->year  = dt.year;
    out->month = dt.month;
    out->day   = dt.day;
    out->dotw  = dt.dotw;
    out->hour  = dt.hour;
    out->min   = dt.min;
    out->sec   = dt.sec;
    return true;
}
