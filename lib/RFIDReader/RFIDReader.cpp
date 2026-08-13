/**
 * @file    RFIDReader.cpp
 * @brief   Implementation of the MFRC522 NTAG reader task.
 *
 * NTAG21x note: MIFARE Ultralight / NTAG cards are read 4 pages (16 bytes) at a
 * time via the READ (0x30) command. MFRC522::MIFARE_Read() returns 16 bytes
 * starting at the requested page. We therefore loop in steps of 4 pages to
 * fill the FilamentTag buffer.
 */

#include "RFIDReader.h"
#include <SPI.h>
#include <MFRC522.h>
#include "Events.h"
#include "FilamentTag.h"
#include "TagFormat.h"
#include "TagFormatRegistry.h"

// The MFRC522 instance is file-local; only one reader per project here.
static MFRC522* s_mfrc = nullptr;

RFIDReader::RFIDReader(uint8_t ssPin, uint8_t rstPin,
                       uint8_t sckPin, uint8_t misoPin, uint8_t mosiPin)
    : _ssPin(ssPin), _rstPin(rstPin),
      _sckPin(sckPin), _misoPin(misoPin), _mosiPin(mosiPin) {}

void RFIDReader::logLine(const String& s) {
    if (_log) _log(s);
    else      Serial.println(s);
}

// ---- Status accessors (called from the web task) ---------------------------
// Each snapshots a guarded field. For the String, the value is copied out under
// the lock into a local and returned by value; the copy itself is cheap and the
// critical section stays short.

bool RFIDReader::isReaderPresent() const {
    taskENTER_CRITICAL(&_statusMux);
    bool v = _readerOk;
    taskEXIT_CRITICAL(&_statusMux);
    return v;
}

String RFIDReader::lastUid() const {
    // The reader task is the ONLY writer of _lastUid, and it assigns it outside
    // any critical section. We therefore copy it here without taking the lock:
    // copying a String allocates, and allocating while holding a portMUX (which
    // disables interrupts on this core) is exactly what we want to avoid. A
    // web-side read at worst returns a momentarily stale UID, which is harmless
    // for a status display.
    return _lastUid;
}

bool RFIDReader::lastReadOk() const {
    taskENTER_CRITICAL(&_statusMux);
    bool v = _lastOk;
    taskEXIT_CRITICAL(&_statusMux);
    return v;
}

uint32_t RFIDReader::readCount() const {
    taskENTER_CRITICAL(&_statusMux);
    uint32_t v = _readCount;
    taskEXIT_CRITICAL(&_statusMux);
    return v;
}

// ---- Write-mode accessors (all guarded by _statusMux) ----------------------
void RFIDReader::setMode(Mode m) {
    taskENTER_CRITICAL(&_statusMux);
    _mode = m;
    taskEXIT_CRITICAL(&_statusMux);
}

RFIDReader::Mode RFIDReader::mode() const {
    taskENTER_CRITICAL(&_statusMux);
    Mode m = _mode;
    taskEXIT_CRITICAL(&_statusMux);
    return m;
}

void RFIDReader::setPendingWrite(const FilamentRecord& rec) {
    taskENTER_CRITICAL(&_statusMux);
    _pending    = rec;
    _hasPending = true;
    taskEXIT_CRITICAL(&_statusMux);
}

void RFIDReader::clearPendingWrite() {
    taskENTER_CRITICAL(&_statusMux);
    _hasPending = false;
    taskEXIT_CRITICAL(&_statusMux);
}

bool RFIDReader::hasPendingWrite() const {
    taskENTER_CRITICAL(&_statusMux);
    bool h = _hasPending;
    taskEXIT_CRITICAL(&_statusMux);
    return h;
}

String RFIDReader::writeFormatName() const {
    // _writeFormat is set once at startup/from /setup and not changed from the
    // reader task, so no lock is needed for this read.
    return _writeFormat ? String(_writeFormat->name()) : String("");
}

String RFIDReader::lastReadFormat() const {
    taskENTER_CRITICAL(&_statusMux);
    String f = _lastReadFormat;
    taskEXIT_CRITICAL(&_statusMux);
    return f;
}

bool RFIDReader::begin(QueueHandle_t evtQueue) {
    _evtQueue = evtQueue;

    // Bring up SPI on the project-specified pins. The -1 SS arg lets us drive
    // SS ourselves via MFRC522.
    SPI.begin(_sckPin, _misoPin, _mosiPin, _ssPin);

    s_mfrc = new MFRC522(_ssPin, _rstPin);
    s_mfrc->PCD_Init();
    vTaskDelay(pdMS_TO_TICKS(50));   // let the PCD settle (non-blocking)

    // Probe the reader version register to confirm it is actually wired up.
    // 0x00 / 0xFF are the classic "nothing there" responses.
    uint8_t v = s_mfrc->PCD_ReadRegister(MFRC522::VersionReg);
    bool present = (v != 0x00 && v != 0xFF);
    taskENTER_CRITICAL(&_statusMux);
    _readerOk = present;
    taskEXIT_CRITICAL(&_statusMux);

    if (present) {
        logLine("[rfid] MFRC522 online, version 0x" + String(v, HEX));
    } else {
        logLine("[rfid] MFRC522 NOT found (ver 0x" + String(v, HEX) +
                ") - check wiring/power");
    }

    // Spawn the poller regardless; if the reader is absent it simply reports so
    // and the status page reflects it (no busy-spin, fixed cadence).
    xTaskCreate(readerTask, "rfid", 4096, this, 2, NULL);
    return present;
}

void RFIDReader::readerTask(void* pv) {
    RFIDReader* self = static_cast<RFIDReader*>(pv);
    for (;;) {
        self->serviceOnce();
        vTaskDelay(pdMS_TO_TICKS(120));   // poll cadence; non-blocking
    }
}

void RFIDReader::serviceOnce() {
    if (!s_mfrc) return;

    // Is a new card present?
    if (!s_mfrc->PICC_IsNewCardPresent()) {
        // Field empty: if a tag was held, clear the de-bounce after a gap so a
        // re-tap of the same spool registers again.
        if (_heldUid.length() && (millis() - _lastSeenMs > 800)) {
            _heldUid = "";
        }
        return;
    }
    if (!s_mfrc->PICC_ReadCardSerial()) return;

    // Build the UID hex string.
    String uid;
    for (byte i = 0; i < s_mfrc->uid.size; i++) {
        if (s_mfrc->uid.uidByte[i] < 0x10) uid += '0';
        uid += String(s_mfrc->uid.uidByte[i], HEX);
    }
    uid.toUpperCase();
    _lastSeenMs = millis();

    // De-bounce: same UID still held -> ignore until it leaves.
    if (uid == _heldUid) {
        s_mfrc->PICC_HaltA();
        return;
    }
    _heldUid = uid;

    logLine("[rfid] tag detected UID=" + uid);

    // ---- Write-mode branch -------------------------------------------------
    // If we are in WRITE mode and a record is queued, write it to this tag and
    // verify, then emit a write-result event. Otherwise fall through to the
    // normal read/decode path below.
    {
        taskENTER_CRITICAL(&_statusMux);
        bool           doWrite = (_mode == Mode::Write) && _hasPending;
        FilamentRecord rec     = _pending;
        taskEXIT_CRITICAL(&_statusMux);

        if (doWrite) {
            bool ok = writeAndVerify(rec, uid);

            AppEvent ev{};
            strncpy(ev.filament.uid, uid.c_str(), sizeof(ev.filament.uid) - 1);
            ev.type = ok ? EVT_TAG_WRITTEN : EVT_WRITE_ERROR;
            ev.filament.ok = ok;
            ev.filament.errorCode = 0;
            if (_evtQueue) xQueueSend(_evtQueue, &ev, 0);

            _lastUid = uid;
            taskENTER_CRITICAL(&_statusMux);
            _lastOk = ok;
            if (ok) _hasPending = false;   // consume the record on success only
            taskEXIT_CRITICAL(&_statusMux);

            s_mfrc->PICC_HaltA();
            return;
        }
    }

    // ---- Read the page range into a flat buffer ----------------------------
    // Sized for the largest format (OpenSpool NDEF needs more than the 32-page
    // Anycubic window). Read depth follows the registry's max pagesToRead().
    static const uint16_t RBUF = 256;            // 64 pages
    uint8_t  buf[RBUF];
    memset(buf, 0, sizeof(buf));
    bool readOk = true;

    uint8_t readPages = FT_PAGES_TO_READ;        // default (Anycubic) depth
    if (_formats) {
        uint8_t m = _formats->maxPagesToRead();
        if (m > readPages) readPages = m;
        if ((uint16_t)readPages * 4 > RBUF) readPages = RBUF / 4;
    }

    // READ returns 16 bytes (4 pages) per call; MFRC522 wants an 18-byte buffer
    // (16 data + 2 CRC). Step through in 4-page chunks.
    for (uint8_t page = FT_FIRST_PAGE; page < readPages; page += 4) {
        uint8_t tmp[18];
        uint8_t len = sizeof(tmp);
        MFRC522::StatusCode sc = s_mfrc->MIFARE_Read(page, tmp, &len);
        if (sc != MFRC522::STATUS_OK) {
            // Not fatal for NDEF formats: a short tag may NAK past its end. If we
            // already read the Anycubic window, keep what we have and decode.
            if (page >= FT_PAGES_TO_READ) { readOk = true; break; }
            logLine("[rfid] read failed at page " + String(page) +
                    " (" + String(s_mfrc->GetStatusCodeName(sc)) + ")");
            readOk = false;
            break;
        }
        // Copy the 16 valid data bytes into the flat buffer.
        uint16_t dstOff = (uint16_t)page * 4;
        for (uint8_t b = 0; b < 16 && (dstOff + b) < RBUF; b++) {
            buf[dstOff + b] = tmp[b];
        }
    }

    AppEvent ev{};
    strncpy(ev.filament.uid, uid.c_str(), sizeof(ev.filament.uid) - 1);

    if (!readOk) {
        // _lastUid is a heap-backed String; assign it outside the spinlock so we
        // never allocate inside a critical section. The reader task is its only
        // writer, so a web-side read at worst sees a momentarily stale UID.
        _lastUid = uid;
        taskENTER_CRITICAL(&_statusMux);
        _lastOk = false;
        taskEXIT_CRITICAL(&_statusMux);
        ev.type = EVT_TAG_ERROR;
        ev.filament.ok = false;
        ev.filament.errorCode = (uint8_t)ReadStatus::READ_FAILED;
        if (_evtQueue) xQueueSend(_evtQueue, &ev, 0);
        s_mfrc->PICC_HaltA();
        return;
    }

    // ---- Decode ------------------------------------------------------------
    // If a format registry is wired, auto-detect which standard this tag uses
    // and decode with it; otherwise use the built-in Anycubic codec directly
    // (original behavior). FilamentRecord is the shared neutral form either way.
    FilamentRecord rec;
    ReadStatus st;
    if (_formats) {
        TagFormat* fmt = _formats->detect(buf, sizeof(buf));
        if (fmt) {
            st = fmt->decode(buf, sizeof(buf), uid.c_str(), rec);
            logLine("[rfid] format detected: " + String(fmt->name()));
            taskENTER_CRITICAL(&_statusMux);
            _lastReadFormat = String(fmt->name());
            taskEXIT_CRITICAL(&_statusMux);
        } else {
            st = ReadStatus::BAD_HEADER;
            rec.valid = false;
            logLine("[rfid] no known tag format matched");
        }
    } else {
        st = FilamentTag::decode(buf, sizeof(buf), uid.c_str(), rec);
        taskENTER_CRITICAL(&_statusMux);
        _lastReadFormat = "Anycubic";
        taskEXIT_CRITICAL(&_statusMux);
    }

    if (rec.valid) {
        FilamentTag::storeSet(rec);
        _lastUid = uid;                       // String assign outside the lock
        taskENTER_CRITICAL(&_statusMux);
        _lastOk = true;
        _readCount++;                         // plain RMW, now guarded (no volatile)
        taskEXIT_CRITICAL(&_statusMux);
        logLine("[rfid] decoded SKU=" + String(rec.sku) +
                " type=" + String(rec.type) +
                " color=" + String(rec.colorHex));
        ev.type = EVT_FILAMENT_READ;
        ev.filament.ok = true;
        ev.filament.errorCode = (uint8_t)st;   // OK or BAD_HEADER (soft)
    } else {
        _lastUid = uid;                       // String assign outside the lock
        taskENTER_CRITICAL(&_statusMux);
        _lastOk = false;
        taskEXIT_CRITICAL(&_statusMux);
        logLine("[rfid] decode failed (status " + String((int)st) + ")");
        ev.type = EVT_TAG_ERROR;
        ev.filament.ok = false;
        ev.filament.errorCode = (uint8_t)st;
    }

    if (_evtQueue) xQueueSend(_evtQueue, &ev, 0);

    s_mfrc->PICC_HaltA();    // stop talking to this card
}

// ---- writeAndVerify --------------------------------------------------------
// Encode the record into a page buffer, write the format's pages to the tag via
// Ultralight WRITE (4 bytes/page), then read the whole range back and confirm
// the written pages match. Returns true only on a verified write. Runs in the
// reader task, which owns s_mfrc.
bool RFIDReader::writeAndVerify(const FilamentRecord& rec, const String& uid) {
    if (!s_mfrc) return false;

    // Buffer large enough for the biggest format (OpenSpool NDEF needs more than
    // the Anycubic 128-byte page window).
    static const uint16_t WBUF = 256;
    uint8_t buf[WBUF];
    memset(buf, 0, sizeof(buf));

    // Encode with the selected write format (set on /setup). If none is wired,
    // fall back to the built-in Anycubic codec so behavior matches Stage 1.
    uint8_t  pagesToWrite = 0;
    uint16_t nbytes       = 0;
    if (_writeFormat) {
        nbytes = _writeFormat->encode(rec, buf, sizeof(buf), pagesToWrite);
        if (nbytes == 0) {
            logLine("[rfid] write aborted: encode failed (" +
                    String(_writeFormat->name()) + ")");
            return false;
        }
    } else {
        if (!FilamentTag::encode(rec, buf, sizeof(buf))) {
            logLine("[rfid] write aborted: record failed to encode");
            return false;
        }
        pagesToWrite = FT_PAGES_TO_READ;
    }

    // Ask the format which page numbers to write.
    uint8_t pages[64];
    uint8_t nPages;
    if (_writeFormat) {
        nPages = _writeFormat->writePages(pagesToWrite, pages, sizeof(pages));
    } else {
        // Anycubic scattered set (Stage-1 fallback).
        static const uint8_t kPages[] = {
            4, 5, 6, 7, 8, 10, 11, 12, 13, 15, 16, 17, 18, 20, 24, 29, 30, 31
        };
        nPages = sizeof(kPages);
        memcpy(pages, kPages, nPages);
    }

    // ---- Write phase -------------------------------------------------------
    for (uint8_t i = 0; i < nPages; i++) {
        uint8_t  page = pages[i];
        uint16_t off  = (uint16_t)page * 4;
        if (off + 4 > sizeof(buf)) continue;          // safety bound
        MFRC522::StatusCode sc =
            s_mfrc->MIFARE_Ultralight_Write(page, &buf[off], 4);
        if (sc != MFRC522::STATUS_OK) {
            logLine("[rfid] write failed at page " + String(page) +
                    " (" + String(s_mfrc->GetStatusCodeName(sc)) + ")");
            return false;
        }
    }

    // ---- Verify phase: read the written pages back and compare -------------
    // Read across the full span of written pages (lowest..highest), then compare
    // each written page's 4 bytes.
    uint8_t hiPage = 4;
    for (uint8_t i = 0; i < nPages; i++) if (pages[i] > hiPage) hiPage = pages[i];
    uint16_t spanPages = (uint16_t)hiPage + 1;

    uint8_t rb[WBUF];
    memset(rb, 0, sizeof(rb));
    for (uint8_t page = 4; page < spanPages; page += 4) {
        uint8_t tmp[18];
        uint8_t len = sizeof(tmp);
        MFRC522::StatusCode sc = s_mfrc->MIFARE_Read(page, tmp, &len);
        if (sc != MFRC522::STATUS_OK) {
            logLine("[rfid] verify read failed at page " + String(page) +
                    " (" + String(s_mfrc->GetStatusCodeName(sc)) + ")");
            return false;
        }
        uint16_t dstOff = (uint16_t)page * 4;
        for (uint8_t b = 0; b < 16 && (dstOff + b) < sizeof(rb); b++) {
            rb[dstOff + b] = tmp[b];
        }
    }

    for (uint8_t i = 0; i < nPages; i++) {
        uint16_t off = (uint16_t)pages[i] * 4;
        if (off + 4 > sizeof(buf)) continue;
        if (memcmp(&buf[off], &rb[off], 4) != 0) {
            logLine("[rfid] verify mismatch at page " + String(pages[i]));
            return false;
        }
    }

    logLine("[rfid] write verified UID=" + uid + " type=" + String(rec.type) +
            (_writeFormat ? " fmt=" + String(_writeFormat->name()) : ""));
    return true;
}
