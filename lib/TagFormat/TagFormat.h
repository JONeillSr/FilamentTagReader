/**
 * @file    TagFormat.h
 * @brief   Abstract interface for a filament-tag codec (one per tag standard).
 *
 * A TagFormat knows how to recognise, decode, and encode one filament-tag
 * standard (Anycubic binary pages, OpenSpool NDEF/JSON, etc.). The RFIDReader is
 * format-agnostic: on read it asks each registered format "is this yours?" and
 * uses the one that matches; on write it uses the format selected on /setup.
 *
 * All formats translate to/from the neutral FilamentRecord, so the rest of the
 * app (dashboard, OLED, feedback) is unaffected by which standard a tag uses.
 *
 * Design notes:
 *  - decode()/encode() are pure (no hardware). The reader supplies a raw byte
 *    buffer read from the tag and consumes a raw byte buffer to write.
 *  - Formats declare how many pages they need read for detection/decoding, so
 *    the reader can read enough up front (Anycubic = 32 pages; NDEF formats may
 *    need more to capture the whole message).
 */

#pragma once
#include <Arduino.h>
#include "FilamentTag.h"     // FilamentRecord, ReadStatus

class TagFormat {
public:
    virtual ~TagFormat() {}

    // Short stable id used in settings and the UI (e.g. "anycubic", "openspool").
    virtual const char* id() const = 0;
    // Human-readable name for the UI (e.g. "Anycubic", "OpenSpool").
    virtual const char* name() const = 0;

    // How many 4-byte pages the reader should read from the tag before calling
    // detect()/decode(). Lets NDEF formats ask for more of the tag than the
    // Anycubic fixed layout needs.
    virtual uint8_t pagesToRead() const = 0;

    // Does this raw buffer look like this format's tag? Cheap structural check
    // (magic byte, NDEF TLV, etc.) used for read-time auto-detection.
    virtual bool detect(const uint8_t* buf, uint16_t len) const = 0;

    // Decode a raw buffer into a FilamentRecord.
    virtual ReadStatus decode(const uint8_t* buf, uint16_t len,
                              const char* uidHex,
                              FilamentRecord& out) const = 0;

    // Encode a record into a raw buffer ready to write. Returns the number of
    // bytes to write (from offset 0), or 0 on failure. `pagesToWrite` receives
    // the number of 4-byte pages those bytes occupy (caller writes that many).
    virtual uint16_t encode(const FilamentRecord& rec,
                            uint8_t* buf, uint16_t bufLen,
                            uint8_t& pagesToWrite) const = 0;

    // Which 4-byte page numbers this format writes, given an encoded buffer of
    // `pagesToWrite` pages. `out` is filled with up to `maxPages` page numbers;
    // returns the count. Default: a contiguous run starting at page 4 (NDEF-style
    // formats). The Anycubic format overrides this with its scattered page set.
    virtual uint8_t writePages(uint8_t pagesToWrite,
                               uint8_t* out, uint8_t maxPages) const {
        uint8_t n = 0;
        for (uint8_t p = 4; p < 4 + pagesToWrite && n < maxPages; p++)
            out[n++] = p;
        return n;
    }
};
