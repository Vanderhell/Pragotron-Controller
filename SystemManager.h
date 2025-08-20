/**
 * @file    SystemManager.h
 * @brief   High-level coordinator: RTC/TZ init, minute ticks, catch-up, and NTP re-sync.
 */

#pragma once

#include <Arduino.h>
#include <RTClib.h>
#include "ConfigManager.h"
#include "Logger.h"
#include "RTCManager.h"
#include "PulseManager.h"
#include "StateManager.h"

/**
 * @class SystemManager
 * @brief Drives the Pragotron minute-clock lifecycle.
 *
 * Flow:
 *  - Init RTC/TZ (AUTO→with NTP, MANUAL→no NTP) per ConfigManager.
 *  - Read persisted HH:MM (StateManager), compare to RTC, and catch up if needed.
 *  - On every loop: emit minute pulses or progress catch-up (non-blocking).
 *  - AUTO: periodically re-sync with NTP; handle DST/TZ jumps and drift.
 */
class SystemManager {
public:
    SystemManager(
        ConfigManager* config,
        Logger* logger,
        RTCManager* rtc,
        PulseManager* pulse,
        StateManager* state
    );

    /// Perform initialization as described in the class brief.
    void begin();

    /// Main loop: minute tick, catch-up progression, periodic NTP (AUTO).
    void loop();

    /// Callback from Web UI: user set visible clock to HH:MM (minutes since midnight).
    void OnManualClockSet(int clockMinutes);

private:
    ConfigManager* configManager;
    Logger*        logger;
    RTCManager*    rtcManager;
    PulseManager*  pulseManager;
    StateManager*  stateManager;

    // We keep time as minutes of day (0..1439) for the physical clock position.
    int       lastImpulseMinutes = -1;

    // NTP re-sync timer (AUTO mode)
    uint32_t  lastNtpSyncMs = 0;

    // Catch-up state
    bool      catchupActive      = false;
    int       catchupRemaining   = 0;
    uint32_t  catchupLastPulseMs = 0;
    uint32_t  catchupIntervalMs  = 0;

    // --- Core logic ---
    void doCatchUpIfNeeded();           ///< Decide on catch-up at boot/init (after initial NTP).
    void checkMinuteChange();           ///< Regular minute tick (disabled during catch-up).
    void startCatchUp(int diffMinutes, const char* reason);
    void tickCatchUp();                 ///< Non-blocking catch-up engine.

    // --- NTP/catch-up integration helpers ---
    uint32_t initialNtpSyncDeltaSecIfAuto(); ///< Return seconds changed by initial NTP (0 if MANUAL).
    void     tryStartCatchUp(const char* reason); ///< Central gate to (re)start catch-up.

    // --- Utilities ---
    static int minutesOfDay(const DateTime& dt) {
        return dt.hour() * 60 + dt.minute();
    }
    /// Forward difference in minutes within [0, 1440).
    static int diffForwardMinutes(int fromMinutes, int toMinutes) {
        int diff = ((toMinutes - fromMinutes) % 1440 + 1440) % 1440;
        return diff;
    }
    /// Absolute seconds difference between two DateTimes.
    static inline uint32_t secDiff(const DateTime& a, const DateTime& b) {
        int32_t d = (int32_t)b.unixtime() - (int32_t)a.unixtime();
        return (d < 0) ? (uint32_t)(-d) : (uint32_t)d;
    }
};
