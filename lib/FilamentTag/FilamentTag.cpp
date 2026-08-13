/**
 * @file    FilamentTag.cpp
 * @brief   Implementation of the Anycubic filament tag decoder + shared store.
 */

#include "FilamentTag.h"
#include <string.h>
#include <stdio.h>

// Static member definitions.
SemaphoreHandle_t FilamentTag::_mutex   = nullptr;
FilamentRecord    FilamentTag::_last;
bool              FilamentTag::_hasData = false;

// Convenience: byte offset of a page's first byte in the flat buffer.
static inline uint16_t pageOffset(uint16_t page) { return page * 4; }

// Read a little-endian 16-bit value at buffer pointer p.
uint16_t FilamentTag::le16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

// Copy an ASCII field spanning `numPages` pages starting at `startPage`,
// trimming at the first NUL and stripping trailing spaces. Output is always
// NUL-terminated and never overruns outSize.
void FilamentTag::copyAsciiField(const uint8_t* buf, uint16_t startPage,
                                 uint16_t numPages, char* out, size_t outSize) {
    size_t   w   = 0;
    uint16_t off = pageOffset(startPage);
    uint16_t n   = numPages * 4;
    for (uint16_t i = 0; i < n && w < outSize - 1; i++) {
        char c = (char)buf[off + i];
        if (c == '\0') break;          // NUL marks end of string
        // Skip non-printable bytes defensively.
        if (c < 0x20 || c > 0x7E) continue;
        out[w++] = c;
    }
    // Trim trailing spaces.
    while (w > 0 && out[w - 1] == ' ') w--;
    out[w] = '\0';
}

ReadStatus FilamentTag::decode(const uint8_t* pageBuf,
                               uint16_t        bufLen,
                               const char*     uidHex,
                               FilamentRecord& out) {
    out = FilamentRecord();   // reset to defaults

    if (pageBuf == nullptr || bufLen < FT_BUFFER_BYTES) {
        return ReadStatus::READ_FAILED;
    }

    // UID.
    if (uidHex) {
        strncpy(out.uid, uidHex, sizeof(out.uid) - 1);
        out.uid[sizeof(out.uid) - 1] = '\0';
    }

    // ---- Header sanity (page 4 = 7B 00 65 00) ------------------------------
    // We treat the first byte (0x7B) as the magic marker. We warn but do not
    // hard-fail on the rest, since the "length/65" field meaning is unconfirmed.
    const uint8_t* hdr = &pageBuf[pageOffset(4)];
    bool headerOk = (hdr[0] == 0x7B);

    // ---- ASCII fields ------------------------------------------------------
    copyAsciiField(pageBuf, 5,  4, out.sku,   sizeof(out.sku));    // pages 5-8
    copyAsciiField(pageBuf, 10, 4, out.brand, sizeof(out.brand));  // pages 10-13
    copyAsciiField(pageBuf, 15, 4, out.type,  sizeof(out.type));   // pages 15-18

    // ---- Color (page 20), stored ABGR little-endian ------------------------
    // Byte order on the tag is A, B, G, R.
    const uint8_t* col = &pageBuf[pageOffset(20)];
    out.colorA = col[0];
    out.colorB = col[1];
    out.colorG = col[2];
    out.colorR = col[3];
    snprintf(out.colorHex, sizeof(out.colorHex), "#%02X%02X%02X",
             out.colorR, out.colorG, out.colorB);

    // ---- Temperatures ------------------------------------------------------
    const uint8_t* ext = &pageBuf[pageOffset(24)];
    out.extruderMinC = le16(ext);
    out.extruderMaxC = le16(ext + 2);

    const uint8_t* bed = &pageBuf[pageOffset(29)];
    out.bedMinC = le16(bed);
    out.bedMaxC = le16(bed + 2);

    // ---- Filament params (page 30) -----------------------------------------
    const uint8_t* fp = &pageBuf[pageOffset(30)];
    out.diameterRaw = le16(fp);          // e.g. 0x00AF = 175
    out.diameterMm  = out.diameterRaw / 100.0f;   // => 1.75
    out.lengthRaw   = le16(fp + 2);      // e.g. 0x014A = 330

    out.readMillis = millis();

    // ---- Validity decision -------------------------------------------------
    // Consider the read good if at least SKU or type came through. The header
    // mismatch is reported but not fatal (some firmware revisions vary here).
    bool anyField = (out.sku[0] != '\0') || (out.type[0] != '\0') ||
                    (out.brand[0] != '\0');

    if (!anyField) {
        out.valid = false;
        return ReadStatus::EMPTY_FIELDS;
    }

    out.valid = true;
    return headerOk ? ReadStatus::OK : ReadStatus::BAD_HEADER;
}

// ============================================================================
//  Shared store
// ============================================================================

// ---- encode (inverse of decode) --------------------------------------------
// Lay a record into the flat page buffer in the exact format decode() reads.
// Numeric fields are little-endian; ASCII fields are NUL-padded across their
// pages. Only the pages this format defines are touched.
static inline void putLe16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}
static void putAscii(uint8_t* buf, uint16_t startPage, uint16_t numPages,
                     const char* s) {
    uint16_t off  = startPage * 4;
    uint16_t span = numPages * 4;
    uint16_t n    = 0;
    while (s && s[n] && n < span) { buf[off + n] = (uint8_t)s[n]; n++; }
    while (n < span) { buf[off + n] = 0x00; n++; }   // NUL-pad the remainder
}

bool FilamentTag::encode(const FilamentRecord& rec,
                         uint8_t*              pageBuf,
                         uint16_t              bufLen) {
    if (pageBuf == nullptr || bufLen < FT_BUFFER_BYTES) return false;
    // Minimum viable record: a type string at least. (Color/temps may be zero.)
    if (rec.type[0] == '\0') return false;

    // Header (page 4): magic 0x7B, then 00 65 00 as the original format uses.
    uint8_t* hdr = &pageBuf[4 * 4];
    hdr[0] = 0x7B; hdr[1] = 0x00; hdr[2] = 0x65; hdr[3] = 0x00;

    // ASCII fields, NUL-padded across their page spans (matching decode()).
    putAscii(pageBuf, 5,  4, rec.sku);     // pages 5-8
    putAscii(pageBuf, 10, 4, rec.brand);   // pages 10-13
    putAscii(pageBuf, 15, 4, rec.type);    // pages 15-18

    // Color (page 20), ABGR byte order.
    uint8_t* col = &pageBuf[20 * 4];
    col[0] = rec.colorA; col[1] = rec.colorB; col[2] = rec.colorG; col[3] = rec.colorR;

    // Extruder temps (page 24): min, max as two LE16.
    uint8_t* ext = &pageBuf[24 * 4];
    putLe16(ext,     rec.extruderMinC);
    putLe16(ext + 2, rec.extruderMaxC);

    // Bed temps (page 29).
    uint8_t* bed = &pageBuf[29 * 4];
    putLe16(bed,     rec.bedMinC);
    putLe16(bed + 2, rec.bedMaxC);

    // Filament params (page 30): diameter raw, length raw.
    uint8_t* fp = &pageBuf[30 * 4];
    putLe16(fp,     rec.diameterRaw ? rec.diameterRaw : 175);  // default 1.75mm
    putLe16(fp + 2, rec.lengthRaw);

    return true;
}

void FilamentTag::storeInit() {
    if (_mutex == nullptr) _mutex = xSemaphoreCreateMutex();
    _hasData = false;
}

void FilamentTag::storeSet(const FilamentRecord& rec) {
    if (!_mutex) return;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _last    = rec;
    _hasData = true;
    xSemaphoreGive(_mutex);
}

FilamentRecord FilamentTag::storeGet() {
    FilamentRecord copy;
    if (!_mutex) return copy;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    copy = _last;
    xSemaphoreGive(_mutex);
    return copy;
}

bool FilamentTag::storeHasData() {
    if (!_mutex) return false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool h = _hasData;
    xSemaphoreGive(_mutex);
    return h;
}
