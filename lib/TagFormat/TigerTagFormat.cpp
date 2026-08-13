/**
 * @file    TigerTagFormat.cpp
 * @brief   Big-endian binary codec for TigerTag tags (basic read/write).
 */

#include "TigerTagFormat.h"
#include "TigerTagDB.h"
#include <string.h>
#include <time.h>

// Field offsets within user memory (page 4 = offset 0). See header for the map.
enum {
    OFF_VERSION   = 0,   // 4
    OFF_PRODUCT   = 4,   // 4
    OFF_MATERIAL  = 8,   // 2
    OFF_DIAMETER  = 10,  // 1
    OFF_ASPECT1   = 11,  // 1
    OFF_ASPECT2   = 12,  // 1
    OFF_TYPE      = 13,  // 1
    OFF_BRAND     = 14,  // 2
    OFF_UNIT      = 16,  // 1
    OFF_COLOR1    = 17,  // 4 (RGBA)
    OFF_COLOR2    = 21,  // 3
    OFF_COLOR3    = 24,  // 3
    OFF_TD        = 27,  // 2
    OFF_MEASURE   = 29,  // 3
    OFF_NOZ_MIN   = 32,  // 1
    OFF_NOZ_MAX   = 33,  // 1
    OFF_DRY_TEMP  = 34,  // 1
    OFF_DRY_TIME  = 35,  // 1
    OFF_BED_MIN   = 36,  // 1
    OFF_BED_MAX   = 37,  // 1
    OFF_TIMESTAMP = 38,  // 4
    OFF_RESERVED  = 42,  // 12
    OFF_MESSAGE   = 54,  // up to 26
    TT_DATA_BYTES = 80,  // pages 4-23
    MSG_MAX       = 26,
};

// User memory begins at page 4 = byte offset 16 in a page-0-based buffer.
static const uint16_t USER_BASE = 16;

// TigerTag fixed IDs.
static const uint8_t  TYPE_FILAMENT = 0x8E;   // 142
static const uint8_t  DIAM_175      = 0x38;   // 56
static const uint8_t  DIAM_285      = 0xDD;   // 221
static const uint8_t  UNIT_GRAMS    = 0x15;   // 21

// Seconds between 1970-01-01 and 2000-01-01 (TigerTag epoch).
static const uint32_t EPOCH_2000 = 946684800UL;

// ---- big-endian helpers ----------------------------------------------------
uint32_t TigerTagFormat::rd32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}
uint16_t TigerTagFormat::rd16(const uint8_t* p) {
    return ((uint16_t)p[0] << 8) | p[1];
}
uint32_t TigerTagFormat::rd24(const uint8_t* p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}
void TigerTagFormat::wr32(uint8_t* p, uint32_t v) {
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
void TigerTagFormat::wr16(uint8_t* p, uint16_t v) {
    p[0] = v >> 8; p[1] = v;
}
void TigerTagFormat::wr24(uint8_t* p, uint32_t v) {
    p[0] = v >> 16; p[1] = v >> 8; p[2] = v;
}

// ---- detect ----------------------------------------------------------------
// A TigerTag is identified by its "ID TigerTag" version field at the start of
// user memory matching a known version constant.
bool TigerTagFormat::detect(const uint8_t* buf, uint16_t len) const {
    if (len < USER_BASE + 4) return false;
    uint32_t ver = rd32(&buf[USER_BASE + OFF_VERSION]);
    return ver == TT_VERSION_OFFLINE || ver == TT_VERSION_PLUS ||
           ver == TT_VERSION_INIT;
}

// ---- decode ----------------------------------------------------------------
ReadStatus TigerTagFormat::decode(const uint8_t* buf, uint16_t len,
                                  const char* uidHex,
                                  FilamentRecord& out) const {
    out = FilamentRecord{};
    out.valid = false;
    if (uidHex) strncpy(out.uid, uidHex, sizeof(out.uid) - 1);
    if (len < USER_BASE + TT_DATA_BYTES) return ReadStatus::READ_FAILED;

    const uint8_t* d = &buf[USER_BASE];

    uint16_t matId    = rd16(&d[OFF_MATERIAL]);
    uint8_t  aspect1  = d[OFF_ASPECT1];
    uint16_t brandId  = rd16(&d[OFF_BRAND]);
    uint8_t  diaId    = d[OFF_DIAMETER];

    // Names from IDs (via DB if available).
    if (_db) {
        String mn = _db->materialName(matId);
        String bn = _db->brandName(brandId);
        String an = _db->aspectName(aspect1);
        // Compose a type string: material (+ aspect if meaningful).
        String typeStr = mn;
        if (an.length() && !an.equalsIgnoreCase("None") && !an.equalsIgnoreCase("-"))
            typeStr += " " + an;
        strncpy(out.type,  typeStr.c_str(), sizeof(out.type) - 1);
        strncpy(out.brand, bn.c_str(),      sizeof(out.brand) - 1);
    } else {
        snprintf(out.type, sizeof(out.type), "mat:%u", matId);
    }

    // Color1 RGBA.
    out.colorR = d[OFF_COLOR1 + 0];
    out.colorG = d[OFF_COLOR1 + 1];
    out.colorB = d[OFF_COLOR1 + 2];
    out.colorA = d[OFF_COLOR1 + 3];
    snprintf(out.colorHex, sizeof(out.colorHex), "#%02X%02X%02X",
             out.colorR, out.colorG, out.colorB);

    // Diameter.
    out.diameterRaw = 175;
    out.diameterMm  = 1.75f;
    if (diaId == DIAM_285) { out.diameterRaw = 285; out.diameterMm = 2.85f; }

    // Temps.
    out.extruderMinC = d[OFF_NOZ_MIN];
    out.extruderMaxC = d[OFF_NOZ_MAX];
    out.bedMinC      = d[OFF_BED_MIN];
    out.bedMaxC      = d[OFF_BED_MAX];

    // Weight (Measure, 3 bytes) -> store into lengthRaw as a carry-through.
    out.lengthRaw = (uint16_t)rd24(&d[OFF_MEASURE]);

    out.valid = (out.type[0] != '\0');
    return out.valid ? ReadStatus::OK : ReadStatus::EMPTY_FIELDS;
}

// ---- encode ----------------------------------------------------------------
uint16_t TigerTagFormat::encode(const FilamentRecord& rec,
                                uint8_t* buf, uint16_t bufLen,
                                uint8_t& pagesToWrite) const {
    pagesToWrite = 0;
    if (!buf || bufLen < USER_BASE + TT_DATA_BYTES) return 0;
    if (rec.type[0] == '\0') return 0;

    // Zero the data region (pages 4-23); leave pages 0-3 and signature alone.
    uint8_t* d = &buf[USER_BASE];
    memset(d, 0, TT_DATA_BYTES);

    // Split rec.type into "material" + optional "finish" (e.g. "PLA Silk").
    String type = rec.type;
    String material = type, finish = "";
    int sp = type.indexOf(' ');
    if (sp > 0) { material = type.substring(0, sp); finish = type.substring(sp + 1); }

    // Resolve IDs via DB (with the alias layers). Without a DB we can only write
    // a minimally-valid tag, which is not useful, so require the DB.
    if (!_db) return 0;

    TigerTagDB::MaterialInfo mi;
    uint16_t aspectId = _db->aspectNoneId();
    _db->resolveMaterial(material, finish, mi, aspectId);
    bool genericBrand = false;
    uint16_t brandId = _db->resolveBrand(rec.brand, genericBrand);

    // Version + product: write a plain offline Maker tag.
    wr32(&d[OFF_VERSION], TT_VERSION_OFFLINE);
    wr32(&d[OFF_PRODUCT], 0xFFFFFFFF);

    wr16(&d[OFF_MATERIAL], mi.id);
    d[OFF_DIAMETER] = (rec.diameterRaw == 285) ? DIAM_285 : DIAM_175;
    d[OFF_ASPECT1]  = (uint8_t)aspectId;
    d[OFF_ASPECT2]  = 0x00;
    d[OFF_TYPE]     = TYPE_FILAMENT;
    wr16(&d[OFF_BRAND], brandId);
    d[OFF_UNIT]     = UNIT_GRAMS;

    // Color1 RGBA; Color2/3 left zero.
    d[OFF_COLOR1 + 0] = rec.colorR;
    d[OFF_COLOR1 + 1] = rec.colorG;
    d[OFF_COLOR1 + 2] = rec.colorB;
    d[OFF_COLOR1 + 3] = rec.colorA ? rec.colorA : 0xFF;

    // TD: 0 (undefined). Measure: weight in grams (from lengthRaw carry).
    wr16(&d[OFF_TD], 0);
    wr24(&d[OFF_MEASURE], rec.lengthRaw);

    // Temps: prefer the record's, fall back to the material's recommended.
    d[OFF_NOZ_MIN] = rec.extruderMinC ? rec.extruderMinC : mi.nmin;
    d[OFF_NOZ_MAX] = rec.extruderMaxC ? rec.extruderMaxC : mi.nmax;
    d[OFF_DRY_TEMP] = mi.dtemp;
    d[OFF_DRY_TIME] = mi.dtime;
    d[OFF_BED_MIN]  = rec.bedMinC ? rec.bedMinC : mi.bmin;
    d[OFF_BED_MAX]  = rec.bedMaxC ? rec.bedMaxC : mi.bmax;

    // Timestamp: seconds since 2000-01-01. If no RTC, 0 is acceptable (Init-like).
    uint32_t now = (uint32_t)time(nullptr);
    uint32_t ts  = (now > EPOCH_2000) ? (now - EPOCH_2000) : 0;
    wr32(&d[OFF_TIMESTAMP], ts);

    // Custom message: if the brand was unknown (generic), carry its real name so
    // it is not lost; otherwise leave any record note. Truncated to 26 bytes.
    String msg;
    if (genericBrand && rec.brand[0]) msg = String(rec.brand);
    if (msg.length()) {
        uint8_t n = msg.length() > MSG_MAX ? MSG_MAX : msg.length();
        memcpy(&d[OFF_MESSAGE], msg.c_str(), n);
    }

    pagesToWrite = 20;            // pages 4-23
    return USER_BASE + TT_DATA_BYTES;
}

// ---- writePages ------------------------------------------------------------
uint8_t TigerTagFormat::writePages(uint8_t /*pagesToWrite*/,
                                   uint8_t* out, uint8_t maxPages) const {
    // Contiguous data pages 4..23.
    uint8_t n = 0;
    for (uint8_t p = 4; p <= 23 && n < maxPages; p++) out[n++] = p;
    return n;
}
