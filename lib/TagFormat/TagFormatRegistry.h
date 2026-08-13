/**
 * @file    TagFormatRegistry.h
 * @brief   Small registry of known TagFormats with lookup by id and by
 *          auto-detection against a raw tag buffer.
 *
 * The registry owns the set of formats the firmware understands. The reader uses
 * detectFormat() on read (try each format's detect()) and formatById() on write
 * (the format chosen on /setup). Adding a new standard = register one more
 * TagFormat here; nothing else changes.
 */

#pragma once
#include "TagFormat.h"
#include <vector>

class TagFormatRegistry {
public:
    // Register a format (pointer must outlive the registry; in practice these
    // are file-static singletons in main).
    void add(TagFormat* fmt) { _formats.push_back(fmt); }

    const std::vector<TagFormat*>& all() const { return _formats; }

    // Look up a format by its stable id (e.g. "anycubic"). Returns the first
    // registered format if not found, so writes always have a valid target.
    TagFormat* byId(const char* id) const {
        for (auto* f : _formats) if (strcmp(f->id(), id) == 0) return f;
        return _formats.empty() ? nullptr : _formats.front();
    }

    // Try each format's detector against a raw buffer; return the first match,
    // or nullptr if none recognise it.
    TagFormat* detect(const uint8_t* buf, uint16_t len) const {
        for (auto* f : _formats) if (f->detect(buf, len)) return f;
        return nullptr;
    }

    // The largest pagesToRead() across all formats, so the reader reads enough
    // for any of them before detecting.
    uint8_t maxPagesToRead() const {
        uint8_t m = 0;
        for (auto* f : _formats) if (f->pagesToRead() > m) m = f->pagesToRead();
        return m;
    }

private:
    std::vector<TagFormat*> _formats;
};
