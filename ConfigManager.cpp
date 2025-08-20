/**
 * @file    ConfigManager.cpp
 * @brief   Loads and exposes runtime configuration from /config.json on SD.
 *
 * Responsibilities:
 *  - Mount SD (fallback to defaults if SD/JSON is unavailable or invalid)
 *  - Parse JSON via ArduinoJson
 *  - Provide a strongly-typed Config instance to the rest of the app
 *
 * Notes:
 *  - This module prints short Slovak status lines to Serial (kept as-is).
 *  - `begin()` calls `SD.begin()` with default CS; your main() already does
 *    `SD.begin(SD_CS)`. This duplication is harmless; left unchanged by request.
 */

#include "ConfigManager.h"

/**
 * @brief Initialize configuration by attempting to mount SD and read JSON.
 * @param path Path to the JSON config file (default: "/config.json").
 * @return true if configuration was loaded successfully; false if defaults used.
 *
 * Behavior:
 *  - If SD mount fails → apply defaults and return false.
 *  - If JSON load/parse fails → apply defaults and return false.
 *  - Otherwise → store parsed values and return true.
 */
bool ConfigManager::begin(const char* path) {
    if (!SD.begin()) {
        Serial.println("❌ SD initialization failed.");
        applyDefaults();
        return false;
    }

    if (!loadFromFile(path)) {
        Serial.println("⚠️ Loading config failed, using defaults.");
        applyDefaults();
        return false;
    }

    Serial.println("✅ Config loaded successfully.");
    return true;
}

/**
 * @brief Load and parse configuration from the given JSON file.
 * @param path Absolute path on SD (e.g., "/config.json").
 * @return true on success, false if file missing or JSON invalid.
 *
 * JSON schema (selected keys):
 *  - wifi_ssid, wifi_password, ntp_server
 *  - tz_mode: "posix" | "eu" | "fixed"
 *  - posix_tz: e.g., "CET-1CEST,M3.5.0/2,M10.5.0/3"
 *  - time_zone_offset_hrs, time_zone_offset_min, use_eu_dst
 *  - mode: "auto"...
 *  - impulse_interval_sec, impulse_delay_ms
 *  - resync_rtc_if_diff_seconds, max_catchup_minutes
 *  - web_edit_enabled, debug_serial
 *  - ntp_resync_every_minutes
 */
bool ConfigManager::loadFromFile(const char* path) {
    File file = SD.open(path, FILE_READ);
    if (!file) {
        Serial.printf("⚠️ Config file %s not found.\n", path);
        return false;
    }

    StaticJsonDocument<2048> doc; // Adjust capacity if config grows.
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.print("❌ Failed to parse config.json: ");
        Serial.println(error.c_str());
        return false;
    }

    // ---- WiFi & NTP ----
    {
        const char* s = doc["wifi_ssid"]    | "";
        config.wifiSsid = String(s);
    }
    {
        const char* s = doc["wifi_password"]| "";
        config.wifiPassword = String(s);
    }
    {
        const char* s = doc["ntp_server"]   | "pool.ntp.org";
        config.ntpServer = String(s);
    }

    // ---- Timezone mode selection ----
    {
        const char* s = doc["tz_mode"] | "eu";             // "posix"|"eu"|"fixed"
        config.tz_mode = toLowerTrim(String(s));
    }
    {
        const char* s = doc["posix_tz"] | "";
        config.posix_tz = String(s);
    }
    config.timeZoneOffsetHrs = doc["time_zone_offset_hrs"] | 0;
    config.timeZoneOffsetMin = doc["time_zone_offset_min"] | 0;
    config.useEuDst          = doc["use_eu_dst"]           | true;

    // ---- App behavior ----
    {
        const char* s = doc["mode"] | "auto";
        config.mode = String(s);
    }
    config.impulseIntervalSec     = doc["impulse_interval_sec"]       | 60;
    config.impulseDelayMs         = doc["impulse_delay_ms"]           | 500;
    config.resyncRtcIfDiffSeconds = doc["resync_rtc_if_diff_seconds"] | 60;
    config.maxCatchupMinutes      = doc["max_catchup_minutes"]        | 180;
    config.webEditEnabled         = doc["web_edit_enabled"]           | false;
    config.debugSerial            = doc["debug_serial"]               | false;

    // Auto NTP re-sync cadence (minutes)
    config.ntpResyncEveryMinutes = doc["ntp_resync_every_minutes"] | 15;
    // allow 0 = disabled, clamp negatives to 0
    if (config.ntpResyncEveryMinutes < 0) config.ntpResyncEveryMinutes = 0;

    // Sanity clamps
    if (config.timeZoneOffsetMin < 0)  config.timeZoneOffsetMin = 0;
    if (config.timeZoneOffsetMin > 59) config.timeZoneOffsetMin = 59;

    // Short summary to Serial (useful for field debugging)
    Serial.println(F("---- Loaded Config ----"));
    Serial.printf("WiFi SSID: %s\n", config.wifiSsid.c_str());
    Serial.printf("NTP Server: %s\n", config.ntpServer.c_str());
    Serial.printf("TZ Mode: %s\n",   config.tz_mode.c_str());
    if (config.tz_mode == "posix") {
        Serial.printf("POSIX TZ: %s\n", config.posix_tz.c_str());
    } else if (config.tz_mode == "fixed") {
        Serial.printf("Fixed Offset: %d:%02d (useEuDst=%s)\n",
                      config.timeZoneOffsetHrs, config.timeZoneOffsetMin, config.useEuDst ? "true" : "false");
    } else {
        Serial.printf("EU DST: %s (CET/CEST)\n", config.useEuDst ? "true" : "false");
    }
    Serial.printf("Resync every : %d min\n", config.ntpResyncEveryMinutes);
    Serial.printf("Impulse: interval=%ds, delay=%dms\n", config.impulseIntervalSec, config.impulseDelayMs);
    Serial.printf("Resync RTC if diff: %ds\n", config.resyncRtcIfDiffSeconds);
    Serial.printf("Max catch-up: %d min\n", config.maxCatchupMinutes);
    Serial.printf("WebEdit: %s, DebugSerial: %s\n",
                  config.webEditEnabled ? "true" : "false",
                  config.debugSerial ? "true" : "false");
    Serial.println(F("-----------------------"));

    return true;
}

/**
 * @brief Apply safe default values when SD/JSON is unavailable.
 *
 * Defaults:
 *  - NTP: pool.ntp.org
 *  - Timezone mode: EU (CET/CEST with useEuDst=true)
 *  - Impulse timing: 60s interval, 500ms delay
 *  - Catch-up cap: 180 minutes
 *  - Periodic NTP re-sync: 15 minutes
 */
void ConfigManager::applyDefaults() {
    // WiFi & NTP
    config.wifiSsid   = "";
    config.wifiPassword = "";
    config.ntpServer  = "pool.ntp.org";

    // TZ default: EU mode (CET/CEST)
    config.tz_mode           = "eu";
    config.posix_tz          = "";
    config.timeZoneOffsetHrs = 0;
    config.timeZoneOffsetMin = 0;
    config.useEuDst          = true;

    // App
    config.mode                   = "auto";
    config.impulseIntervalSec     = 60;
    config.impulseDelayMs         = 500;
    config.resyncRtcIfDiffSeconds = 60;
    config.maxCatchupMinutes      = 180;
    config.webEditEnabled         = false;
    config.debugSerial            = false;
    config.ntpResyncEveryMinutes  = 15;
}

/**
 * @brief Accessor for the immutable configuration snapshot.
 * @return const reference to the current Config.
 */
const Config& ConfigManager::getConfig() const {
    return config;
}
