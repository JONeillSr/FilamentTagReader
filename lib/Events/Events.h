#pragma once
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
 * @file    Events.h
 * @brief   App-wide event bus types for a small FreeRTOS application.
 *
 * This is a reusable, application-agnostic event bus. A producer task (here,
 * the RFID reader) fills an AppEvent and pushes it onto appEventQueue; a single
 * consumer task pops events and reacts (updates the display, drives the LED and
 * buzzer, writes the log). Decoupling producers from consumers this way keeps
 * the hardware-polling task free of any UI or networking logic.
 *
 * The enum carries every event variant this application understands. Projects
 * that reuse this header add or remove variants to suit their own needs.
 */

enum EventType : uint8_t {
    EVT_CARD_TAP,        // a card/tag UID was read (UID only, no payload decode)
    EVT_FILAMENT_READ,   // a full Anycubic filament tag was decoded successfully
    EVT_TAG_ERROR,       // a tag was present but could not be read / decoded
    EVT_TAG_WRITTEN,     // a filament tag was written AND verified successfully
    EVT_WRITE_ERROR,     // a write was attempted but failed (write or verify)
};

// Payload for a bare UID read (EVT_CARD_TAP). Useful for access-control style
// uses where only the card identity matters, not any data stored on the card.
struct CardTapEvent {
    char uid[20];
    char cardType[32];
};

// Payload for filament events. The fully-decoded record lives in FilamentTag's
// own mutex-guarded store; this payload carries only the UID and a status code
// so each queue entry stays small and trivially copyable.
struct FilamentEvent {
    char    uid[24];     // hex UID string, e.g. "04A1B2C3D4E5F6"
    bool    ok;          // true on EVT_FILAMENT_READ, false on EVT_TAG_ERROR
    uint8_t errorCode;   // 0 = none; see FilamentTag.h ReadStatus
};

// One event type carrying whichever payload matches `type`. A plain struct
// (rather than a union) keeps copying trivial and avoids lifetime pitfalls when
// passing through a FreeRTOS queue by value.
struct AppEvent {
    EventType     type;
    CardTapEvent  card;      // valid when type == EVT_CARD_TAP
    FilamentEvent filament;  // valid when type == EVT_FILAMENT_READ / EVT_TAG_ERROR
};

// Created in main.cpp during setup(), before any task that uses it is started.
extern QueueHandle_t appEventQueue;
