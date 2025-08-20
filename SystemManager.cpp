/**
 * @file    SystemManager.cpp
 * @brief   Orchestrates overall runtime: TZ/RTC/NTP init, minute ticks,
 *          catch-up logic, periodic NTP re-sync, and manual adjustments.
 *
 * Responsibilities
 * ----------------
 * - Initialize timezone/RTC based on configuration (AUTO with NTP vs MANUAL).
 * - Perform an initial NTP sync (AUTO) and detect real DST/TZ jumps (~1h).
 * - Compare persisted clock state (HH:MM) with current local time to decide catch-up.
 * - Generate minute impulses (A/B alternating) via PulseManager.
 * - Run non-blocking catch-up when the stored time lags behind current time.
 * - Periodically re-sync with NTP (AUTO) and handle any drift/DST changes.
 *
 * Notes
 * -----
 * - In AUTO, system time (time_t) is the source of truth; RTC is just a fallback cache.
 * - DS1307 stores **local time**; POSIX TZ rules drive localtime().
 * - All timings/limits come from ConfigManager (see /config.json).
 */

#include "SystemManager.h"
#include <time.h>
#include <stdlib.h> // llabs

// Helper: build a DateTime from the current system LOCAL time
static inline DateTime SystemLocalNow() {
    time_t t = time(nullptr);
    tm lt;
    localtime_r(&t, &lt);
    return DateTime(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                    lt.tm_hour, lt.tm_min, lt.tm_sec);
}

SystemManager::SystemManager(
    ConfigManager* config,
    Logger* logger,
    RTCManager* rtc,
    PulseManager* pulse,
    StateManager* state
) :
    configManager(config),
    logger(logger),
    rtcManager(rtc),
    pulseManager(pulse),
    stateManager(state)
{}

/**
 * @brief Bootstrap the system (see file header for the flow).
 */
void SystemManager::begin() {
    logger->info("🔄 SystemManager starting...");

    const auto& cfg = configManager->getConfig();
    const bool isAuto = (cfg.mode == "auto");

    bool rtcInitOk = false;

    // --- Initialize RTC/TZ according to mode and tz_mode ---
    if (isAuto) {
        if (cfg.tz_mode == "posix" && cfg.posix_tz.length() > 0) {
            logger->info(String("🗺️ [AUTO] TZ=posix: ") + cfg.posix_tz.c_str());
            rtcInitOk = rtcManager->begin(cfg.ntpServer.c_str(), cfg.posix_tz.c_str());
        } else if (cfg.tz_mode == "eu" || cfg.useEuDst) {
            logger->info("🗺️ [AUTO] TZ=eu (CET/CEST)");
            rtcInitOk = rtcManager->begin(cfg.ntpServer.c_str(), /*offsetHrs=*/1, /*useEUDst=*/true);
        } else {
            logger->info(String("🗺️ [AUTO] TZ=fixed: ")
                         + String(cfg.timeZoneOffsetHrs) + ":" + String(cfg.timeZoneOffsetMin));
            rtcInitOk = rtcManager->begin(cfg.ntpServer.c_str(),
                                          cfg.timeZoneOffsetHrs,
                                          /*useEUDst=*/false,
                                          cfg.timeZoneOffsetMin);
        }
    } else {
        // MANUAL: no NTP
        if (cfg.tz_mode == "posix" && cfg.posix_tz.length() > 0) {
            logger->info(String("🗺️ [MANUAL] TZ=posix: ") + cfg.posix_tz.c_str());
            rtcInitOk = rtcManager->beginManual(cfg.posix_tz.c_str());
        } else if (cfg.tz_mode == "eu" || cfg.useEuDst) {
            logger->info("🗺️ [MANUAL] TZ=eu (CET/CEST)");
            rtcInitOk = rtcManager->beginManual(/*offsetHrs=*/1, /*useEUDst=*/true, /*offsetMin=*/0);
        } else {
            logger->info(String("🗺️ [MANUAL] TZ=fixed: ")
                         + String(cfg.timeZoneOffsetHrs) + ":" + String(cfg.timeZoneOffsetMin));
            rtcInitOk = rtcManager->beginManual(cfg.timeZoneOffsetHrs, /*useEUDst=*/false, cfg.timeZoneOffsetMin);
        }
    }

    if (!rtcInitOk) {
        logger->error("❌ RTC/NTP init failed!");
    }

    // --- Immediate NTP sync (AUTO) + detect real DST flip on the SYSTEM clock ---
    time_t sysBefore = time(nullptr);
    tm ltBefore; localtime_r(&sysBefore, &ltBefore);
    char zBefore[8]; strftime(zBefore, sizeof(zBefore), "%z", &ltBefore);

    uint32_t sysDelta = 0;
    if (isAuto) {
        rtcManager->syncWithNtp(cfg.resyncRtcIfDiffSeconds);
        delay(200);
    }

    time_t sysAfter = time(nullptr);
    tm ltAfter; localtime_r(&sysAfter, &ltAfter);
    char zAfter[8];  strftime(zAfter,  sizeof(zAfter),  "%z", &ltAfter);

    bool realDstFlip = (ltBefore.tm_isdst != ltAfter.tm_isdst);
    sysDelta = (uint32_t) llabs((long long)(sysAfter - sysBefore));

    // Always log TZ flags and NTP outcome
    logger->info(String("🧭 TZ before/after: ") + zBefore + " → " + zAfter +
                 String(", isdst: ") + String(ltBefore.tm_isdst) + " → " + String(ltAfter.tm_isdst));
    if (sysDelta >= (uint32_t)cfg.resyncRtcIfDiffSeconds) {
        logger->info(String("🌐 NTP: boot-time correction by ") + sysDelta + " s");
    } else {
        logger->info("🌐 NTP: no boot-time correction");
    }

    // --- Load state.txt and compare to *current local time* ---
    DateTime stateDt = stateManager->loadLastKnownClockTime();
    DateTime nowDt = isAuto ? SystemLocalNow()
                            : (rtcInitOk ? rtcManager->now() : SystemLocalNow());

    lastImpulseMinutes = minutesOfDay(stateDt);

    char sH[6], nH[6];
    snprintf(sH, sizeof(sH), "%02d:%02d", stateDt.hour(), stateDt.minute());
    snprintf(nH, sizeof(nH), "%02d:%02d", nowDt.hour(), nowDt.minute());
    int bootDiff = diffForwardMinutes(lastImpulseMinutes, minutesOfDay(nowDt));
    logger->info(String("BOOT: state.txt=") + sH + " | NOW=" + nH + " | diff=" + String(bootDiff) + " min");

    // --- DST guard: align without catch-up only on a *real* DST flip (~1h) ---
    if (realDstFlip && sysDelta >= 55*60 && sysDelta <= 65*60) {
        int nowMin = minutesOfDay(nowDt);
        lastImpulseMinutes = nowMin;
        stateManager->saveClockTime(DateTime(2000, 1, 1, nowDt.hour(), nowDt.minute(), 0));
        logger->info("⛳ Detected ~1h DST/TZ shift — aligned without catch-up.");
    }

    // --- Pulse timing & anti-duplicate gap ---
    pulseManager->setImpulseTiming(cfg.impulseDelayMs, 150);
    uint32_t minGap = (uint32_t)cfg.impulseDelayMs + 150 + 50;
    if (minGap < 600) minGap = 600;
    pulseManager->setMinGapMs(minGap);

    // --- Decide catch-up after init/DST alignment ---
    doCatchUpIfNeeded();

    // --- Periodic NTP re-sync timer (AUTO) ---
    lastNtpSyncMs = millis();

    logger->info("✅ System ready.");
}

/**
 * @brief Main loop: minute tick + non-blocking catch-up + periodic NTP (AUTO).
 */
void SystemManager::loop() {
    checkMinuteChange();
    tickCatchUp();

    const auto& cfg = configManager->getConfig();
    if (cfg.mode != "auto") return;

    const uint32_t SYNC_EVERY_MS =
        (uint32_t)cfg.ntpResyncEveryMinutes * 60UL * 1000UL;
    if (SYNC_EVERY_MS == 0) return;

    uint32_t nowMs = millis();
    if ((uint32_t)(nowMs - lastNtpSyncMs) < SYNC_EVERY_MS) return;
    lastNtpSyncMs = nowMs;

    // Snapshot SYSTEM clock BEFORE sync
    time_t sysBefore = time(nullptr);
    tm ltBefore; localtime_r(&sysBefore, &ltBefore);
    char zBefore[8]; strftime(zBefore, sizeof(zBefore), "%z", &ltBefore);

    rtcManager->syncWithNtp(cfg.resyncRtcIfDiffSeconds);
    delay(200);

    // Snapshot SYSTEM clock AFTER sync
    time_t sysAfter = time(nullptr);
    tm ltAfter; localtime_r(&sysAfter, &ltAfter);
    char zAfter[8];  strftime(zAfter,  sizeof(zAfter),  "%z", &ltAfter);

    const bool dstFlip = (ltBefore.tm_isdst != ltAfter.tm_isdst);
    const uint32_t sysDelta = (uint32_t) llabs((long long)(sysAfter - sysBefore));

    logger->info(String("🧭 TZ before/after: ") + zBefore + " → " + zAfter +
                 String(", isdst: ") + String(ltBefore.tm_isdst) + " → " + String(ltAfter.tm_isdst));

    if (sysDelta == 0) return;

    if (dstFlip && sysDelta >= 55*60 && sysDelta <= 65*60) {
        // Align state to *current system local time* without catch-up
        DateTime after = SystemLocalNow();
        int nowMin = minutesOfDay(after);
        lastImpulseMinutes = nowMin;
        stateManager->saveClockTime(DateTime(2000, 1, 1, after.hour(), after.minute(), 0));
        logger->info("🌐 NTP: detected ~1h DST/TZ shift. Aligned without catch-up.");
        return;
    }

    if (sysDelta >= (uint32_t)cfg.resyncRtcIfDiffSeconds) {
        logger->info(String("🌐 NTP: corrected by ") + sysDelta + " s — starting re-catch-up.");
        tryStartCatchUp("ntp-resync");
    }
}

/**
 * @brief Legacy helper (RTC-based). Kept for compatibility with potential external callers.
 * @return Seconds difference between RTC before and after NTP (0 if MANUAL).
 */
uint32_t SystemManager::initialNtpSyncDeltaSecIfAuto() {
    const auto& cfg = configManager->getConfig();
    if (cfg.mode != "auto") return 0;
    DateTime before = rtcManager->now();
    rtcManager->syncWithNtp(cfg.resyncRtcIfDiffSeconds);
    delay(200);
    DateTime after  = rtcManager->now();
    return secDiff(before, after);
}

/**
 * @brief Decide whether to start catch-up after boot/init.
 */
void SystemManager::doCatchUpIfNeeded() {
    tryStartCatchUp("boot");
}

/**
 * @brief Centralized gate to start a catch-up session.
 */
void SystemManager::tryStartCatchUp(const char* reason) {
    if (catchupActive) return;

    // In AUTO use system time; in MANUAL use RTC
    const bool isAuto = (configManager->getConfig().mode == "auto");
    DateTime now = isAuto ? SystemLocalNow() : rtcManager->now();
    int realMin  = minutesOfDay(now);

    int diff = diffForwardMinutes(lastImpulseMinutes, realMin);
    if (diff == 0) {
        logger->info("⏱ Clocks are up-to-date — no catch-up needed.");
        return;
    }

    int maxCatch = configManager->getConfig().maxCatchupMinutes;
    if (diff > maxCatch) {
        logger->error("❌ Catch-up exceeds limit! Difference: " + String(diff) + " minutes");
        return;
    }

    startCatchUp(diff, reason);
}

/**
 * @brief Enter catch-up mode with a fixed interval between pulses.
 */
void SystemManager::startCatchUp(int diffMinutes, const char* reason) {
    catchupRemaining   = diffMinutes;
    catchupIntervalMs  = (uint32_t)configManager->getConfig().impulseDelayMs + 150 + 50;
    catchupLastPulseMs = 0;
    catchupActive      = true;

    logger->info(String("⚙️ Catch-up start: ") + diffMinutes + " pulses (" + reason + "), interval "
                 + String(catchupIntervalMs) + " ms");
}

/**
 * @brief Non-blocking catch-up engine; emits pulses at catchupIntervalMs until done.
 */
void SystemManager::tickCatchUp() {
    if (!catchupActive) return;

    uint32_t nowMs = millis();
    if (catchupLastPulseMs == 0 || (nowMs - catchupLastPulseMs) >= catchupIntervalMs) {
        bool sent = pulseManager->triggerPulse(true); // burst=true during catch-up
        if (!sent) return;

        catchupLastPulseMs = millis();

        // advance internal clock by +1 minute
        lastImpulseMinutes = (lastImpulseMinutes + 1) % 1440;

        // persist intermediate state (2000-01-01 HH:MM:00)
        int h = lastImpulseMinutes / 60;
        int m = lastImpulseMinutes % 60;
        stateManager->saveClockTime(DateTime(2000, 1, 1, h, m, 0));

        logger->info(String("📌 Catch-up remaining: ") + String(catchupRemaining - 1));

        if (--catchupRemaining <= 0) {
            catchupActive = false;
            logger->info("✅ Catch-up finished.");

            // Align to actual *current local time*
            const bool isAuto = (configManager->getConfig().mode == "auto");
            DateTime now = isAuto ? SystemLocalNow() : rtcManager->now();
            lastImpulseMinutes = minutesOfDay(now);
            stateManager->saveClockTime(DateTime(2000, 1, 1, now.hour(), now.minute(), 0));
        }
    }
}

/**
 * @brief Regular minute tick handler (disabled during catch-up).
 */
void SystemManager::checkMinuteChange() {
    if (catchupActive) return;

    // In AUTO use system time; in MANUAL use RTC
    const bool isAuto = (configManager->getConfig().mode == "auto");
    DateTime now = isAuto ? SystemLocalNow() : rtcManager->now();
    int nowMin = minutesOfDay(now);

    if (nowMin != lastImpulseMinutes) {
        bool sent = pulseManager->triggerPulse(false); // normal mode — min-gap applies
        if (!sent) {
            logger->info("⏭️ Pulse skipped (min-gap).");
            return;
        }

        char hhmm[6];
        snprintf(hhmm, sizeof(hhmm), "%02d:%02d", now.hour(), now.minute());
        logger->info(String("🕒 Pulse for ") + hhmm);

        lastImpulseMinutes = nowMin;
        stateManager->saveClockTime(DateTime(2000, 1, 1, now.hour(), now.minute(), 0));
    }
}

/**
 * @brief Handle manual HH:MM entry (from Web UI).
 */
void SystemManager::OnManualClockSet(int clockMinutes) {
    // In AUTO use system time; in MANUAL use RTC
    const bool isAuto = (configManager->getConfig().mode == "auto");
    DateTime now = isAuto ? SystemLocalNow() : rtcManager->now();
    int nowMin = minutesOfDay(now);
    int diff = diffForwardMinutes(clockMinutes, nowMin);

    int ch = clockMinutes / 60, cm = clockMinutes % 60;
    logger->info(String("🛠️ Manual set: entered ")
                 + (ch < 10 ? "0" : "") + ch + ":" + (cm < 10 ? "0" : "") + cm
                 + " (min=" + String(clockMinutes) + "), target NOW "
                 + String(now.hour()<10?"0":"") + now.hour() + ":" + String(now.minute()<10?"0":"") + now.minute()
                 + " (min=" + String(nowMin) + "), forward diff = " + String(diff) + " min");

    int maxCatch = configManager->getConfig().maxCatchupMinutes;
    if (diff > maxCatch) {
        logger->error("❌ Manual set: difference " + String(diff) + " min exceeds limit "
                      + String(maxCatch) + ". Stop.");
        lastImpulseMinutes = clockMinutes % 1440; // still persist for consistent ticks
        stateManager->saveClockTime(DateTime(2000,1,1, ch, cm, 0));
        return;
    }

    lastImpulseMinutes = clockMinutes % 1440;
    stateManager->saveClockTime(DateTime(2000,1,1, ch, cm, 0));

    if (diff == 0) {
        logger->info("ℹ️ Manual set: already aligned (0 min difference) — no catch-up needed.");
        return;
    }

    startCatchUp(diff, "manual");
}
