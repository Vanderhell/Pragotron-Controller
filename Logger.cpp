/**
 * @file    Logger.cpp
 * @brief   Lightweight file/serial logger for ESP32 with optional daily rotation.
 *
 * Overview
 * --------
 * - Two modes:
 *   1) Daily mode:   <basePath>/YYYY-MM-DD.txt (when basePath does NOT end with ".txt")
 *   2) Single file:  <basePath>                (when basePath ends with ".txt")
 * - Timestamps are taken from the global RTCManager instance (local time).
 * - Serial output can be enabled/disabled independently of file logging.
 *
 * Notes
 * -----
 * - SD must already be initialized elsewhere (e.g., in setup()).
 * - This module does not change any system state besides writing log files.
 * - If RTC is not yet available, placeholder dates/timestamps are used.
 */

#include "Logger.h"
#include "RTCManager.h"

// Uses the global rtcManager provided elsewhere in the project.
extern RTCManager rtcManager;

/**
 * @brief Helper: returns true if the string ends with ".txt" (case-sensitive).
 */
static bool endsWithTxt(const String& s) {
    int n = s.length();
    return (n >= 4) && s.substring(n - 4) == ".txt";
}

/**
 * @brief Initialize the logger.
 * @param logPath      If ends with ".txt", single-file mode. Otherwise treated as a folder for daily logs.
 * @param enableSerial If true, mirror all log lines to Serial.
 * @return Always true (initialization is non-failing by design).
 *
 * Behavior:
 * - In daily mode, ensures that the base directory exists.
 * - Defers the first actual date detection until the first log write.
 */
bool Logger::begin(const char* logPath, bool enableSerial) {
    serialEnabled = enableSerial;

    // SD should already be initialized in setup(); Logger does not (re)initialize SD.
    basePath = String(logPath);
    dailyMode = !endsWithTxt(basePath); // ".txt" => single-file; otherwise daily logs.

    if (dailyMode) {
        ensureDirIfDaily();
    }

    // Defer date determination until the first log call.
    currentDate = "";

    if (serialEnabled) {
        Serial.printf("[Logger] init (%s), path=%s\n",
                      dailyMode ? "daily" : "single", basePath.c_str());
    }
    return true;
}

/**
 * @brief Create the base directory if in daily-rotation mode.
 */
void Logger::ensureDirIfDaily() {
    if (!dailyMode) return;
    if (!SD.exists(basePath)) {
        SD.mkdir(basePath);
    }
}

/**
 * @brief Update the active date for daily rotation if the date has changed.
 *
 * - If this is the first use, capture today's date (or placeholder).
 * - If RTC returns a valid date ("!= 0000-00-00") and it differs from the current one, rotate.
 */
void Logger::rotateIfNeeded() {
    if (!dailyMode) return;

    String d = todayDateString();
    if (currentDate.isEmpty()) {
        // First use — set even if it's a placeholder.
        currentDate = d;
        return;
    }
    if (d != "0000-00-00" && d != currentDate) {
        currentDate = d;
    }
}

/**
 * @brief Get today's date as "YYYY-MM-DD".
 * @return "0000-00-00" if RTC is not yet available.
 */
String Logger::todayDateString() {
    // If RTC is not ready yet, return a placeholder date.
    if (!rtcManager.isRtcAvailable()) {
        return String("0000-00-00");
    }
    auto now = rtcManager.now(); // Local time (RTC synced from NTP)
    char buf[11];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", now.year(), now.month(), now.day());
    return String(buf);
}

/**
 * @brief Compute the active log file path based on the selected mode.
 * @return Full path to the target log file.
 */
String Logger::activeLogPath() {
    if (dailyMode) {
        String d = currentDate.isEmpty() ? todayDateString() : currentDate;
        return basePath + "/" + d + ".txt";
    }
    // Single-file mode
    return basePath;
}

/**
 * @brief Build a timestamp string "YYYY-MM-DD HH:MM:SS".
 * @return Placeholder "0000-00-00 00:00:00" if RTC is not available.
 */
String Logger::getTimestamp() {
    if (!rtcManager.isRtcAvailable()) {
        return String("0000-00-00 00:00:00");
    }
    auto now = rtcManager.now(); // Local time
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    return String(buffer);
}

/**
 * @brief Convert log level enum to a short string token.
 */
String Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARN";
        case LogLevel::ERROR:   return "ERROR";
        default:                return "LOG";
    }
}

/**
 * @brief Write a log entry with timestamp and level to Serial and/or SD file.
 * @param level   Log severity.
 * @param message Log message (one line).
 *
 * I/O:
 * - Serial: Printed if serialEnabled == true.
 * - SD: Appends to active log file. If open fails and serial is enabled, prints a warning to Serial.
 */
void Logger::log(LogLevel level, const String& message) {
    rotateIfNeeded();

    String entry = "[" + getTimestamp() + "] [" + levelToString(level) + "] " + message;

    if (serialEnabled) {
        Serial.println(entry);
    }

    String path = activeLogPath();
    File f = SD.open(path, FILE_APPEND);
    if (f) {
        f.println(entry);
        f.close();
    } else if (serialEnabled) {
        Serial.println("⚠️ Logger: cannot open " + path);
    }
}

/**
 * @brief Convenience wrappers for common log levels.
 */
void Logger::info(const String& message)  { log(LogLevel::INFO,    message); }
void Logger::warn(const String& message)  { log(LogLevel::WARNING, message); }
void Logger::error(const String& message) { log(LogLevel::ERROR,   message); }
