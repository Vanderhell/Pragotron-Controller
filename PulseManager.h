/**
 * @file    PulseManager.h
 * @brief   Simple H-bridge pulser with A/B alternation and anti-duplicate guard.
 */

#pragma once
#include <Arduino.h>

/**
 * @class PulseManager
 * @brief Drives two GPIO pins as an H-bridge to generate alternating pulses.
 *
 * Usage:
 *   PulseManager pm(IN1, IN2);
 *   pm.begin();
 *   pm.setImpulseTiming(200, 200);
 *   pm.setMinGapMs(600);
 *   pm.triggerPulse();           // emits A (first), next call emits B, etc.
 */
class PulseManager {
public:
    /**
     * @brief Construct with bridge input pin numbers.
     * @param in1Pin GPIO for bridge input 1.
     * @param in2Pin GPIO for bridge input 2.
     */
    PulseManager(int in1Pin, int in2Pin);

    /// Initialize pins (set as outputs, LOW) and reset internal state.
    void begin();

    /// Configure pulse duration and post-pulse dead-time (milliseconds).
    void setImpulseTiming(int pulseDurationMs, int pauseAfterMs);

    /// Set minimum gap between pulses to avoid double-triggering.
    void setMinGapMs(uint32_t ms);                 // minimum spacing between pulses

    /**
     * @brief Trigger one pulse if guard permits.
     * @param allowBurst If true, ignore the min-gap guard.
     * @return true if a pulse was generated, false if skipped.
     */
    bool triggerPulse(bool allowBurst = false);

    // Optional: direct polarity control for diagnostics.
    void forceA();
    void forceB();

private:
    // ── Hardware pins ────────────────────────────────────────────────────────
    int pinIn1;
    int pinIn2;

    // ── Timing (ms) ─────────────────────────────────────────────────────────
    int durationMs = 200;      ///< Active drive time per pulse.
    int pauseMs    = 200;      ///< Dead-time/coast after each pulse.

    // ── State ────────────────────────────────────────────────────────────────
    bool     lastWasA    = false;   ///< false means last was B → next will be A.
    uint32_t lastTrigMs  = 0;       ///< Timestamp of last pulse completion.
    uint32_t minGapMs    = 600;     ///< Anti-duplicate guard (default).

    // ── Internals ───────────────────────────────────────────────────────────
    void   pulseA();
    void   pulseB();
    inline void stopBridge();
};
