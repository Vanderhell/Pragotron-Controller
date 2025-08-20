/**
 * @file    StateManager.cpp
 * @brief   Persist and restore the last known clock time (HH:MM) on SD.
 *
 * Overview
 * --------
 * - Stores only the minute time-of-day as text ("HH:MM") in /state.txt.
 * - On first run (file missing), creates the file with the current system time
 *   (derived from RTC→system clock) and returns that value.
 * - Supports reading legacy formats "YYYY-MM-DD HH:MM" and "HH:MM"; the date
 *   part is ignored and only HH:MM is used.
 *
 * Requirements
 * ------------
 * - SD must already be initialized elsewhere (e.g., during application setup).
 * - The system timezone should already be configured so that `localtime()` is correct.
 */

#include "StateManager.h"
#include <SD.h>

/**
 * @brief Initialize state manager with a target file path.
 * @param filePath Path to the state file (default: "/state.txt").
 * @return true if the manager is ready to operate (SD must be mounted beforehand).
 *
 * Note: This does not call SD.begin(); it assumes the filesystem is ready.
 */
bool StateManager::begin(const char* filePath) {
    statePath = String(filePath);
    // SD.begin() is called elsewhere (in the app's setup). We only capture the path.
    valid = true;
    return true;
}

/**
 * @brief Load the last known clock time from SD.
 * @return DateTime with sentinel date (2000-01-01) and restored HH:MM.
 *
 * Behavior:
 * - If the state file does not exist, it is created with the current local system time
 *   and that time is returned.
 * - If the file exists, it is parsed (supports "YYYY-MM-DD HH:MM" or "HH:MM").
 * - Invalid content resets the file to "00:00" and returns 00:00.
 */
DateTime StateManager::loadLastKnownClockTime() {
    if (!SD.exists(statePath)) {
        // File missing: create it with current local system time (derived from RTC if set).
        time_t tnow = time(nullptr);
        struct tm *tmnow = localtime(&tnow);
        int h = tmnow ? tmnow->tm_hour : 0;
        int m = tmnow ? tmnow->tm_min  : 0;

        File nf = SD.open(statePath, FILE_WRITE);
        if (nf) { 
            char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
            nf.println(buf);
            nf.close();
        }
        Serial.printf("⚠️ %s not found. Creating with %02d:%02d.\n", statePath.c_str(), h, m);
        return DateTime(2000, 1, 1, h, m, 0);
    }

    File f = SD.open(statePath, FILE_READ);
    if (!f) {
        Serial.println("❌ Failed to open state.txt (read). Creating with 00:00.");
        File nf = SD.open(statePath, FILE_WRITE);
        if (nf) { nf.println("00:00"); nf.close(); }
        return DateTime(2000, 1, 1, 0, 0, 0);
    }

    String line = f.readStringUntil('\n');
    f.close();

    return parseLine(line);
}

/**
 * @brief Save only "HH:MM" to the state file (overwrites the file).
 * @param dt Local DateTime; only hour and minute are persisted.
 * @return true on success, false if the file could not be opened.
 */
bool StateManager::saveClockTime(const DateTime& dt) {
    File f = SD.open(statePath, FILE_WRITE); // create/overwrite — single-line state file
    if (!f) {
        Serial.println("❌ Failed to write state.txt");
        return false;
    }

    String content = formatDateTime(dt); // "HH:MM"
    f.println(content);
    f.close();

    return true;
}

/**
 * @brief Parse a state line supporting both "YYYY-MM-DD HH:MM" and "HH:MM".
 * @param line Raw line from the state file.
 * @return DateTime with sentinel date 2000-01-01 and extracted HH:MM,
 *         or 00:00 if parsing fails (and file is reset to "00:00").
 */
DateTime StateManager::parseLine(const String& line) {
    int y=2000, M=1, d=1, h=-1, m=-1;

    // Try "YYYY-MM-DD HH:MM" first.
    if (sscanf(line.c_str(), "%d-%d-%d %d:%d", &y, &M, &d, &h, &m) == 5) {
        if (h >= 0 && h < 24 && m >= 0 && m < 60)
            return DateTime(2000, 1, 1, h, m, 0); // date is ignored downstream
    }

    // Fallback to "HH:MM".
    if (sscanf(line.c_str(), "%d:%d", &h, &m) == 2) {
        if (h >= 0 && h < 24 && m >= 0 && m < 60)
            return DateTime(2000, 1, 1, h, m, 0);
    }

    Serial.println("❌ Invalid format in state.txt, resetting to 00:00");
    // Fix the file content to "00:00".
    File nf = SD.open(statePath, FILE_WRITE);
    if (nf) { nf.println("00:00"); nf.close(); }
    return DateTime(2000, 1, 1, 0, 0, 0);
}

/**
 * @brief Format a DateTime to "HH:MM".
 */
String StateManager::formatDateTime(const DateTime& dt) {
    char buffer[6];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", dt.hour(), dt.minute());
    return String(buffer);
}

/**
 * @brief True if the manager was successfully initialized with a path.
 */
bool StateManager::isValid() const {
    return valid;
}
