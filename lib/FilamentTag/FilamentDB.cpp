/**
 * @file    FilamentDB.cpp
 * @brief   Implementation of the JSON-backed filament database.
 */

#include "FilamentDB.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// A minimal built-in default used when the JSON file is missing or invalid, so
// writes still work (with the most common materials) and the device is never
// bricked by a bad edit. Kept small on purpose.
static const char* BUILTIN_JSON = R"JSON(
{
  "brand_default": "AC",
  "weights": { "1 KG": 330, "750 G": 247, "500 G": 165, "250 G": 82 },
  "materials": {
    "PLA":      { "extMin":190,"extMax":230,"bedMin":50,"bedMax":60,"sku":"AHPLBK-101","brand":"AC" },
    "PLA Silk": { "extMin":200,"extMax":230,"bedMin":55,"bedMax":65,"sku":"AHSCWH-102","brand":"AC" },
    "PETG":     { "extMin":230,"extMax":250,"bedMin":70,"bedMax":90,"sku":"","brand":"AC" },
    "ABS":      { "extMin":220,"extMax":250,"bedMin":90,"bedMax":100,"sku":"SHABBK-102","brand":"AC" }
  }
}
)JSON";

bool FilamentDB::begin(const char* path) {
    _fallback = false;
    if (!LittleFS.begin(true)) {       // format-on-fail so a fresh device works
        loadBuiltinFallback();
        return false;
    }
    File f = LittleFS.open(path, "r");
    if (!f) {                          // no file yet -> seed it from the builtin
        _fallback = false;
        if (parseInto(BUILTIN_JSON)) {
            // Persist the builtin so the web editor has something to show/edit.
            File w = LittleFS.open(path, "w");
            if (w) { w.print(BUILTIN_JSON); w.close(); }
            _raw = BUILTIN_JSON;
            return true;
        }
        loadBuiltinFallback();
        return false;
    }
    String json = f.readString();
    f.close();
    if (!parseInto(json)) {            // file present but malformed
        loadBuiltinFallback();
        return false;
    }
    _raw = json;
    return true;
}

bool FilamentDB::parseInto(const String& json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;

    // Validate the shape before committing: must have a materials object with at
    // least one entry, and a weights object.
    JsonObjectConst mats = doc["materials"].as<JsonObjectConst>();
    JsonObjectConst wts  = doc["weights"].as<JsonObjectConst>();
    if (mats.isNull() || mats.size() == 0) return false;

    std::vector<Material> newMats;
    std::vector<Weight>   newWts;
    String brandDef = doc["brand_default"] | "AC";

    for (JsonPairConst kv : mats) {
        Material m;
        m.name   = kv.key().c_str();
        JsonObjectConst o = kv.value().as<JsonObjectConst>();
        m.extMin = o["extMin"] | 0;
        m.extMax = o["extMax"] | 0;
        m.bedMin = o["bedMin"] | 0;
        m.bedMax = o["bedMax"] | 0;
        m.sku    = o["sku"]   | "";
        m.brand  = o["brand"] | brandDef;
        newMats.push_back(m);
    }
    if (!wts.isNull()) {
        for (JsonPairConst kv : wts) {
            Weight w;
            w.label  = kv.key().c_str();
            w.length = kv.value().as<uint16_t>();
            newWts.push_back(w);
        }
    }

    // Commit only after a fully successful parse (so a bad edit never half-loads).
    _materials    = std::move(newMats);
    _weights      = std::move(newWts);
    _brandDefault = brandDef;
    _fallback     = false;
    return true;
}

void FilamentDB::loadBuiltinFallback() {
    _fallback = true;
    _raw      = BUILTIN_JSON;
    parseInto(BUILTIN_JSON);   // populate vectors from the builtin
    _fallback = true;          // parseInto clears it; we are still on fallback
}

bool FilamentDB::saveJson(const String& json, const char* path) {
    // Validate by parsing into temporaries first; only persist if it is good.
    std::vector<Material> savedMats = _materials;   // keep current as backup
    std::vector<Weight>   savedWts  = _weights;
    String                savedBrand = _brandDefault;

    if (!parseInto(json)) {
        // Restore the backup (parseInto leaves vectors untouched on failure, but
        // be explicit for safety) and report failure.
        _materials = savedMats; _weights = savedWts; _brandDefault = savedBrand;
        return false;
    }
    File w = LittleFS.open(path, "w");
    if (!w) return false;
    w.print(json);
    w.close();
    _raw = json;
    return true;
}

const FilamentDB::Material* FilamentDB::findMaterial(const String& name) const {
    for (const auto& m : _materials) if (m.name == name) return &m;
    return nullptr;
}

uint16_t FilamentDB::lengthForWeight(const String& label, uint16_t def) const {
    for (const auto& w : _weights) if (w.label == label) return w.length;
    return def;
}
