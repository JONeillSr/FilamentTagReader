/**
 * @file    WebHandlers.cpp
 * @brief   Project routes: dashboard page + /api/filament JSON.
 *
 * The JSON endpoint serves the last decoded FilamentRecord from FilamentTag's
 * mutex-guarded store, plus live reader status read directly from the
 * RFIDReader module's thread-safe accessors. Kept deliberately small: the heavy
 * lifting (decode, storage) lives in the reusable modules.
 */

#include "WebHandlers.h"
#include "HtmlPages.h"
#include "FilamentTag.h"
#include "FilamentDB.h"
#include <ArduinoJson.h>

void registerWebHandlers(WebServer& server, RFIDReader& reader, FilamentDB& db,
                         TigerTagDB& ttdb) {
    // ---- Dashboard ---------------------------------------------------------
    server.on("/", [&server]() {
        server.send_P(200, "text/html", DASHBOARD_HTML);
    });

    // ---- Filament JSON -----------------------------------------------------
    // Capture the reader by reference so we can query its live status through
    // the module's guarded accessors (no duplicated global state).
    server.on("/api/filament", [&server, &reader, &db]() {
        JsonDocument doc;
        doc["readerOk"]  = reader.isReaderPresent();
        doc["readCount"] = reader.readCount();
        doc["uptime"]    = millis();
        // Surface the current mode + whether a write is queued, so the dashboard
        // can show the right panel without a second request.
        doc["mode"]       = (reader.mode() == RFIDReader::Mode::Write) ? "write" : "read";
        doc["writeFormat"] = reader.writeFormatName();
        doc["pending"]    = reader.hasPendingWrite();
        doc["dbFallback"] = db.usingFallback();

        bool has = FilamentTag::storeHasData();
        doc["hasData"] = has;

        if (has) {
            FilamentRecord rec = FilamentTag::storeGet();
            JsonObject f = doc["filament"].to<JsonObject>();
            f["uid"]          = rec.uid;
            f["format"]       = reader.lastReadFormat();
            f["sku"]          = rec.sku;
            f["brand"]        = rec.brand;
            f["type"]         = rec.type;
            f["colorHex"]     = rec.colorHex;
            f["colorA"]       = rec.colorA;
            f["colorR"]       = rec.colorR;
            f["colorG"]       = rec.colorG;
            f["colorB"]       = rec.colorB;
            f["extruderMinC"] = rec.extruderMinC;
            f["extruderMaxC"] = rec.extruderMaxC;
            f["bedMinC"]      = rec.bedMinC;
            f["bedMaxC"]      = rec.bedMaxC;
            f["diameterMm"]   = rec.diameterMm;
            f["diameterRaw"]  = rec.diameterRaw;
            f["lengthRaw"]    = rec.lengthRaw;
            f["readMillis"]   = rec.readMillis;
        }

        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    // ---- Materials list (for the write form selectors) ---------------------
    // Serves the material names and weight labels from the filament DB so the
    // dashboard form is always in sync with the (editable) table.
    server.on("/api/materials", [&server, &db]() {
        JsonDocument doc;
        doc["fallback"] = db.usingFallback();
        JsonArray mats = doc["materials"].to<JsonArray>();
        for (const auto& m : db.materials()) {
            JsonObject o = mats.add<JsonObject>();
            o["name"]   = m.name;
            o["sku"]    = m.sku;
            o["brand"]  = m.brand;
            o["extMin"] = m.extMin; o["extMax"] = m.extMax;
            o["bedMin"] = m.bedMin; o["bedMax"] = m.bedMax;
        }
        JsonArray wts = doc["weights"].to<JsonArray>();
        for (const auto& w : db.weights()) {
            JsonObject o = wts.add<JsonObject>();
            o["label"]  = w.label;
            o["length"] = w.length;
        }
        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    // ---- Mode get/set ------------------------------------------------------
    server.on("/api/mode", HTTP_GET, [&server, &reader]() {
        JsonDocument doc;
        doc["mode"] = (reader.mode() == RFIDReader::Mode::Write) ? "write" : "read";
        String out; serializeJson(doc, out);
        server.send(200, "application/json", out);
    });
    server.on("/api/mode", HTTP_POST, [&server, &reader]() {
        if (!server.hasArg("mode")) { server.send(400, "text/plain", "missing mode"); return; }
        String m = server.arg("mode");
        reader.setMode(m == "write" ? RFIDReader::Mode::Write
                                    : RFIDReader::Mode::Read);
        if (m != "write") reader.clearPendingWrite();   // leaving write clears queue
        server.send(200, "text/plain", "OK");
    });

    // ---- Queue a write -----------------------------------------------------
    // Builds a FilamentRecord from the chosen material/color/weight (consulting
    // the DB for temps/SKU/brand) and queues it on the reader. The actual write
    // happens on the next tap, in the reader task.
    server.on("/api/write", HTTP_POST, [&server, &reader, &db]() {
        if (!server.hasArg("type") || !server.hasArg("color") ||
            !server.hasArg("weight")) {
            server.send(400, "text/plain", "need type, color, weight");
            return;
        }
        String type   = server.arg("type");
        String color  = server.arg("color");    // "#RRGGBB"
        String weight = server.arg("weight");

        const FilamentDB::Material* mat = db.findMaterial(type);
        if (!mat) { server.send(404, "text/plain", "unknown material"); return; }

        FilamentRecord rec{};
        strncpy(rec.type,  type.c_str(),       sizeof(rec.type) - 1);
        strncpy(rec.sku,   mat->sku.c_str(),   sizeof(rec.sku) - 1);
        strncpy(rec.brand, mat->brand.c_str(), sizeof(rec.brand) - 1);
        rec.extruderMinC = mat->extMin; rec.extruderMaxC = mat->extMax;
        rec.bedMinC      = mat->bedMin; rec.bedMaxC      = mat->bedMax;

        // Parse "#RRGGBB" -> components; tag stores ABGR with A=0xFF opaque.
        long rgb = strtol(color.c_str() + (color[0] == '#' ? 1 : 0), nullptr, 16);
        rec.colorR = (rgb >> 16) & 0xFF;
        rec.colorG = (rgb >> 8)  & 0xFF;
        rec.colorB = (rgb)       & 0xFF;
        rec.colorA = 0xFF;
        snprintf(rec.colorHex, sizeof(rec.colorHex), "#%06lX", rgb & 0xFFFFFF);

        rec.diameterRaw = 175;                       // 1.75 mm
        rec.lengthRaw   = db.lengthForWeight(weight); // weight -> length
        rec.valid = true;

        reader.setPendingWrite(rec);
        // Ensure we are in write mode so the next tap performs the write.
        reader.setMode(RFIDReader::Mode::Write);

        server.send(200, "text/plain", "queued");
    });

    // ---- Filament DB JSON editor ------------------------------------------
    // GET serves the raw JSON for editing; POST validates+persists it (the DB
    // rejects malformed input and keeps the previous data). The editor page
    // itself is served at /filaments.
    server.on("/filaments", [&server]() {
        server.send_P(200, "text/html", FILAMENTS_HTML);
    });
    server.on("/api/db", HTTP_GET, [&server, &db]() {
        server.send(200, "application/json", db.rawJson());
    });
    server.on("/api/db", HTTP_POST, [&server, &db]() {
        // The editor posts the full JSON body as the "json" arg.
        if (!server.hasArg("json")) { server.send(400, "text/plain", "missing json"); return; }
        if (db.saveJson(server.arg("json"))) {
            server.send(200, "text/plain", "saved");
        } else {
            server.send(422, "text/plain", "invalid JSON - not saved (previous kept)");
        }
    });

    // ---- TigerTag ID database editor --------------------------------------
    // Same validated-save pattern as the filament table, for tigertag_ids.json
    // (brand/material/aspect -> numeric IDs + the local-naming aliases).
    server.on("/tigertag", [&server]() {
        server.send_P(200, "text/html", TIGERTAG_HTML);
    });
    server.on("/api/ttdb", HTTP_GET, [&server, &ttdb]() {
        server.send(200, "application/json", ttdb.rawJson());
    });
    server.on("/api/ttdb", HTTP_POST, [&server, &ttdb]() {
        if (!server.hasArg("json")) { server.send(400, "text/plain", "missing json"); return; }
        if (ttdb.saveJson(server.arg("json"))) {
            server.send(200, "text/plain", "saved");
        } else {
            server.send(422, "text/plain", "invalid JSON - not saved (previous kept)");
        }
    });
}
