/**
 * @file    WebServerManager.h
 * @brief   Small HTTP server wrapper for status, control, and static file serving.
 */

#pragma once

#include <WebServer.h>
#include <Arduino.h>
#include <RTClib.h>
#include "StateManager.h"
#include "ConfigManager.h"
#include "RTCManager.h"

/**
 * @class WebServerManager
 * @brief Exposes REST-style endpoints and serves a static UI from SD.
 *
 * Usage:
 *   WebServerManager web(&state, &config, &rtc);
 *   web.begin(80);
 *   ...
 *   web.handleClient();  // in loop()
 */
class WebServerManager {
public:
    WebServerManager(StateManager* state, ConfigManager* config, RTCManager* rtc);

    /// Start the server and register routes (port parameter kept for API symmetry).
    void begin(uint16_t port = 80);

    /// Process client requests; call frequently from the main loop.
    void handleClient();

    // SystemManager registers a handler that will be called after manual time set.
    using ClockSetHandler = void (*)(int newClockMinutes);
    void setOnClockSet(ClockSetHandler handler) { onClockSet = handler; }

private:
    WebServer     server;
    StateManager* stateManager;
    ConfigManager* configManager;
    RTCManager*   rtcManager;

    ClockSetHandler onClockSet = nullptr; // callback invoked after /api/set-state

    // Route handlers
    void handleRoot();
    void handleFileRequest();
    void handleApiStatus();
    void handleApiSetState();
    void handleApiLog();
    void handleNotFound();
    void handleApiLogsList();          // GET /api/logs
    void handleApiLogsFile();          // GET /api/logfile?file=YYYY-MM-DD.txt

    // Utilities
    static String hhmmFromDateTime(const DateTime& dt) {
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:%02d", dt.hour(), dt.minute());
        return String(buf);
    }
    static String contentTypeFor(const String& path) {
        if (path.endsWith(".html")) return "text/html";
        if (path.endsWith(".css"))  return "text/css";
        if (path.endsWith(".js"))   return "application/javascript";
        if (path.endsWith(".json")) return "application/json";
        if (path.endsWith(".txt"))  return "text/plain";
        if (path.endsWith(".ico"))  return "image/x-icon";
        if (path.endsWith(".png"))  return "image/png";
        if (path.endsWith(".svg"))  return "image/svg+xml";
        return "text/plain";
    }
};
