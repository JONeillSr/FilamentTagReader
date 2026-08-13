#pragma once
#include <WebServer.h>
#include "RFIDReader.h"
#include "FilamentDB.h"
#include "TigerTagDB.h"

/**
 * @file    WebHandlers.h
 * @brief   Project-specific web routes for the Filament Tag Reader / Writer.
 *
 * Registers the dashboard ("/") and the JSON data endpoint ("/api/filament")
 * on the WebServer that WebService owns. Call after creating WebService but
 * BEFORE WebService::begin(), passing webSvc.routes().
 *
 * The reader is passed by reference so the JSON endpoint can read live reader
 * status (present flag, read count) straight from the module's thread-safe
 * accessors, rather than via duplicated global mirror variables.
 */
void registerWebHandlers(WebServer& server, RFIDReader& reader, FilamentDB& db,
                         TigerTagDB& ttdb);
