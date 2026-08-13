/**
 * @file    TigerTagDB.cpp
 * @brief   Implementation of the TigerTag ID lookup tables.
 */

#include "TigerTagDB.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// Minimal built-in fallback (the most common materials/brands) so writes still
// work if the JSON file is missing or invalid. Kept tiny on purpose.
static const char* TT_BUILTIN = R"JSON(
{
  "generic_brand_id":65535, "aspect_none_id":255,
  "brands":{ "Generic":65535, "Polymaker":50604, "Bambu Lab":35123, "eSun":48026, "Overture":46203, "SUNLU":51857 },
  "materials":{
    "PLA":{"id":38219,"nmin":190,"nmax":230,"bmin":50,"bmax":60,"dtemp":45,"dtime":8},
    "PETG":{"id":38256,"nmin":230,"nmax":250,"bmin":70,"bmax":90,"dtemp":65,"dtime":8},
    "ABS":{"id":20562,"nmin":220,"nmax":250,"bmin":90,"bmax":100,"dtemp":80,"dtime":8},
    "TPU":{"id":43518,"nmin":210,"nmax":230,"bmin":40,"bmax":60,"dtemp":50,"dtime":8}
  },
  "aspects":{ "None":255, "Basic":104, "Silk":92, "Matt":247, "Glitter":64 },
  "material_aliases":{ "PLA|Silk":"PLA" },
  "aspect_aliases":{ "Matte":"Matt" }
}
)JSON";

bool TigerTagDB::begin(const char* path) {
    _fallback = false;
    if (!LittleFS.begin(true)) { loadBuiltinFallback(); return false; }
    File f = LittleFS.open(path, "r");
    if (!f) {
        // Seed the file from the builtin so the web editor has content.
        if (parseInto(TT_BUILTIN)) {
            File w = LittleFS.open(path, "w");
            if (w) { w.print(TT_BUILTIN); w.close(); }
            _raw = TT_BUILTIN;
            return true;
        }
        loadBuiltinFallback();
        return false;
    }
    String json = f.readString();
    f.close();
    if (!parseInto(json)) { loadBuiltinFallback(); return false; }
    _raw = json;
    return true;
}

bool TigerTagDB::parseInto(const String& json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;

    JsonObjectConst brands = doc["brands"].as<JsonObjectConst>();
    JsonObjectConst mats   = doc["materials"].as<JsonObjectConst>();
    if (brands.isNull() || mats.isNull()) return false;

    std::vector<KV>  nb;
    std::vector<MKV> nm;
    std::vector<KV>  na;
    std::vector<SKV> nma, naa;

    for (JsonPairConst kv : brands)
        nb.push_back({ kv.key().c_str(), (uint16_t)(kv.value().as<uint32_t>() & 0xFFFF) });

    for (JsonPairConst kv : mats) {
        MaterialInfo mi;
        JsonObjectConst o = kv.value().as<JsonObjectConst>();
        mi.id    = (uint16_t)(o["id"].as<uint32_t>() & 0xFFFF);
        mi.nmin  = o["nmin"] | 0;  mi.nmax = o["nmax"] | 0;
        mi.bmin  = o["bmin"] | 0;  mi.bmax = o["bmax"] | 0;
        mi.dtemp = o["dtemp"] | 0; mi.dtime = o["dtime"] | 0;
        mi.found = true;
        nm.push_back({ kv.key().c_str(), mi });
    }

    JsonObjectConst aspects = doc["aspects"].as<JsonObjectConst>();
    if (!aspects.isNull())
        for (JsonPairConst kv : aspects)
            na.push_back({ kv.key().c_str(), (uint16_t)(kv.value().as<uint32_t>() & 0xFFFF) });

    JsonObjectConst ma = doc["material_aliases"].as<JsonObjectConst>();
    if (!ma.isNull())
        for (JsonPairConst kv : ma)
            nma.push_back({ kv.key().c_str(), kv.value().as<const char*>() });

    JsonObjectConst aa = doc["aspect_aliases"].as<JsonObjectConst>();
    if (!aa.isNull())
        for (JsonPairConst kv : aa)
            naa.push_back({ kv.key().c_str(), kv.value().as<const char*>() });

    // Commit on full success.
    _brands       = std::move(nb);
    _materials    = std::move(nm);
    _aspects      = std::move(na);
    _matAliases   = std::move(nma);
    _aspAliases   = std::move(naa);
    _genericBrand = doc["generic_brand_id"] | 65535;
    _aspectNone   = doc["aspect_none_id"]   | 255;
    _fallback     = false;
    return true;
}

void TigerTagDB::loadBuiltinFallback() {
    _fallback = true;
    _raw      = TT_BUILTIN;
    parseInto(TT_BUILTIN);
    _fallback = true;        // parseInto clears it; we are still on fallback
}

bool TigerTagDB::saveJson(const String& json, const char* path) {
    // Validate by parsing; only persist if valid (keep current on failure).
    auto b=_brands; auto m=_materials; auto a=_aspects; auto ma=_matAliases; auto aa=_aspAliases;
    uint16_t gb=_genericBrand, an=_aspectNone;
    if (!parseInto(json)) {
        _brands=b;_materials=m;_aspects=a;_matAliases=ma;_aspAliases=aa;
        _genericBrand=gb;_aspectNone=an;
        return false;
    }
    File w = LittleFS.open(path, "w");
    if (!w) return false;
    w.print(json); w.close();
    _raw = json;
    return true;
}

// ---- lookups ---------------------------------------------------------------

const TigerTagDB::MaterialInfo* TigerTagDB::findMaterialByLabel(const String& label) const {
    for (const auto& m : _materials)
        if (m.k.equalsIgnoreCase(label)) return &m.info;
    return nullptr;
}

String TigerTagDB::resolveMatAlias(const String& material, const String& finish) const {
    // Try "material|finish" first, then "material|*".
    String key = material + "|" + finish;
    for (const auto& a : _matAliases) if (a.k.equalsIgnoreCase(key)) return a.v;
    String wild = material + "|*";
    for (const auto& a : _matAliases) if (a.k.equalsIgnoreCase(wild)) return a.v;
    return "";
}

String TigerTagDB::resolveAspAlias(const String& finish) const {
    for (const auto& a : _aspAliases) if (a.k.equalsIgnoreCase(finish)) return a.v;
    return finish;   // no alias -> use as-is
}

void TigerTagDB::resolveMaterial(const String& material, const String& finish,
                                 MaterialInfo& matOut, uint16_t& aspectIdOut) const {
    aspectIdOut = _aspectNone;

    // 1) Material-variant alias (combo collapses finish into the material).
    String variant = resolveMatAlias(material, finish);
    if (variant.length()) {
        const MaterialInfo* mi = findMaterialByLabel(variant);
        if (mi) { matOut = *mi; aspectIdOut = _aspectNone; return; }
    }

    // 2) Base material + aspect (with aspect alias for spelling/closeness).
    const MaterialInfo* base = findMaterialByLabel(material);
    if (!base) {
        // Last resort: try material with "+" stripped (e.g. "PLA+/Pro" -> "PLA+").
        int slash = material.indexOf('/');
        if (slash > 0) base = findMaterialByLabel(material.substring(0, slash));
    }
    if (base) matOut = *base;
    else      matOut = MaterialInfo{};     // not found; id stays 0

    if (finish.length()) {
        String aspName = resolveAspAlias(finish);
        for (const auto& a : _aspects)
            if (a.k.equalsIgnoreCase(aspName)) { aspectIdOut = a.v; break; }
    }
}

uint16_t TigerTagDB::resolveBrand(const String& brand, bool& isGeneric) const {
    for (const auto& b : _brands)
        if (b.k.equalsIgnoreCase(brand)) { isGeneric = (b.v == _genericBrand); return b.v; }
    isGeneric = true;
    return _genericBrand;
}

String TigerTagDB::brandName(uint16_t id) const {
    for (const auto& b : _brands) if (b.v == id) return b.k;
    return "";
}
String TigerTagDB::materialName(uint16_t id) const {
    for (const auto& m : _materials) if (m.info.id == id) return m.k;
    return "";
}
String TigerTagDB::aspectName(uint16_t id) const {
    for (const auto& a : _aspects) if (a.v == id) return a.k;
    return "";
}
