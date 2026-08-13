/**
 * @file    TigerTagFormat.h
 * @brief   TagFormat implementation for the TigerTag standard (NTAG213).
 *
 * TigerTag stores a fixed binary structure (big-endian) in pages 4-23 of an
 * NTAG213 (80 bytes of data), with optional ECDSA signature in pages 24-39 that
 * this implementation does NOT read or write (basic read/write scope only).
 *
 * Material/brand/aspect are numeric IDs from the TigerTag database, resolved via
 * TigerTagDB. The neutral FilamentRecord carries text names; this codec maps
 * them to/from IDs on write/read.
 *
 * Implemented purely from the public TigerTag specification (not their code) to
 * keep the host project's MIT licensing clean. "TigerTag" is referenced only as
 * a supported format name; no TigerTag branding/logo is used.
 *
 * Data layout (offsets within user memory, page 4 = offset 0; big-endian):
 *   0  ID TigerTag(4)  4 ID Product(4)  8 Material(2) 10 Diameter(1)
 *   11 Aspect1(1) 12 Aspect2(1) 13 Type(1) 14 Brand(2) 16 Unit(1)
 *   17 Color1 RGBA(4) 21 Color2 RGB(3) 24 Color3 RGB(3) 27 TD(2)
 *   29 Measure(3) 32 NozzleMin(1) 33 NozzleMax(1) 34 DryTemp(1) 35 DryTime(1)
 *   36 BedMin(1) 37 BedMax(1) 38 Timestamp(4) 42 Reserved(12) 54 Message(26)
 *   -> 80 bytes total (pages 4-23). Pages 24-39 (signature) untouched.
 */

#pragma once
#include "TagFormat.h"

class TigerTagDB;   // forward

class TigerTagFormat : public TagFormat {
public:
    explicit TigerTagFormat(TigerTagDB* db) : _db(db) {}

    const char* id()   const override { return "tigertag"; }
    const char* name() const override { return "TigerTag"; }

    // Data lives in pages 4-23 (20 pages). Read that window (we skip signature
    // pages 24-39). 4 + 20 = 24 pages.
    uint8_t pagesToRead() const override { return 24; }

    bool detect(const uint8_t* buf, uint16_t len) const override;

    ReadStatus decode(const uint8_t* buf, uint16_t len,
                      const char* uidHex,
                      FilamentRecord& out) const override;

    uint16_t encode(const FilamentRecord& rec,
                    uint8_t* buf, uint16_t bufLen,
                    uint8_t& pagesToWrite) const override;

    // TigerTag writes a contiguous run of data pages 4-23.
    uint8_t writePages(uint8_t pagesToWrite,
                       uint8_t* out, uint8_t maxPages) const override;

    // Known TigerTag version IDs (the "ID TigerTag" field), used for detection.
    static const uint32_t TT_VERSION_OFFLINE = 0x5BF59264; // TigerTag 100% Offline
    static const uint32_t TT_VERSION_PLUS    = 0xBC0FCB97; // TigerTag+
    static const uint32_t TT_VERSION_INIT    = 0x6C46A3C1; // TigerTag Init

private:
    TigerTagDB* _db;

    static uint32_t rd32(const uint8_t* p);
    static uint16_t rd16(const uint8_t* p);
    static void     wr32(uint8_t* p, uint32_t v);
    static void     wr16(uint8_t* p, uint16_t v);
    static void     wr24(uint8_t* p, uint32_t v);
    static uint32_t rd24(const uint8_t* p);
};
