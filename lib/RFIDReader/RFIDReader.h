/**
 * @file    RFIDReader.h
 * @brief   Reusable MFRC522 (RC522 / HW-126) NTAG reader running in its own
 *          FreeRTOS task.
 *
 * Responsibilities:
 *   - Own the MFRC522 driver and the SPI bus setup for it.
 *   - Poll for a tag in a dedicated task (non-blocking; vTaskDelay only).
 *   - On a new tag, read the page range FilamentTag needs, decode it, store the
 *     result, and push an AppEvent (EVT_FILAMENT_READ or EVT_TAG_ERROR).
 *   - De-bounce: the same UID held on the reader does not re-fire until the tag
 *     leaves the field and returns (or a cooldown elapses).
 *
 * This module is app-agnostic about what the tag means beyond "Anycubic
 * filament"; the byte-layout knowledge lives in FilamentTag. Logging is done
 * through an injected log callback so the module does not depend on WebService
 * directly (keeps lib/ from reaching into a specific project service).
 */

#pragma once
#include <Arduino.h>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "FilamentTag.h"      // for FilamentRecord (used in the write API)

class RFIDReader {
public:
    // Log sink: the app passes a function that forwards to WebService::log so
    // lines land on /webserial, telnet, and Serial. Optional.
    using LogFn = std::function<void(const String&)>;

    // Pin assignments come from the project Pins.h and are passed in at
    // construction so this module carries no board knowledge.
    RFIDReader(uint8_t ssPin, uint8_t rstPin,
               uint8_t sckPin, uint8_t misoPin, uint8_t mosiPin);

    void setLogger(LogFn fn) { _log = fn; }

    // Initialise SPI + MFRC522 and spawn the reader task. `evtQueue` is the
    // shared app event queue (created in main before this is called).
    bool begin(QueueHandle_t evtQueue);

    // Status helpers for the /status page. These run on the web-server task
    // while the reader task updates the same fields, so each one snapshots the
    // value under a short critical section. The accessors stay logically const
    // (they only read), so the guarding spinlock is declared mutable below.
    bool     isReaderPresent() const;
    String   lastUid()         const;
    bool     lastReadOk()      const;
    uint32_t readCount()       const;

    // ---- Write mode --------------------------------------------------------
    // The reader operates in one of two modes. In READ mode (default) a tap
    // decodes and reports the filament. In WRITE mode the next tap writes the
    // pending record to the tag, then reads it back to verify.
    enum class Mode : uint8_t { Read = 0, Write = 1 };
    void  setMode(Mode m);
    Mode  mode() const;

    // Queue a record to write on the next tap (used in WRITE mode). Thread-safe;
    // copies the record under the status lock. Until one is set, write-mode taps
    // do nothing. Cleared automatically after a successful verified write so a
    // spool is not written twice by accident.
    void  setPendingWrite(const FilamentRecord& rec);
    void  clearPendingWrite();
    bool  hasPendingWrite() const;

    // ---- Pluggable tag formats (optional) ----------------------------------
    // If a registry is set, reads auto-detect the format and writes use the
    // selected write-format. If left null, the reader uses the built-in Anycubic
    // codec directly (original behavior), so this is a no-op until wired.
    void setFormatRegistry(class TagFormatRegistry* reg) { _formats = reg; }
    // The format used for writing (chosen on /setup). Defaults to the first
    // registered format if not set.
    void setWriteFormat(class TagFormat* fmt) { _writeFormat = fmt; }
    // Display name of the current write format (e.g. "Anycubic"), or "" if none.
    String writeFormatName() const;
    // Name of the format that decoded the most recent successful read
    // (e.g. "Anycubic"), or "" if none yet.
    String lastReadFormat() const;

private:
    uint8_t _ssPin, _rstPin, _sckPin, _misoPin, _mosiPin;

    QueueHandle_t _evtQueue  = nullptr;
    LogFn         _log       = nullptr;

    // Reader-present flag and per-read status are written by the reader task and
    // read by the web task. `volatile` does not provide atomicity or ordering on
    // this multi-core MCU, so a short portMUX critical section guards them. The
    // mutex is mutable so the const status accessors can take it.
    mutable portMUX_TYPE _statusMux = portMUX_INITIALIZER_UNLOCKED;
    bool        _readerOk   = false;     // guarded by _statusMux
    String      _lastUid    = "";        // guarded by _statusMux
    bool        _lastOk     = false;     // guarded by _statusMux
    uint32_t    _readCount  = 0;         // guarded by _statusMux

    // These are touched only by the reader task, so they need no guard.
    String      _heldUid    = "";        // UID currently sitting on the reader
    uint32_t    _lastSeenMs = 0;
    String      _lastReadFormat = "";    // format name of the last good read

    // Write-mode state (guarded by _statusMux).
    Mode           _mode        = Mode::Read;
    bool           _hasPending  = false;
    FilamentRecord _pending;             // record to write on the next tap

    // Optional pluggable formats (null = use built-in Anycubic codec directly).
    class TagFormatRegistry* _formats     = nullptr;
    class TagFormat*         _writeFormat = nullptr;

    void logLine(const String& s);
    static void readerTask(void* pv);
    void        serviceOnce();                // one poll iteration
    // Write the given record to the currently-selected card, then read back and
    // verify the bytes. Must be called only from the reader task (owns s_mfrc).
    // Returns true only if the write verified. `uid` is for logging/events.
    bool        writeAndVerify(const FilamentRecord& rec, const String& uid);
};
