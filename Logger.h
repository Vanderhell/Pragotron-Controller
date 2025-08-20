/**
 * @file    Logger.h
 * @brief   Lightweight logging API (Serial + SD) with optional daily rotation.
 *
 * Usage
 * -----
 *   Logger logger;
 *   logger.begin("/logs", true); // enableSerial
 *   logger.info("System started.");
 *   logger.warn("Low voltage.");
 *   logger.error("Write failed.");
 *
 * Modes
 * -----
 * - Daily rotation mode: basePath is a directory (e.g., "/logs")
 *   -> writes to "/logs/YYYY-MM-DD.txt"
 * - Single-file mode: basePath ends with ".txt" (e.g., "/log.txt")
 *   -> appends to that single file
 *
 * Requirements
 * ------------
 * - SD must be initialized before calling Logger::begin().
 * - A global RTCManager must be available to provide timestamps.
 */

#pragma once

#include <Arduino.h>
#include <SD.h>

/**
 * @enum LogLevel
 * @brief Log severity levels used by Logger.
 */
enum class LogLevel {
    INFO,
    WARNING,
    ERROR
};

/**
 * @class Logger
 * @brief File/serial logger with timestamped entries and optional daily rotation.
 */
class Logger {
public:
    /**
     * @brief Initialize the logger.
     * @param logPath      Directory for daily logs (e.g., "/logs") or a single file path ending with ".txt".
     * @param enableSerial If true, mirror logs to Serial.
     * @return true (initialization is non-failing).
     */
    bool begin(const char* logPath = "/logs", bool enableSerial = true);

    /**
     * @brief Generic logging entry point.
     * @param level   Log severity.
     * @param message One-line message to log.
     */
    void log(LogLevel level, const String& message);

    /// @brief Shorthand for INFO level.
    void info(const String& message);
    /// @brief Shorthand for WARNING level.
    void warn(const String& message);
    /// @brief Shorthand for ERROR level.
    void error(const String& message);

private:
    // Mode & state
    bool   serialEnabled = true;   ///< If true, also prints each entry to Serial.
    bool   dailyMode     = true;   ///< true = daily rotation; false = single-file mode.
    String basePath;               ///< "/logs" (dir) or "/log.txt" (single-file).
    String currentDate;            ///< "YYYY-MM-DD" tracked for daily rotation.

    // Helpers
    void   ensureDirIfDaily();     ///< Create base directory if dailyMode is enabled.
    void   rotateIfNeeded();       ///< Update currentDate when the day changes.
    String todayDateString();      ///< From RTCManager (local date), or "0000-00-00".
    String activeLogPath();        ///< Resolve target file path for the write.
    String getTimestamp();         ///< "YYYY-MM-DD HH:MM:SS", or placeholder if RTC unknown.
    String levelToString(LogLevel level);
};
