/**
 * @file    AnycubicFormat.h
 * @brief   TagFormat implementation for the Anycubic binary page layout.
 *
 * This is a thin adapter over the existing FilamentTag::decode()/encode() codec
 * (raw NTAG pages, little-endian fields, magic byte 0x7B at page 4). It adds no
 * new parsing logic -- it just presents the working Anycubic codec through the
 * common TagFormat interface so the reader can treat all formats uniformly.
 */

#pragma once
#include "TagFormat.h"

class AnycubicFormat : public TagFormat {
public:
    const char* id()   const override { return "anycubic"; }
    const char* name() const override { return "Anycubic"; }

    // The Anycubic layout uses pages up to 31, so read the full 32-page window.
    uint8_t pagesToRead() const override { return FT_PAGES_TO_READ; }

    // Structural check: the format's header magic 0x7B sits at page 4 (byte 16).
    bool detect(const uint8_t* buf, uint16_t len) const override {
        if (len < 17) return false;
        return buf[16] == 0x7B;
    }

    ReadStatus decode(const uint8_t* buf, uint16_t len,
                      const char* uidHex,
                      FilamentRecord& out) const override {
        return FilamentTag::decode(buf, len, uidHex, out);
    }

    uint16_t encode(const FilamentRecord& rec,
                    uint8_t* buf, uint16_t bufLen,
                    uint8_t& pagesToWrite) const override {
        if (!FilamentTag::encode(rec, buf, bufLen)) { pagesToWrite = 0; return 0; }
        // The Anycubic codec lays out a fixed 32-page buffer; the reader writes
        // the specific pages this format defines (see writePages()).
        pagesToWrite = FT_PAGES_TO_READ;
        return FT_BUFFER_BYTES;
    }

    // Anycubic writes a specific scattered set of pages (header, SKU, brand,
    // type, color, temps, diameter/length, trailer) -- not a contiguous run.
    uint8_t writePages(uint8_t /*pagesToWrite*/,
                       uint8_t* out, uint8_t maxPages) const override {
        static const uint8_t kPages[] = {
            4, 5, 6, 7, 8, 10, 11, 12, 13, 15, 16, 17, 18, 20, 24, 29, 30, 31
        };
        uint8_t n = 0;
        for (uint8_t i = 0; i < sizeof(kPages) && n < maxPages; i++)
            out[n++] = kPages[i];
        return n;
    }
};
