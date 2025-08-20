/**
 * @file    main.cpp  (ESP32 / Arduino)
 * @brief   Pragotron minute clock controller bootstrap.
 *
 * This file wires together all managers (Config, Logger, RTC, State, Pulse,
 * System, Web) and orchestrates the device lifecycle:
 *   1) Initialize SD card (for config/log/state)
 *   2) Load configuration from /config.json
 *   3) Bring up Wi-Fi (so NTP can work)
 *   4) Start Logger (timestamps will be corrected once SystemManager sets time)
 *   5) Initialize State manager (HH:MM persisted in /state.txt)
 *   6) Initialize Pulse outputs that drive the clock coils
 *   7) Initialize SystemManager (catch-up, minute ticks, NTP/TZ, etc.)
 *   8) If online, start the embedded Web UI and register callbacks
 *
 * Notes:
 * - Keep pin assignments in sync with your hardware.
 * - All runtime files (config/log/state) live on SD; device can still run
 *   degraded if SD is absent, but config/log/state features will be limited.
 * - Do NOT modify logic here unless you also update the design docs.
 *
 * Dependencies:
 * - ESP32 Arduino core
 * - SD (SPI) support for the selected board
 * - Project-local managers: ConfigManager, Logger, RTCManager, StateManager,
 *   PulseManager, SystemManager, WebServerManager
 */

#include <WiFi.h>
#include <SD.h>
#include "ConfigManager.h"
#include "Logger.h"
#include "RTCManager.h"
#include "StateManager.h"
#include "PulseManager.h"
#include "SystemManager.h"
#include "WebServerManager.h"

// ──────────────────────────────────────────────────────────────────────────────
// GPIO configuration
// Two output pins controlling the minute-impulse driver (coil A/B or IN1/IN2).
// Adjust to your wiring if needed.
#define PIN_IN1 25
#define PIN_IN2 26

// SD card chip-select pin (set to match your board/shield).
#define SD_CS   5

// ──────────────────────────────────────────────────────────────────────────────
// Global instances (lifetime = whole program)
ConfigManager     configManager;
Logger            logger;
RTCManager        rtcManager;
StateManager      stateManager;
PulseManager      pulseManager(PIN_IN1, PIN_IN2);

// Allocated dynamically to control construction order and pass references.
SystemManager*    systemManager    = nullptr;
WebServerManager* webServerManager = nullptr;

/**
 * @brief Web callback adapter: called when user sets time manually (HH:MM).
 * @param minutes Minutes since 00:00 (0..1439) to set the clock to.
 *
 * Thin thunk to avoid exposing SystemManager directly to the web module.
 */
static void OnClockSetThunk(int minutes) {
  if (systemManager) systemManager->OnManualClockSet(minutes);
}

/**
 * @brief Arduino setup: one-time initialization sequence.
 *
 * Order matters:
 *  - SD first (so config/log/state can mount),
 *  - then Config (to know Wi-Fi/NTP/etc.),
 *  - then Wi-Fi (required for NTP),
 *  - then Logger (SystemManager will soon correct time via NTP),
 *  - then State/Pulse/System,
 *  - finally Web (if Wi-Fi connected).
 */
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("=== SETUP START ===");

  // 1) SD card
  if (!SD.begin(SD_CS)) {
    Serial.println("❌ SD init failed! (CS=5?)");
    // Proceeding: config/log/state features will be limited.
  }

  // 2) Configuration
  configManager.begin("/config.json");

  // 3) Wi-Fi (needed for NTP time sync)
  WiFi.begin(
    configManager.getConfig().wifiSsid.c_str(),
    configManager.getConfig().wifiPassword.c_str()
  );
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries++ < 20) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n📶 Pripojený k WiFi: " + WiFi.SSID());
    Serial.println("🌐 IP adresa: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n⚠️ WiFi nepripojené, NTP teraz neprebehne.");
  }

  // 4) Logger (timestamps will be corrected once SystemManager sets time)
  logger.begin("/logs", configManager.getConfig().debugSerial);
  logger.info("🚀 PragotronController štartuje...");

  // 5) State Manager
  stateManager.begin("/state.txt");

  // 6) Pulse Manager
  pulseManager.begin();

  // 7) System Manager (handles TZ/RTC, NTP, catch-up, minute ticks, etc.)
  systemManager = new SystemManager(
    &configManager, &logger, &rtcManager, &pulseManager, &stateManager
  );
  systemManager->begin();

  // 8) Web Server (optional, only if Wi-Fi is up)
  if (WiFi.status() == WL_CONNECTED) {
    webServerManager = new WebServerManager(&stateManager, &configManager, &rtcManager);
    webServerManager->begin();
    webServerManager->setOnClockSet(OnClockSetThunk);
  }
}

/**
 * @brief Arduino loop: delegate to SystemManager and WebServer.
 *
 * Keep the tick short to preserve catch-up timing accuracy.
 */
void loop() {
  systemManager->loop();

  // Handle incoming HTTP requests when Web UI is enabled.
  if (webServerManager) webServerManager->handleClient();

  // Short delay to allow other tasks to run and to keep timing responsive.
  delay(20);
}
