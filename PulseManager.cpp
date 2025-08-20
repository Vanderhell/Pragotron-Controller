/**
 * @file    PulseManager.cpp
 * @brief   H-bridge pulse driver for minute-step coils (A/B alternation).
 *
 * Overview
 * --------
 * - Alternates direction on each trigger (A then B then A ...).
 * - Enforces a minimal gap between pulses to avoid double-triggering.
 * - Provides configurable pulse duration and post-pulse dead-time.
 */

#include "PulseManager.h"

PulseManager::PulseManager(int in1Pin, int in2Pin)
    : pinIn1(in1Pin), pinIn2(in2Pin) {}

/**
 * @brief Initialize GPIO and internal state.
 *
 * Sets both bridge inputs LOW (coast) and prepares the alternation so that
 * the first emitted pulse is A.
 */
void PulseManager::begin() {
    pinMode(pinIn1, OUTPUT);
    pinMode(pinIn2, OUTPUT);
    digitalWrite(pinIn1, LOW);
    digitalWrite(pinIn2, LOW);
    lastWasA   = false;   // after startup, the first pulse will be A
    lastTrigMs = 0;
}

/**
 * @brief Configure pulse waveform timing.
 * @param pulseDurationMs  Active time the bridge drives one polarity.
 * @param pauseAfterMs     Dead-time (coast) after each pulse.
 */
void PulseManager::setImpulseTiming(int pulseDurationMs, int pauseAfterMs) {
    durationMs = pulseDurationMs;
    pauseMs    = pauseAfterMs;
}

/**
 * @brief Set a minimal enforced gap between trigger calls.
 * @param ms Minimum milliseconds between pulses (anti-duplicate guard).
 */
void PulseManager::setMinGapMs(uint32_t ms) {
    minGapMs = ms;
}

/**
 * @brief Emit one pulse if allowed by anti-duplicate guard.
 * @param allowBurst If true, bypass the min-gap guard.
 * @return true if a pulse was emitted; false if skipped by guard.
 *
 * The required gap is max(minGapMs, durationMs + pauseMs + 50ms safety).
 * On each accepted trigger, the direction alternates A/B.
 */
bool PulseManager::triggerPulse(bool allowBurst) {
    const uint32_t now = millis();

    // If called too soon after the last pulse (and not in burst mode), skip.
    const uint32_t requiredGap = max<uint32_t>(minGapMs, (uint32_t)(durationMs + pauseMs + 50));
    if (!allowBurst && (now - lastTrigMs) < requiredGap) {
        // Serial.printf("[PULSE] skipped duplicate (%lums < %lums)\n", now - lastTrigMs, requiredGap);
        return false;
    }

    if (lastWasA) pulseB(); else pulseA();
    lastWasA   = !lastWasA;
    lastTrigMs = millis();     // timestamp after the pulse completes

    // Dead-time is handled inside pulseA/pulseB (stopBridge + delay(pauseMs)).
    return true;
}

/**
 * @brief Drive polarity A: IN1=HIGH, IN2=LOW for durationMs, then coast.
 */
void PulseManager::pulseA() {
    digitalWrite(pinIn1, HIGH);
    digitalWrite(pinIn2, LOW);
    delay(durationMs);
    stopBridge();              // coast
    delay(pauseMs);            // dead-time
}

/**
 * @brief Drive polarity B: IN1=LOW, IN2=HIGH for durationMs, then coast.
 */
void PulseManager::pulseB() {
    digitalWrite(pinIn1, LOW);
    digitalWrite(pinIn2, HIGH);
    delay(durationMs);
    stopBridge();              // coast
    delay(pauseMs);            // dead-time
}

/**
 * @brief Disable both bridge legs (coast).
 */
inline void PulseManager::stopBridge() {
    digitalWrite(pinIn1, LOW);
    digitalWrite(pinIn2, LOW);
}

// Optional diagnostics / manual forcing
void PulseManager::forceA() { pulseA(); }
void PulseManager::forceB() { pulseB(); }
