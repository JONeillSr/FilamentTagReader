/**
 * @file    FilamentTag.h
 * @brief   Decoder + shared store for Anycubic filament NTAG21x tags.
 *
 * Anycubic spools carry an NTAG213/215/216 (4 bytes per page). The relevant
 * fields are laid out as fixed page ranges. This module knows ONLY about the
 * byte layout and how to turn raw pages into a human-readable record; it does
 * not touch any reader hardware. The RFIDReader module hands it a flat page
 * buffer and this module produces a FilamentRecord.
 *
 * Tag layout (page : bytes):
 *   4    7B 00 65 00   header (magic / length) - not decoded, informational
 *   5-8  ASCII          SKU            e.g. "AHPLLB-103"
 *   10-13 ASCII         brand          e.g. "AC" (Anycubic)
 *   15-18 ASCII         type           e.g. "PLA"
 *   20   AA BB GG RR    color, ABGR little-endian byte order
 *   24   min(LE) max(LE) extruder temp, 16-bit little-endian, degrees C
 *   29   min(LE) max(LE) hotbed temp,   16-bit little-endian, degrees C
 *   30   dia(LE) len(LE) diameter (x100 mm) and length (raw units)
 *   31   E8 03 00 00    unknown (1000 LE) - not decoded
 *
 * All multi-byte numeric fields are little-endian. Strings are NUL-padded
 * ASCII; we trim at the first NUL and at any trailing spaces.
 */

#pragma once
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// We read pages 0..(NTAG_PAGES_READ-1) into a flat buffer. Page 31 is the last
// field we care about, so read through 32 pages (0..31) = 128 bytes.
static const uint8_t  FT_FIRST_PAGE      = 0;
static const uint8_t  FT_PAGES_TO_READ   = 32;          // pages 0..31
static const uint16_t FT_BUFFER_BYTES    = FT_PAGES_TO_READ * 4;  // 128

// Result of a decode attempt.
enum class ReadStatus : uint8_t {
    OK            = 0,
    NO_TAG        = 1,   // no tag in field
    READ_FAILED   = 2,   // SPI / auth / read error partway through
    BAD_HEADER    = 3,   // page 4 header did not match expected magic
    EMPTY_FIELDS  = 4,   // read succeeded but key fields were blank
};

// Decoded, display-ready filament record.
struct FilamentRecord {
    bool     valid = false;

    char     uid[24]      = {0};   // hex string of the tag UID
    char     sku[24]      = {0};   // pages 5-8
    char     brand[20]    = {0};   // pages 10-13
    char     type[20]     = {0};   // pages 15-18

    // Color: stored on tag as ABGR. We expose the components plus a CSS hex.
    uint8_t  colorA = 0, colorR = 0, colorG = 0, colorB = 0;
    char     colorHex[8]  = {0};   // "#RRGGBB"

    // Temperatures (degrees C).
    uint16_t extruderMinC = 0, extruderMaxC = 0;
    uint16_t bedMinC      = 0, bedMaxC      = 0;

    // Filament physical params.
    uint16_t diameterRaw  = 0;     // raw 16-bit (e.g. 0x00AF = 175)
    float    diameterMm   = 0.0f;  // diameterRaw / 100.0 (=> 1.75)
    uint16_t lengthRaw    = 0;     // raw 16-bit (e.g. 0x014A = 330)

    uint32_t readMillis   = 0;     // millis() at decode time
};

class FilamentTag {
public:
    // Decode a flat page buffer (FT_BUFFER_BYTES long) into `out`.
    // `uidHex` is the already-formatted UID string from the reader.
    // Returns OK on success; on failure `out.valid` is false and the status
    // explains why. Pure function: no hardware, no globals touched.
    static ReadStatus decode(const uint8_t* pageBuf,
                             uint16_t        bufLen,
                             const char*     uidHex,
                             FilamentRecord& out);

    // Encode a record into a flat page buffer ready to write to a tag. This is
    // the inverse of decode(): it lays out the same page format (header magic,
    // SKU, brand, type, ABGR color, temps, diameter/length) into `pageBuf`.
    // `bufLen` must be at least FT_BUFFER_BYTES. Only the pages this format uses
    // are written; the caller writes them to the tag. Pure function: no hardware.
    // Returns true if the record had the minimum fields needed to encode.
    static bool encode(const FilamentRecord& rec,
                       uint8_t*              pageBuf,
                       uint16_t              bufLen);

    // ---- Shared store (last successful read) -------------------------------
    // The RFID task writes here after a good decode; the web handlers read it.
    // All access is guarded by an internal mutex created in storeInit().
    static void           storeInit();
    static void           storeSet(const FilamentRecord& rec);
    static FilamentRecord storeGet();              // returns a copy
    static bool           storeHasData();

private:
    static SemaphoreHandle_t _mutex;
    static FilamentRecord    _last;
    static bool              _hasData;

    // Helpers.
    static void copyAsciiField(const uint8_t* buf, uint16_t startPage,
                              uint16_t numPages, char* out, size_t outSize);
    static uint16_t le16(const uint8_t* p);
};
