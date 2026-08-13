/**
 * @file    OpenSpoolFormat.cpp
 * @brief   NDEF/JSON codec for OpenSpool tags.
 *
 * NDEF-on-NTAG layout we read/write (user memory, starting at page 4 / byte 16):
 *
 *   [ TLV: 0x03 ][ length ][ NDEF message ........ ][ TLV terminator: 0xFE ]
 *
 * The NDEF message is a single record:
 *
 *   [ header byte ][ type length ][ payload length ][ type "application/json" ]
 *   [ payload (the JSON text) ]
 *
 * For our short payloads (< 256 bytes) we use the NDEF "short record" (SR) form
 * with a single-byte payload length. Header byte = 0xD2:
 *   MB=1 ME=1 CF=0 SR=1 IL=0 TNF=0x02(MIME)  -> 1101 0010b = 0xD2
 */

#include "OpenSpoolFormat.h"
#include <ArduinoJson.h>
#include <string.h>

static const char* JSON_MIME = "application/json";

// ---- helpers ---------------------------------------------------------------

bool OpenSpoolFormat::isKnownBrand(const char* brand) {
    // Per the OpenSpool spec; unknown brands are treated as "Generic".
    static const char* known[] = {
        "Generic", "Overture", "PolyLite", "eSun", "PolyTerra"
    };
    for (auto* k : known) if (strcasecmp(brand, k) == 0) return true;
    return false;
}

// Locate the NDEF-message TLV (tag 0x03) in the user-memory buffer. NTAG user
// memory begins at page 4 (byte offset 16). A capability-container or lock TLVs
// may precede it; we scan TLV-by-TLV from the start of user memory.
bool OpenSpoolFormat::findNdefMessage(const uint8_t* buf, uint16_t len,
                                      uint16_t& msgOff, uint16_t& msgLen) {
    uint16_t i = 16;                 // start of user memory (page 4)
    while (i < len) {
        uint8_t tlvType = buf[i];
        if (tlvType == 0x00) { i++; continue; }      // NULL TLV, skip
        if (tlvType == 0xFE) return false;           // terminator, no NDEF
        if (i + 1 >= len) return false;
        // Length field: 1 byte, or 0xFF + 2 bytes (we only need 1-byte here).
        uint16_t l;
        uint16_t valOff;
        if (buf[i + 1] == 0xFF) {
            if (i + 3 >= len) return false;
            l = (buf[i + 2] << 8) | buf[i + 3];
            valOff = i + 4;
        } else {
            l = buf[i + 1];
            valOff = i + 2;
        }
        if (tlvType == 0x03) {                        // NDEF-message TLV
            if (valOff + l > len) l = len - valOff;   // clamp to what we read
            msgOff = valOff;
            msgLen = l;
            return true;
        }
        i = valOff + l;                               // skip other TLVs
    }
    return false;
}

// From an NDEF message, pull the payload of the first record if it is a MIME
// "application/json" record. Handles the short-record (SR) form.
bool OpenSpoolFormat::extractJsonPayload(const uint8_t* msg, uint16_t msgLen,
                                         const uint8_t*& json, uint16_t& jsonLen) {
    if (msgLen < 3) return false;
    uint16_t p = 0;
    uint8_t  hdr = msg[p++];
    bool sr = hdr & 0x10;                  // short record?
    bool il = hdr & 0x08;                  // ID length present?
    uint8_t typeLen = msg[p++];
    uint32_t payLen;
    if (sr) {
        payLen = msg[p++];
    } else {
        if (p + 4 > msgLen) return false;
        payLen = ((uint32_t)msg[p] << 24) | ((uint32_t)msg[p + 1] << 16) |
                 ((uint32_t)msg[p + 2] << 8) | msg[p + 3];
        p += 4;
    }
    uint8_t idLen = 0;
    if (il) { if (p >= msgLen) return false; idLen = msg[p++]; }
    // Type field.
    if (p + typeLen > msgLen) return false;
    bool isJson = (typeLen == strlen(JSON_MIME)) &&
                  (memcmp(&msg[p], JSON_MIME, typeLen) == 0);
    p += typeLen;
    p += idLen;                            // skip ID if present
    if (!isJson) return false;
    if (p + payLen > msgLen) payLen = msgLen - p;   // clamp
    json    = &msg[p];
    jsonLen = (uint16_t)payLen;
    return true;
}

// ---- detect ----------------------------------------------------------------

bool OpenSpoolFormat::detect(const uint8_t* buf, uint16_t len) const {
    uint16_t msgOff, msgLen;
    if (!findNdefMessage(buf, len, msgOff, msgLen)) return false;
    const uint8_t* json; uint16_t jsonLen;
    if (!extractJsonPayload(&buf[msgOff], msgLen, json, jsonLen)) return false;
    // Confirm it is actually an OpenSpool payload (not some other JSON record).
    // Cheap substring check avoids a full parse during detection.
    for (uint16_t i = 0; i + 9 <= jsonLen; i++) {
        if (memcmp(&json[i], "openspool", 9) == 0) return true;
    }
    return false;
}

// ---- decode ----------------------------------------------------------------

ReadStatus OpenSpoolFormat::decode(const uint8_t* buf, uint16_t len,
                                   const char* uidHex,
                                   FilamentRecord& out) const {
    out = FilamentRecord{};
    out.valid = false;
    if (uidHex) strncpy(out.uid, uidHex, sizeof(out.uid) - 1);

    uint16_t msgOff, msgLen;
    if (!findNdefMessage(buf, len, msgOff, msgLen)) return ReadStatus::BAD_HEADER;
    const uint8_t* json; uint16_t jsonLen;
    if (!extractJsonPayload(&buf[msgOff], msgLen, json, jsonLen))
        return ReadStatus::BAD_HEADER;

    JsonDocument doc;
    if (deserializeJson(doc, json, jsonLen)) return ReadStatus::READ_FAILED;

    const char* type  = doc["type"]      | "";
    const char* color = doc["color_hex"] | "";
    const char* brand = doc["brand"]     | "Generic";
    // OpenSpool temps are strings in the spec; accept string or number.
    uint16_t minT = 0, maxT = 0;
    if (doc["min_temp"].is<const char*>()) minT = atoi(doc["min_temp"].as<const char*>());
    else                                   minT = doc["min_temp"] | 0;
    if (doc["max_temp"].is<const char*>()) maxT = atoi(doc["max_temp"].as<const char*>());
    else                                   maxT = doc["max_temp"] | 0;

    strncpy(out.type,  type,  sizeof(out.type) - 1);
    strncpy(out.brand, isKnownBrand(brand) ? brand : "Generic", sizeof(out.brand) - 1);

    // color_hex is "RRGGBB"; fill both the hex string and the components.
    if (strlen(color) >= 6) {
        char c[7]; strncpy(c, color, 6); c[6] = '\0';
        long rgb = strtol(c, nullptr, 16);
        out.colorR = (rgb >> 16) & 0xFF;
        out.colorG = (rgb >> 8)  & 0xFF;
        out.colorB =  rgb        & 0xFF;
        out.colorA = 0xFF;
        snprintf(out.colorHex, sizeof(out.colorHex), "#%06lX", rgb & 0xFFFFFF);
    }

    // OpenSpool carries a single temp range; map it to the extruder/nozzle
    // fields. Bed temps are not part of the spec, so leave them zero.
    out.extruderMinC = minT;
    out.extruderMaxC = maxT;
    out.diameterRaw  = 175;          // OpenSpool implies 1.75 mm
    out.diameterMm   = 1.75f;

    out.valid = (out.type[0] != '\0');
    return out.valid ? ReadStatus::OK : ReadStatus::EMPTY_FIELDS;
}

// ---- encode ----------------------------------------------------------------

uint16_t OpenSpoolFormat::encode(const FilamentRecord& rec,
                                 uint8_t* buf, uint16_t bufLen,
                                 uint8_t& pagesToWrite) const {
    pagesToWrite = 0;
    if (!buf || bufLen < 32) return 0;
    if (rec.type[0] == '\0') return 0;

    // Build the JSON payload. color_hex is 6 hex digits (no leading '#').
    char colorHex[7];
    snprintf(colorHex, sizeof(colorHex), "%02X%02X%02X",
             rec.colorR, rec.colorG, rec.colorB);
    const char* brand = isKnownBrand(rec.brand) ? rec.brand : "Generic";

    JsonDocument doc;
    doc["protocol"]  = "openspool";
    doc["version"]   = "1.0";
    doc["type"]      = rec.type;
    doc["color_hex"] = colorHex;
    doc["brand"]     = brand;
    // Temps as strings, per the spec's example.
    char mn[8], mx[8];
    snprintf(mn, sizeof(mn), "%u", rec.extruderMinC);
    snprintf(mx, sizeof(mx), "%u", rec.extruderMaxC);
    doc["min_temp"]  = mn;
    doc["max_temp"]  = mx;

    char jsonBuf[224];
    size_t jsonLen = serializeJson(doc, jsonBuf, sizeof(jsonBuf));
    if (jsonLen == 0 || jsonLen > 200) return 0;     // keep within NTAG215 room

    // ---- Frame as NDEF, wrapped in the NTAG NDEF-message TLV ----------------
    // We assume a short record (payload < 256). Build into a temp, then lay it
    // into the user-memory area of `buf` starting at byte 16 (page 4).
    const uint8_t typeLen = (uint8_t)strlen(JSON_MIME);
    // NDEF record = hdr + typeLen + payLen + type + payload
    uint16_t recLen = 1 + 1 + 1 + typeLen + (uint16_t)jsonLen;

    uint16_t need = 16 /*pages 0-3 untouched region in buffer*/
                  + 2 /*TLV tag+len*/ + recLen + 1 /*terminator*/;
    if (need > bufLen) return 0;

    // Zero the user-memory region we will write (so trailing bytes are clean).
    memset(&buf[16], 0, bufLen - 16);

    uint16_t o = 16;
    buf[o++] = 0x03;                      // NDEF-message TLV tag
    buf[o++] = (uint8_t)recLen;           // TLV length (short form)
    // NDEF record header: MB|ME|SR|TNF(MIME=0x02) = 0xD2
    buf[o++] = 0xD2;
    buf[o++] = typeLen;                   // type length
    buf[o++] = (uint8_t)jsonLen;          // payload length (short record)
    memcpy(&buf[o], JSON_MIME, typeLen); o += typeLen;
    memcpy(&buf[o], jsonBuf, jsonLen);    o += jsonLen;
    buf[o++] = 0xFE;                      // TLV terminator

    // Round the written length up to whole 4-byte pages.
    uint16_t bytes = o;
    uint16_t pages = (bytes + 3) / 4;
    pagesToWrite = (uint8_t)pages;
    return bytes;
}
