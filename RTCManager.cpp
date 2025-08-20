/**
 * @file    RTCManager.cpp
 * @brief   RTC + timezone/NTP orchestration for ESP32 with DS1307 (local time).
 *
 * Key rule:
 * - AUTO (s NTP): systémový čas nastavuje SNTP. NEPREPISUJ ho z RTC.
 * - MANUAL alebo offline fallback: systém nastav z RTC (applyRtcToSystemClock()).
 */

#include "RTCManager.h"
#include <sys/time.h>
#include <time.h>

// ---------------- BEGIN: overloads ----------------

/**
 * @brief Initialize RTC + NTP using an explicit POSIX TZ string.
 */
bool RTCManager::begin(const char* ntpServer, const char* posixTz) {
    rtcOk = rtc.begin();
    if (!rtcOk) { Serial.println("❌ RTC not found."); return false; }

    setupNtpWithPosix(ntpServer, posixTz);

    if (!rtc.isrunning()) {
        Serial.println("⚠️ RTC was not running, setting build time.");
        rtc.adjust(DateTime(__DATE__, __TIME__)); // local fallback
    }

    Serial.printf("🌐 Requesting NTP time from '%s'...\n", ntpServer);
    DateTime ntp = getNtpTime(7000);

    if (ntp.year() >= 2020) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                 ntp.year(), ntp.month(), ntp.day(), ntp.hour(), ntp.minute(), ntp.second());
        Serial.printf("✅ NTP replied with LOCAL time: %s\n", buf);

        // Update RTC cache for offline starts
        rtc.adjust(ntp);
        Serial.println("🕒 RTC updated from NTP (local time stored).");

        // ❌ Do NOT apply RTC -> system clock here (AUTO path). SNTP already set system time.
        // applyRtcToSystemClock();
    } else {
        Serial.println("⚠️ NTP not available, keeping RTC time.");
        // Offline fallback: set system time from RTC so logs/localtime() make sense
        applyRtcToSystemClock();
    }

    return true;
}

/**
 * @brief Initialize with fixed offset (hrs) and EU DST toggle; convenience overload.
 */
bool RTCManager::begin(const char* ntpServer, int timeZoneOffsetHrs, bool useEUDst) {
    return begin(ntpServer, timeZoneOffsetHrs, useEUDst, /*offsetMinutes=*/0);
}

/**
 * @brief Initialize with fixed offset (hrs + minutes) and EU DST toggle.
 */
bool RTCManager::begin(const char* ntpServer, int timeZoneOffsetHrs, bool useEUDst, int offsetMinutes) {
    rtcOk = rtc.begin();
    if (!rtcOk) { Serial.println("❌ RTC not found."); return false; }

    setupNtp(ntpServer, timeZoneOffsetHrs, useEUDst, offsetMinutes);

    if (!rtc.isrunning()) {
        Serial.println("⚠️ RTC was not running, setting build time.");
        rtc.adjust(DateTime(__DATE__, __TIME__));
    }

    Serial.printf("🌐 Requesting NTP time from '%s'...\n", ntpServer);
    DateTime ntp = getNtpTime(7000);

    if (ntp.year() >= 2020) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                 ntp.year(), ntp.month(), ntp.day(), ntp.hour(), ntp.minute(), ntp.second());
        Serial.printf("✅ NTP replied with LOCAL time: %s\n", buf);

        rtc.adjust(ntp);
        Serial.println("🕒 RTC updated from NTP (local time stored).");

        // ❌ Do NOT overwrite system time from RTC when NTP succeeded.
        // applyRtcToSystemClock();
    } else {
        Serial.println("⚠️ NTP not available, keeping RTC time.");
        // Offline fallback
        applyRtcToSystemClock();
    }

    return true;
}

// ---------------- END: overloads ----------------

/**
 * @brief Legacy convenience: offset in hours, EU DST assumed true for SK/Europe.
 */
bool RTCManager::begin(const char* ntpServer, int timeZoneOffsetHrs) {
    if (!ntpServer || strlen(ntpServer) == 0) {
        Serial.println("❌ NTP server missing in config!");
        return false;
    }
    return begin(ntpServer, timeZoneOffsetHrs, /*useEUDst=*/true, /*offsetMinutes=*/0);
}

/**
 * @brief Manual initialization (no NTP): set only TZ and ensure RTC runs.
 */
bool RTCManager::beginManual(const char* posixTz) { // MANUAL path keeps applyRtcToSystemClock()
    rtcOk = rtc.begin();
    if (!rtcOk) { Serial.println("❌ RTC not found."); return false; }

    setenv("TZ", posixTz, 1);
    tzset();

    // Verification
    time_t nowEpoch = time(nullptr);
    struct tm localTm;
    localtime_r(&nowEpoch, &localTm);
    char bufLocal[32], bufOffset[8];
    strftime(bufLocal,  sizeof(bufLocal),  "%Y-%m-%d %H:%M:%S", &localTm);
    strftime(bufOffset, sizeof(bufOffset), "%z",               &localTm);
    Serial.printf("🗺️ (MANUAL) TZ set (posix): %s\n", posixTz);
    Serial.printf("🧭 (MANUAL) Local check: %s (offset %s)\n", bufLocal, bufOffset);

    if (!rtc.isrunning()) {
        Serial.println("⚠️ (MANUAL) RTC was not running, setting build time.");
        rtc.adjust(DateTime(__DATE__, __TIME__));
    }

    // ✅ In MANUAL, system clock comes from RTC
    applyRtcToSystemClock();
    return true;
}

/**
 * @brief Manual initialization (no NTP): build POSIX TZ from fixed offset/EU DST.
 */
bool RTCManager::beginManual(int timeZoneOffsetHrs, bool useEUDst, int offsetMin) {
    String tz = buildTZ(timeZoneOffsetHrs, offsetMin, useEUDst);
    return beginManual(tz.c_str());
}

/**
 * @brief Configure NTP with an explicit POSIX TZ string and verify.
 */
bool RTCManager::setupNtpWithPosix(const char* ntpServer, const char* posixTz) {
    configTzTime(posixTz, ntpServer, "time.nist.gov", "pool.ntp.org");

    // Verify
    time_t nowEpoch = time(nullptr);
    struct tm localTm;
    localtime_r(&nowEpoch, &localTm);

    char bufLocal[32], bufOffset[8];
    strftime(bufLocal,  sizeof(bufLocal),  "%Y-%m-%d %H:%M:%S", &localTm);
    strftime(bufOffset, sizeof(bufOffset), "%z",               &localTm);

    Serial.printf("🗺️ TZ set (posix): %s\n", posixTz);
    Serial.printf("🧭 Local check: %s (offset %s)\n", bufLocal, bufOffset);
    return true;
}

/**
 * @brief Configure NTP using fixed offset or EU DST (internally builds POSIX TZ).
 */
bool RTCManager::setupNtp(const char* ntpServer, int offsetHrs, bool useEUDst, int offsetMinutes) {
    String tz = buildTZ(offsetHrs, offsetMinutes, useEUDst);
    configTzTime(tz.c_str(), ntpServer, "time.nist.gov", "pool.ntp.org");

    // Verify
    time_t nowEpoch = time(nullptr);
    struct tm localTm;
    localtime_r(&nowEpoch, &localTm);

    char bufLocal[32], bufOffset[8];
    strftime(bufLocal,  sizeof(bufLocal),  "%Y-%m-%d %H:%M:%S", &localTm);
    strftime(bufOffset, sizeof(bufOffset), "%z",               &localTm);

    Serial.printf("🗺️ TZ set to: %s\n", tz.c_str());
    Serial.printf("🧭 Local check: %s (offset %s)\n", bufLocal, bufOffset);
    return true;
}

/**
 * @brief Build a POSIX TZ string from either EU DST rules or a fixed offset.
 */
String RTCManager::buildTZ(int offsetHours, int offsetMinutes, bool useEUDst) {
    if (useEUDst) {
        return String("CET-1CEST,M3.5.0/2,M10.5.0/3");
    }
    // POSIX: west-of-UTC sign (UTC+2 => -2)
    int westH = -offsetHours;
    int westM = -offsetMinutes;

    char buf[48];
    if (westM != 0) snprintf(buf, sizeof(buf), "LTZ%+d:%02d", westH, abs(westM));
    else            snprintf(buf, sizeof(buf), "LTZ%+d", westH);
    return String(buf);
}

/** @brief Current DS1307 datetime (local time). */
DateTime RTCManager::now() { return rtc.now(); }

/** @brief True if RTC hardware responded during initialization. */
bool RTCManager::isRtcAvailable() const { return rtcOk; }

/** @brief Write a local datetime to the RTC. */
bool RTCManager::adjustRtc(const DateTime& dt) {
    if (!rtcOk) return false;
    rtc.adjust(dt);
    return true;
}

/**
 * @brief Wait (up to timeout) for NTP to produce a local `tm`, return as DateTime.
 */
DateTime RTCManager::getNtpTime(uint32_t timeoutMs) {
    struct tm timeinfo;
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (getLocalTime(&timeinfo, 250)) {
            DateTime dt(timeinfo.tm_year + 1900,
                        timeinfo.tm_mon + 1,
                        timeinfo.tm_mday,
                        timeinfo.tm_hour,
                        timeinfo.tm_min,
                        timeinfo.tm_sec);

            // Debug: print UTC and LOCAL time
            time_t rawtime;
            time(&rawtime);
            struct tm* gmt = gmtime(&rawtime);
            char bufUtc[32], bufLocal[32];
            strftime(bufUtc, sizeof(bufUtc), "%Y-%m-%d %H:%M:%S", gmt);
            strftime(bufLocal, sizeof(bufLocal), "%Y-%m-%d %H:%M:%S", &timeinfo);

            Serial.printf("🌍 NTP UTC time:    %s\n", bufUtc);
            Serial.printf("🗺️ NTP LOCAL time:  %s\n", bufLocal);
            return dt;
        }
        delay(50);
    }
    Serial.println("❌ Failed to get time from NTP.");
    return DateTime(2000, 1, 1, 0, 0, 0);
}

/**
 * @brief Sync RTC from NTP if drift exceeds threshold (in seconds).
 *        Does NOT overwrite system time in AUTO (SNTP already did it).
 */
bool RTCManager::syncWithNtp(int maxAllowedDiffSec) {
    Serial.println("🔄 Syncing with NTP...");
    DateTime ntp = getNtpTime(5000);
    if (ntp.year() < 2020) {
        Serial.println("⚠️ NTP sync failed.");
        return false;
    }

    DateTime rtcLocal = now();
    long diff = labs((long)(ntp.unixtime() - rtcLocal.unixtime()));

    char bufNtp[32], bufRtc[32];
    snprintf(bufNtp, sizeof(bufNtp), "%04d-%02d-%02d %02d:%02d:%02d",
             ntp.year(), ntp.month(), ntp.day(), ntp.hour(), ntp.minute(), ntp.second());
    snprintf(bufRtc, sizeof(bufRtc), "%04d-%02d-%02d %02d:%02d:%02d",
             rtcLocal.year(), rtcLocal.month(), rtcLocal.day(),
             rtcLocal.hour(), rtcLocal.minute(), rtcLocal.second());

    Serial.printf("📡 NTP time: %s\n", bufNtp);
    Serial.printf("⌛ RTC time: %s\n", bufRtc);
    Serial.printf("🔍 Drift: %ld sec (max allowed %d sec)\n", diff, maxAllowedDiffSec);

    if (diff > maxAllowedDiffSec) {
        Serial.println("⚠️ Drift too big → updating RTC from NTP.");
        rtc.adjust(ntp);          // update RTC cache
        // ❌ do NOT call applyRtcToSystemClock() here
        return true;
    }

    Serial.println("✅ Drift within limits. No update needed.");
    return true;
}

/**
 * @brief Apply DS1307 (local time) to the ESP32 system clock (`time()`).
 *        Used only in MANUAL or when NTP is unavailable.
 */
void RTCManager::applyRtcToSystemClock() {
    if (!rtcOk) return;

    DateTime dt = rtc.now(); // LOCAL time
    struct tm tmLocal = {};
    tmLocal.tm_year = dt.year() - 1900;
    tmLocal.tm_mon  = dt.month() - 1;
    tmLocal.tm_mday = dt.day();
    tmLocal.tm_hour = dt.hour();
    tmLocal.tm_min  = dt.minute();
    tmLocal.tm_sec  = dt.second();

    time_t epoch = mktime(&tmLocal);   // uses current TZ/DST rules
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, nullptr);

    Serial.printf("⏱️ System clock set from RTC (local): %04d-%02d-%02d %02d:%02d:%02d; epoch=%ld\n",
        dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second(), (long)epoch);
}
