#pragma once
#include <cstdint>

// PCF85063A external RTC on I2C0 (SDA=4, SCL=5, addr 0x51). Gated by
// POWER_EN, so call rtcInit() AFTER driving POWER_EN high in main().
//
// We store LOCAL time on the chip — when the desktop sends
//   {"time":[epoch_sec, tz_offset_sec]}
// we add the offset and treat the result as the local datetime to set.
// That matches upstream's behavior and keeps clock-face rendering trivial.

void rtcInit();

// True once we've successfully written a time-sync from the desktop.
// (The chip itself runs from its coin cell or battery and may hold a
// random datetime on first power-up.)
bool rtcValid();

// Apply a time-sync from the desktop. epoch is seconds since unix epoch,
// tz_offset_sec is the local timezone offset (positive east of UTC).
void rtcSetEpoch(uint32_t epoch, int32_t tz_offset_sec);

// Read current local time/date from the chip. Returns false if the RTC
// hasn't been synced yet (so callers can show "--:--" instead of stale).
struct RtcNow {
    int year, month, day, dotw, hour, min, sec;
};
bool rtcRead(RtcNow* out);
