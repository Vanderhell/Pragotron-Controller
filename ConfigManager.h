/**
 * @file    ConfigManager.h
 * @brief   Configuration model and loader for the Pragotron controller.
 *
 * Usage:
 *   ConfigManager cfg;
 *   if (!cfg.begin("/config.json")) {
 *       // Defaults applied; proceed in degraded mode if necessary.
 *   }
 *   const Config& c = cfg.getConfig();
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>

/**
 * @struct Config
 * @brief  In-memory configuration snapshot parsed from JSON (or defaults).
 */
struct Config {
    // ── WiFi & NTP ────────────────────────────────────────────────────────────
    String wifiSsid;        ///< Wi-Fi SSID (may be empty when offline usage).
    String wifiPassword;    ///< Wi-Fi password (not printed to logs).
    String ntpServer;       ///< NTP server hostname (e.g., "pool.ntp.org").

    // ── Timezone configuration ────────────────────────────────────────────────
    String tz_mode;         ///< "posix" | "eu" | "fixed"
    String posix_tz;        ///< POSIX TZ string if tz_mode=="posix".
    int    timeZoneOffsetHrs; ///< Hours offset for tz_mode=="fixed".
    int    timeZoneOffsetMin; ///< Minutes (0..59) for tz_mode=="fixed".
    bool   useEuDst;        ///< If true and tz_mode=="eu", use CET/CEST rules.

    // ── Application behavior ─────────────────────────────────────────────────
    String mode;                 ///< Operating mode, e.g., "auto".
    int    impulseIntervalSec;   ///< Impulse interval (seconds) for minute ticks.
    int    impulseDelayMs;       ///< Inter-pulse delay (milliseconds).
    int    resyncRtcIfDiffSeconds; ///< NTP resync if RTC differs by ≥ this (s).
    int    maxCatchupMinutes;    ///< Maximum allowed catch-up minutes on boot.
    bool   webEditEnabled;       ///< Enable web editing UI for config/state.
    bool   debugSerial;          ///< Verbose serial logging toggle.

    // ── Periodic NTP re-sync ─────────────────────────────────────────────────
    int    ntpResyncEveryMinutes; ///< Auto NTP sync cadence (min), min 1.
};

/**
 * @class ConfigManager
 * @brief Loads configuration from SD JSON and exposes it to the system.
 *
 * Contract:
 *  - `begin()` must be called before `getConfig()`.
 *  - On any load failure, safe defaults are applied and `begin()` returns false.
 */
class ConfigManager {
public:
    /**
     * @brief Initialize the configuration system and load JSON from SD.
     * @param path Path to the JSON file (default: "/config.json").
     * @return true on successful load; false if defaults were applied.
     */
    bool begin(const char* path = "/config.json");

    /**
     * @brief Get the current configuration snapshot.
     * @return const reference valid for the lifetime of ConfigManager.
     */
    const Config& getConfig() const;

private:
    Config config;

    /// @brief Open, parse, and map JSON fields into @ref config.
    bool loadFromFile(const char* path);

    /// @brief Fill @ref config with safe defaults.
    void applyDefaults();

    /**
     * @brief Helper: trim and lowercase a String (used for tz_mode).
     * @param s Input string (copied).
     * @return Normalized string (trimmed, to lower case).
     */
    static String toLowerTrim(const String& s) {
        String t = s;
        t.trim();
        t.toLowerCase();
        return t;
    }
};
