/**
 * @file    Feedback.h
 * @brief   Reusable non-blocking RGB LED + active-buzzer feedback helper.
 *
 * Common-cathode tri-color LED (each leg lit HIGH) and an active buzzer (drive
 * HIGH = sound). Everything is non-blocking: beeps and LED pulses are timed in
 * an update() call driven from a light FreeRTOS task, never with delay().
 *
 * Polarity is configurable so the same module works for common-anode parts.
 */

#pragma once
#include <Arduino.h>

class Feedback {
public:
    Feedback(uint8_t rPin, uint8_t gPin, uint8_t bPin,
             uint8_t buzzerPin, bool ledActiveHigh = true);

    // Configure pins and spawn the update task.
    void begin();

    // Solid color helpers (0/255 per channel; PWM not required for indicator).
    void setColor(uint8_t r, uint8_t g, uint8_t b);
    void off();

    // High-level cues (non-blocking; return immediately).
    void cueSuccess();   // green + short double beep
    void cueError();     // red + long beep
    void cueScan();      // brief blue blip (tag detected, decoding)
    void cueWrite();     // blue + short double beep (a good verified write)

private:
    uint8_t _r, _g, _b, _buz;
    bool    _activeHigh;

    // Cue state is written by caller tasks (cueSuccess/cueError/cueScan/setColor)
    // and read/modified by the feedback task in update(). Access is therefore
    // cross-task. `volatile` does NOT provide atomicity or ordering on this
    // multi-core MCU, so a short portMUX critical section guards the shared
    // scalars instead. The protected region is only a few assignments long, so
    // the spinlock is held very briefly and never blocks.
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;

    // Beep scheduling (guarded by _mux).
    uint32_t _beepUntil  = 0;   // millis to stop the current beep tone
    uint8_t  _beepsLeft  = 0;   // remaining beeps in the active pattern
    uint16_t _beepOnMs   = 0;   // on-time per beep
    uint16_t _beepGapMs  = 0;   // gap between beeps
    uint32_t _nextBeepAt = 0;   // millis at which the next beep may start
    bool     _buzOn      = false;

    // LED auto-off timestamp, so cues fade without blocking (guarded by _mux).
    uint32_t _ledOffAt   = 0;

    void writeLed(uint8_t r, uint8_t g, uint8_t b);
    void writeBuzzer(bool on);
    void startBeeps(uint8_t count, uint16_t onMs, uint16_t gapMs);

    static void taskFn(void* pv);
    void        update();
};
