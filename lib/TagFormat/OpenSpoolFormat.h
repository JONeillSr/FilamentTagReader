/**
 * @file    OpenSpoolFormat.h
 * @brief   TagFormat implementation for the OpenSpool standard.
 *
 * OpenSpool stores a single NDEF record of MIME type "application/json" on an
 * NTAG215/216, whose payload is a small JSON object:
 *
 *   { "protocol":"openspool", "version":"1.0", "type":"PLA",
 *     "color_hex":"FFAABB", "brand":"Generic",
 *     "min_temp":"220", "max_temp":"240" }
 *
 * Unlike the Anycubic format (fixed binary pages), this is a text payload inside
 * NDEF framing, so this class handles:
 *   - the NTAG NDEF TLV container ( 0x03 <len> <ndef-message> 0xFE ),
 *   - a single NDEF record with TNF=0x02 (MIME), type "application/json",
 *   - JSON encode/parse of the OpenSpool fields,
 * translating to/from the shared FilamentRecord.
 *
 * Writes are constrained to OpenSpool's known brand/type values (the reader's
 * write form should already restrict these); on decode, unknown brands map to
 * "Generic" per the spec.
 */

#pragma once
#include "TagFormat.h"

class OpenSpoolFormat : public TagFormat {
public:
    const char* id()   const override { return "openspool"; }
    const char* name() const override { return "OpenSpool"; }

    // NDEF data starts at page 4 (user memory). Read a generous window so the
    // whole JSON record is captured on NTAG215/216 (a minimal record is well
    // under 200 bytes). 56 pages * 4 = 224 bytes of user memory.
    uint8_t pagesToRead() const override { return 4 + 56; }

    bool detect(const uint8_t* buf, uint16_t len) const override;

    ReadStatus decode(const uint8_t* buf, uint16_t len,
                      const char* uidHex,
                      FilamentRecord& out) const override;

    uint16_t encode(const FilamentRecord& rec,
                    uint8_t* buf, uint16_t bufLen,
                    uint8_t& pagesToWrite) const override;

    // OpenSpool's recognized brand values (others decode/encode as "Generic").
    static bool isKnownBrand(const char* brand);

private:
    // Find the NDEF message TLV in the user-memory area; returns the offset of
    // the message payload and its length, or false if not found.
    static bool findNdefMessage(const uint8_t* buf, uint16_t len,
                                uint16_t& msgOff, uint16_t& msgLen);
    // Extract the application/json payload from the first NDEF record.
    static bool extractJsonPayload(const uint8_t* msg, uint16_t msgLen,
                                   const uint8_t*& json, uint16_t& jsonLen);
};
