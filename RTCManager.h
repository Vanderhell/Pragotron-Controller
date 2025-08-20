/**
 * @file    RTCManager.h
 * @brief   DS1307-based timekeeping with POSIX TZ and optional NTP sync (ESP32).
 *
 * Design
 * ------
 * - AUTO: system time is set by SNTP; RTC is only a cache (do not overwrite system time from RTC).
 * - MANUAL/offline: apply RTC -> system clock to drive time().
 */

#pragma once

#include <Arduino.h>
#include <RTClib.h>
#include <time.h>

class RTCManager {
public:
    // Initialization (with NTP)
    bool begin(const char* ntpServer, int timeZoneOffsetHrs, bool useEUDst);
    bool begin(const char* ntpServer, int timeZoneOffsetHrs);
    bool begin(const char* ntpServer, const char* posixTz);
    bool begin(const char* ntpServer, int timeZoneOffsetHrs, bool useEUDst, int offsetMinutes);

    // Manual mode (no NTP)
    bool beginManual(const char* posixTz);
    bool beginManual(int timeZoneOffsetHrs, bool useEUDst, int offsetMin);

    // Operations
    DateTime now();
    bool     syncWithNtp(int maxAllowedDiffSec = 60);
    bool     adjustRtc(const DateTime& dt);
    bool     isRtcAvailable() const;
    void     applyRtcToSystemClock();

private:
    RTC_DS1307 rtc;
    bool       rtcOk = false;

    // NTP/TZ setup helpers
    bool     setupNtp(const char* ntpServer, int offsetHrs, bool useEUDst);
    bool     setupNtp(const char* ntpServer, int offsetHrs, bool useEUDst, int offsetMinutes);
    bool     setupNtpWithPosix(const char* ntpServer, const char* posixTz);

    // Internals
    DateTime getNtpTime(uint32_t timeoutMs = 7000);
    static String buildTZ(int offsetHours, int offsetMinutes = 0, bool useEUDst = false);
};
