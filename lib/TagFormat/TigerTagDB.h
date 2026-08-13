/**
 * @file    TigerTagDB.h
 * @brief   TigerTag numeric-ID lookup tables (brands, materials, aspects).
 *
 * TigerTag stores material/brand/aspect as numeric IDs from a published
 * database, not as text. This module loads those ID tables from a JSON file in
 * LittleFS (web-editable) and resolves a FilamentRecord's text names to IDs when
 * writing, and IDs back to names when reading.
 *
 * It also carries two alias layers so a local inventory's naming maps onto the
 * official TigerTag names:
 *   - material_aliases:  "PLA|High Speed" -> "PLA High Speed"  (material variants)
 *   - aspect_aliases:    "Matte" -> "Matt"                     (spelling/finish)
 *
 * Unknown brands resolve to the Generic brand id; the real brand name can then
 * be carried in the tag's 26-byte custom-message field instead.
 *
 * JSON shape (see data/tigertag_ids.json):
 *   { "generic_brand_id":65535, "aspect_none_id":255,
 *     "brands":   { "Polymaker":50604, ... },
 *     "materials":{ "PLA":{ "id":38219,"nmin":190,... }, ... },
 *     "aspects":  { "Silk":92, ... },
 *     "material_aliases":{ "PLA|High Speed":"PLA High Speed", ... },
 *     "aspect_aliases":  { "Matte":"Matt", ... } }
 */

#pragma once
#include <Arduino.h>
#include <vector>

class TigerTagDB {
public:
    struct MaterialInfo {
        uint16_t id   = 0;
        uint16_t nmin = 0, nmax = 0;   // recommended nozzle temps
        uint16_t bmin = 0, bmax = 0;   // recommended bed temps
        uint16_t dtemp = 0, dtime = 0; // dry temp / time
        bool     found = false;
    };

    // Load the ID table from LittleFS. Returns false (and sets usingFallback) if
    // the file is missing/invalid; a tiny built-in set is used so writes of the
    // most common materials still work.
    bool begin(const char* path = "/tigertag_ids.json");
    bool   usingFallback() const { return _fallback; }
    String rawJson()       const { return _raw; }
    bool   saveJson(const String& json, const char* path = "/tigertag_ids.json");

    uint16_t genericBrandId() const { return _genericBrand; }
    uint16_t aspectNoneId()   const { return _aspectNone; }

    // ---- Resolution (write path): names -> IDs -----------------------------
    // Resolve a material + finish (your material_type) to a TigerTag material ID
    // and an aspect ID, applying the alias layers. If the combo maps to a material
    // variant (e.g. PLA + High Speed -> "PLA High Speed"), aspect becomes None.
    void resolveMaterial(const String& material, const String& finish,
                         MaterialInfo& matOut, uint16_t& aspectIdOut) const;

    // Resolve a brand name to a TigerTag brand ID; returns genericBrandId() and
    // sets isGeneric=true if the brand is not in the table.
    uint16_t resolveBrand(const String& brand, bool& isGeneric) const;

    // ---- Reverse (read path): IDs -> names ---------------------------------
    String brandName(uint16_t id)    const;   // "" if unknown
    String materialName(uint16_t id) const;   // "" if unknown
    String aspectName(uint16_t id)   const;   // "" if unknown

private:
    // Stored as parallel key/value vectors to keep it simple and RAM-light.
    struct KV   { String k; uint16_t v; };
    struct MKV  { String k; MaterialInfo info; };
    struct SKV  { String k; String v; };

    std::vector<KV>  _brands;
    std::vector<MKV> _materials;
    std::vector<KV>  _aspects;
    std::vector<SKV> _matAliases;
    std::vector<SKV> _aspAliases;

    uint16_t _genericBrand = 65535;
    uint16_t _aspectNone   = 255;
    String   _raw;
    bool     _fallback     = false;

    bool parseInto(const String& json);
    void loadBuiltinFallback();
    const MaterialInfo* findMaterialByLabel(const String& label) const;
    String resolveMatAlias(const String& material, const String& finish) const;
    String resolveAspAlias(const String& finish) const;
};
