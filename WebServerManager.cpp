/**
 * @file    WebServerManager.cpp
 * @brief   Lightweight HTTP server exposing status and control endpoints,
 *          and serving static assets from SD (/index.html and others).
 *
 * Endpoints
 * ---------
 *   GET  /                 → serves /index.html from SD
 *   GET  /<asset>          → serves files from SD with basic content-type mapping
 *   GET  /api/status       → JSON with device/Wi-Fi/mode and HH:MM times (RTC & clock state)
 *   POST /api/set-state    → { "clock_time": "HH:MM" } → updates state.txt and triggers callback
 *   GET  /api/log          → streams today's log or newest log from /logs
 *   GET  /api/logs         → JSON array of available log files in /logs
 *   GET  /api/logfile?file=YYYY-MM-DD.txt → streams a specific log (name sanitized)
 *
 * Notes
 * -----
 * - SD must be initialized by the application before begin().
 * - Content-Type is inferred by file extension via contentTypeFor().
 * - /api/set-state requires config.webEditEnabled = true.
 * - Time strings use local time (CET/CEST or configured TZ).
 */

#include "WebServerManager.h"
#include <WiFi.h>
#include <FS.h>
#include <SD.h>
#include <ArduinoJson.h>

WebServerManager::WebServerManager(StateManager* state, ConfigManager* config, RTCManager* rtc)
    : server(80), stateManager(state), configManager(config), rtcManager(rtc) {}

/**
 * @brief Start the HTTP server and register all routes.
 * @param port Unused (fixed at 80 by constructor); kept for signature compatibility.
 */
void WebServerManager::begin(uint16_t /*port*/) {
    server.on("/", HTTP_GET, std::bind(&WebServerManager::handleRoot, this));
    server.onNotFound(std::bind(&WebServerManager::handleFileRequest, this));

    server.on("/api/status",    HTTP_GET,  std::bind(&WebServerManager::handleApiStatus, this));
    server.on("/api/set-state", HTTP_POST, std::bind(&WebServerManager::handleApiSetState, this));
    server.on("/api/log",       HTTP_GET,  std::bind(&WebServerManager::handleApiLog, this));
    // New:
    server.on("/api/logs",      HTTP_GET,  std::bind(&WebServerManager::handleApiLogsList, this));
    server.on("/api/logfile",   HTTP_GET,  std::bind(&WebServerManager::handleApiLogsFile, this));

    server.begin();
    Serial.println("🌐 Web server started.");
}

/**
 * @brief Pump HTTP requests; call from main loop.
 */
void WebServerManager::handleClient() {
    server.handleClient();
}

/**
 * @brief Serve /index.html from SD.
 */
void WebServerManager::handleRoot() {
    File file = SD.open("/index.html");
    if (!file) {
        server.send(500, "text/plain", "index.html not found");
        return;
    }
    server.streamFile(file, "text/html");
    file.close();
}

/**
 * @brief Serve static files from SD, or 404 if not present.
 */
void WebServerManager::handleFileRequest() {
    String path = server.uri();
    if (path == "/") path = "/index.html";

    File file = SD.open(path);
    if (!file) {
        handleNotFound();
        return;
    }

    String contentType = contentTypeFor(path);
    server.streamFile(file, contentType);
    file.close();
}

/**
 * @brief Return a compact JSON status payload: Wi-Fi, mode, and HH:MM times.
 *
 * Response example:
 * {
 *   "device_ip": "192.168.1.23",
 *   "wifi_ssid": "MyAP",
 *   "mode": "auto",
 *   "web_edit": false,
 *   "rtc_time": "14:03",
 *   "clock_time": "14:02"
 * }
 */
void WebServerManager::handleApiStatus() {
    StaticJsonDocument<512> doc;

    // RTC time from RTCManager
    DateTime now = rtcManager ? rtcManager->now() : DateTime(2000,1,1,0,0,0);

    // Clock time from state.txt (HH:MM)
    DateTime clockDt = stateManager->loadLastKnownClockTime();

    doc["device_ip"]  = WiFi.localIP().toString();
    doc["wifi_ssid"]  = WiFi.SSID();
    doc["mode"]       = configManager->getConfig().mode;
    doc["web_edit"]   = configManager->getConfig().webEditEnabled;

    // Send only HH:MM
    doc["rtc_time"]   = hhmmFromDateTime(now);
    doc["clock_time"] = hhmmFromDateTime(clockDt);

    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

/**
 * @brief Accept manual HH:MM input to update clock state and notify the system.
 *
 * Security:
 * - Allowed only when `webEditEnabled == true`.
 * - Expects a JSON body: { "clock_time": "HH:MM" }.
 * - Sanitizes and validates input range (00:00..23:59).
 * - Writes to /state.txt and triggers onClockSet(minutes) if registered.
 */
void WebServerManager::handleApiSetState() {
    if (!configManager->getConfig().webEditEnabled) {
        server.send(403, "text/plain", "Not allowed");
        return;
    }

    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "No data");
        return;
    }

    StaticJsonDocument<128> doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
        server.send(400, "text/plain", "Invalid JSON");
        return;
    }

    const char* hhmmC = doc["clock_time"];
    if (!hhmmC) {
        server.send(400, "text/plain", "Missing 'clock_time'");
        return;
    }

    String hhmm = String(hhmmC);
    hhmm.trim();

    int h=-1, m=-1;
    if (sscanf(hhmm.c_str(), "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) {
        server.send(400, "text/plain", "Bad time format. Use HH:MM");
        return;
    }

    int minutes = h * 60 + m;

    // ✅ Persist to state
    // For a minute-based API you could add: stateManager->saveClockMinutes(minutes);
    // Backwards-compatible with DateTime (fake date):
    DateTime dt(2000, 1, 1, h, m, 0);
    stateManager->saveClockTime(dt);

    // Optionally rewrite /state.txt explicitly (compat with other writers)
    File f = SD.open("/state.txt", FILE_WRITE);
    if (f) {
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
        f.seek(0);
        f.print(buf);
        f.flush();
        f.close();
    } else {
        Serial.println("⚠️ Could not open /state.txt for write.");
    }

    // 🟢 Notify the system logic so it can reconcile and adjust the clock
    if (onClockSet) {
        onClockSet(minutes);
    }

    server.send(200, "text/plain", "State updated and rescheduled");
}

/**
 * @brief Stream today's log based on RTC; if not present, stream the newest log.
 */
void WebServerManager::handleApiLog() {
    // 1) Try today's log per RTC
    String todayName;
    if (rtcManager && rtcManager->isRtcAvailable()) {
        DateTime now = rtcManager->now();
        char buf[16]; // "YYYY-MM-DD.txt"
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d.txt", now.year(), now.month(), now.day());
        todayName = String(buf);
        String path = "/logs/" + todayName;
        File f = SD.open(path);
        if (f) { server.streamFile(f, "text/plain"); f.close(); return; }
    }

    // 2) Fallback: find the newest log in /logs
    File dir = SD.open("/logs");
    if (!dir || !dir.isDirectory()) {
        server.send(404, "text/plain", "No logs directory");
        return;
    }

    String newestBase; // "YYYY-MM-DD.txt"
    for (;;) {
        File f = dir.openNextFile();
        if (!f) break;
        if (!f.isDirectory()) {
            String base = String(f.name());
            // basename
            if (base.startsWith("/")) {
                int s = base.lastIndexOf('/');
                if (s >= 0) base = base.substring(s + 1);
            }
            String lower = base; lower.toLowerCase();

            // filter "YYYY-MM-DD.txt" (case-insensitive on extension)
            bool ok = lower.endsWith(".txt") && base.length() >= 12;
            if (ok) {
                for (int i = 0; i < 10; ++i) {
                    if (i == 4 || i == 7) ok &= (base[i] == '-');
                    else ok &= isDigit(base[i]);
                }
            }
            if (ok && (newestBase.isEmpty() || base > newestBase)) {
                newestBase = base; // lexicographically latest date
            }
        }
        f.close();
    }
    dir.close();

    if (!newestBase.isEmpty()) {
        String path = "/logs/" + newestBase;
        File f = SD.open(path);
        if (f) { server.streamFile(f, "text/plain"); f.close(); return; }
    }

    // 3) Nothing found
    server.send(404, "text/plain", "No logs available");
}

/**
 * @brief 404 handler for unknown routes and missing files.
 */
void WebServerManager::handleNotFound() {
    server.send(404, "text/plain", "Not found");
}

/**
 * @brief List available logs in /logs as JSON: [{ "name": "...", "size": N }, ...]
 */
void WebServerManager::handleApiLogsList() {
  StaticJsonDocument<4096> doc;
  JsonArray arr = doc.to<JsonArray>();

  File dir = SD.open("/logs");
  if (!dir || !dir.isDirectory()) {
    server.send(200, "application/json", "[]");
    return;
  }

  for (;;) {
    File f = dir.openNextFile();
    if (!f) break;
    if (!f.isDirectory()) {
      // basename
      String base = String(f.name());
      if (base.startsWith("/")) {
        int s = base.lastIndexOf('/');
        if (s >= 0) base = base.substring(s + 1);
      }

      // case-insensitive .txt
      String lower = base; lower.toLowerCase();
      if (lower.endsWith(".txt")) {
        JsonObject o = arr.createNestedObject();
        o["name"] = base;
        o["size"] = f.size();
      }
    }
    f.close();
  }
  dir.close();

  String out;
  serializeJson(arr, out);
  server.send(200, "application/json", out);
}

/**
 * @brief Stream a specific log file from /logs. Name is sanitized to basename + .txt.
 *        Example: GET /api/logfile?file=2025-08-19.txt
 *
 * Security:
 * - Rejects names containing '/', '\\', or "..".
 * - Requires .txt (case-insensitive).
 */
void WebServerManager::handleApiLogsFile() {
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "Missing 'file'");
    return;
  }
  String name = server.arg("file");

  // Sanitization: basename only, no paths or traversal
  if (name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 || name.indexOf("..") >= 0) {
    server.send(400, "text/plain", "Bad file name");
    return;
  }

  // Must end with .txt (case-insensitive)
  String lower = name; lower.toLowerCase();
  if (!lower.endsWith(".txt")) {
    server.send(400, "text/plain", "Bad file name");
    return;
  }

  String path = "/logs/" + name;
  File f = SD.open(path);
  if (!f) {
    server.send(404, "text/plain", "Not found");
    return;
  }

  server.streamFile(f, "text/plain");
  f.close();
}
