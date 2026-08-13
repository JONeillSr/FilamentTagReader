/**
 * @file    FilamentDB.h
 * @brief   Filament definition database (material temps, SKU, brand, weights).
 *
 * Loads filament definitions from a JSON file in the flash filesystem
 * (LittleFS) so they can be edited via the web UI without reflashing. The file
 * is validated on load; if it is missing or malformed, a small built-in default
 * set is used instead and the fallback state is reported (so a bad edit
 * degrades gracefully rather than disabling writes).
 *
 * This lives alongside FilamentTag because the tables are part of the tag codec
 * (the writer consults them to fill in temps/SKU/brand for a chosen material).
 *
 * JSON shape (see data/filaments.json):
 *   {
 *     "brand_default": "AC",
 *     "weights":   { "1 KG": 330, ... },
 *     "materials": { "PLA Silk": { "extMin":200,"extMax":230,
 *                                  "bedMin":55,"bedMax":65,
 *                                  "sku":"AHSCWH-102","brand":"AC" }, ... }
 *   }
 */

#pragma once
#include <Arduino.h>
#include <vector>

class FilamentDB {
public:
    struct Material {
        String   name;
        uint16_t extMin = 0, extMax = 0;
        uint16_t bedMin = 0, bedMax = 0;
        String   sku;
        String   brand;
    };
    struct Weight {
        String   label;     // e.g. "1 KG"
        uint16_t length;    // tag length value, e.g. 330
    };

    // Load from the given path on LittleFS. Returns true if the file loaded and
    // validated; false if the built-in fallback was used instead. Safe to call
    // again to reload after the file is edited via the web UI.
    bool begin(const char* path = "/filaments.json");

    // True if the current data came from the built-in fallback (file missing or
    // invalid). Surfaced on /status so a bad edit is visible.
    bool usingFallback() const { return _fallback; }

    // The raw JSON text currently on disk (for the web editor to display).
    // Returns the built-in default text if no file is present.
    String rawJson() const { return _raw; }

    // Validate + persist new JSON (from the web editor). Returns true and
    // reloads on success; returns false and leaves the current data untouched
    // if the new text is malformed.
    bool saveJson(const String& json, const char* path = "/filaments.json");

    // Lookups used by the writer.
    const std::vector<Material>& materials() const { return _materials; }
    const std::vector<Weight>&   weights()   const { return _weights; }
    const Material* findMaterial(const String& name) const;
    uint16_t        lengthForWeight(const String& label, uint16_t def = 330) const;
    String          brandDefault() const { return _brandDefault; }

private:
    std::vector<Material> _materials;
    std::vector<Weight>   _weights;
    String                _brandDefault = "AC";
    String                _raw;
    bool                  _fallback = false;

    bool parseInto(const String& json);   // parse + validate into the vectors
    void loadBuiltinFallback();           // populate a minimal safe default
};
