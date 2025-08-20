/**
 * @file    StateManager.h
 * @brief   Minimal persistence of last known clock time (HH:MM) on SD.
 */

#pragma once

#include <Arduino.h>
#include <RTClib.h>

/**
 * @class StateManager
 * @brief Persists the last known time-of-day ("HH:MM") and restores it on boot.
 *
 * Usage:
 *   StateManager sm;
 *   sm.begin("/state.txt");
 *   DateTime t = sm.loadLastKnownClockTime();   // returns 2000-01-01 HH:MM:00
 *   sm.saveClockTime(DateTime(2000,1,1,12,34,0));
 */
class StateManager {
public:
    /// Capture the state file path (SD must already be initialized).
    bool begin(const char* filePath = "/state.txt");

    /// Load last known time-of-day; date is always 2000-01-01.
    DateTime loadLastKnownClockTime();         // returns 2000-01-01 HH:MM:00

    /// Save only "HH:MM" (overwrites the file).
    bool saveClockTime(const DateTime& dt);    // persists just "HH:MM"

    /// True if begin() was called and the path recorded.
    bool isValid() const;

private:
    String statePath;
    bool   valid = false;

    /// Parse "HH:MM" or "YYYY-MM-DD HH:MM"; returns 2000-01-01 HH:MM:00.
    DateTime parseLine(const String& line);

    /// Format "HH:MM" for persistence.
    String  formatDateTime(const DateTime& dt);
};
