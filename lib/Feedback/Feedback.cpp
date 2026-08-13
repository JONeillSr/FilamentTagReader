/**
 * @file    Feedback.cpp
 * @brief   Non-blocking RGB LED + active buzzer implementation.
 */

#include "Feedback.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

Feedback::Feedback(uint8_t rPin, uint8_t gPin, uint8_t bPin,
                   uint8_t buzzerPin, bool ledActiveHigh)
    : _r(rPin), _g(gPin), _b(bPin), _buz(buzzerPin), _activeHigh(ledActiveHigh) {}

void Feedback::begin() {
    pinMode(_r, OUTPUT);
    pinMode(_g, OUTPUT);
    pinMode(_b, OUTPUT);
    pinMode(_buz, OUTPUT);
    off();
    writeBuzzer(false);
    // Light task: 10ms cadence is plenty for indicator timing.
    xTaskCreate(taskFn, "feedback", 2048, this, 1, NULL);
}

// Common-cathode: HIGH lights the leg. Invert if active-low part.
void Feedback::writeLed(uint8_t r, uint8_t g, uint8_t b) {
    auto lvl = [&](uint8_t v) -> int {
        bool on = v > 127;
        if (!_activeHigh) on = !on;
        return on ? HIGH : LOW;
    };
    digitalWrite(_r, lvl(r));
    digitalWrite(_g, lvl(g));
    digitalWrite(_b, lvl(b));
}

void Feedback::writeBuzzer(bool on) {
    // Only the buzzer task calls this (from update()), but _buzOn is also read
    // there across iterations, so update it under the lock for consistency.
    taskENTER_CRITICAL(&_mux);
    _buzOn = on;
    taskEXIT_CRITICAL(&_mux);
    digitalWrite(_buz, on ? HIGH : LOW);   // active buzzer: HIGH = sound
}

void Feedback::setColor(uint8_t r, uint8_t g, uint8_t b) {
    taskENTER_CRITICAL(&_mux);
    _ledOffAt = 0;            // hold this color until explicitly changed
    taskEXIT_CRITICAL(&_mux);
    writeLed(r, g, b);
}

void Feedback::off() {
    writeLed(0, 0, 0);
}

void Feedback::startBeeps(uint8_t count, uint16_t onMs, uint16_t gapMs) {
    taskENTER_CRITICAL(&_mux);
    _beepsLeft  = count;
    _beepOnMs   = onMs;
    _beepGapMs  = gapMs;
    _nextBeepAt = millis();   // first beep fires on the next update() tick
    taskEXIT_CRITICAL(&_mux);
}

void Feedback::cueSuccess() {
    setColor(0, 255, 0);              // green
    taskENTER_CRITICAL(&_mux);
    _ledOffAt = millis() + 1500;      // auto-off after 1.5s
    taskEXIT_CRITICAL(&_mux);
    startBeeps(2, 60, 80);            // short double beep
}

void Feedback::cueError() {
    setColor(255, 0, 0);              // red
    taskENTER_CRITICAL(&_mux);
    _ledOffAt = millis() + 1500;
    taskEXIT_CRITICAL(&_mux);
    startBeeps(1, 400, 0);            // one long beep
}

void Feedback::cueScan() {
    setColor(0, 0, 255);              // blue blip
    taskENTER_CRITICAL(&_mux);
    _ledOffAt = millis() + 250;
    taskEXIT_CRITICAL(&_mux);
}

void Feedback::cueWrite() {
    setColor(0, 0, 255);              // blue = good write
    taskENTER_CRITICAL(&_mux);
    _ledOffAt = millis() + 1500;      // hold like the other success cues
    taskEXIT_CRITICAL(&_mux);
    startBeeps(2, 60, 80);            // short double beep = good (same as read)
}

void Feedback::taskFn(void* pv) {
    Feedback* self = static_cast<Feedback*>(pv);
    for (;;) {
        self->update();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void Feedback::update() {
    uint32_t now = millis();

    // Snapshot shared state under the lock, decide what hardware action to take,
    // then act on the hardware OUTSIDE the lock (never hold a spinlock across a
    // GPIO call). Each branch sets a local "action" flag.
    bool doLedOff   = false;
    bool doBuzOff   = false;
    bool doBuzOn    = false;

    taskENTER_CRITICAL(&_mux);

    // ---- LED auto-off ----
    if (_ledOffAt != 0 && (int32_t)(now - _ledOffAt) >= 0) {
        _ledOffAt = 0;
        doLedOff  = true;
    }

    // ---- Buzzer pattern ----
    if (_buzOn) {
        // Currently sounding: stop when the on-window elapses.
        if ((int32_t)(now - _beepUntil) >= 0) {
            doBuzOff = true;
            if (_beepsLeft > 0) {
                _nextBeepAt = now + _beepGapMs;   // schedule next beep after gap
            }
        }
    } else if (_beepsLeft > 0 && (int32_t)(now - _nextBeepAt) >= 0) {
        // Time to start the next beep in the pattern.
        _beepsLeft  = (uint8_t)(_beepsLeft - 1);  // explicit RMW (not on volatile)
        _beepUntil  = now + _beepOnMs;
        doBuzOn     = true;
    }

    taskEXIT_CRITICAL(&_mux);

    // ---- Hardware actions, performed outside the critical section ----
    if (doLedOff) off();
    if (doBuzOff) writeBuzzer(false);
    if (doBuzOn)  writeBuzzer(true);
}
