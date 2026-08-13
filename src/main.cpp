/**
 * @file    main.cpp
 * @brief   Filament Tag Reader / Writer - top-level wiring and event dispatcher.
 *
 * Architecture (event-driven, FreeRTOS task separation):
 *
 *   [RFIDReader task] --reads & decodes a tag--> pushes AppEvent
 *        |                                              |
 *        v                                              v
 *   FilamentTag store (mutex)                     appEventQueue
 *                                                        |
 *                                                        v
 *                                          [dispatcher task in main]
 *                                          - updates OLED
 *                                          - drives LED/buzzer cues
 *                                          - logs to /webserial + Serial
 *
 *   [WebService task]  serves /, /api/filament, /status, /update, /webserial
 *   [WiFiManager task] portal (AP) OR STA reconnect housekeeping
 *   [Feedback task]    non-blocking LED/buzzer timing
 *
 * Nothing here blocks: every wait is vTaskDelay. The C6 drops USB serial on
 * reboot, so all startup progress is mirrored to the OLED and (once WiFi is up)
 * to /webserial and telnet.
 */

#include <Arduino.h>
#include "Pins.h"
#include "BoardConfig.h"
#include "Events.h"
#include "FilamentTag.h"
#include "FilamentDB.h"
#include "TagFormatRegistry.h"
#include "AnycubicFormat.h"
#include "OpenSpoolFormat.h"
#include "TigerTagDB.h"
#include "TigerTagFormat.h"
#include "RFIDReader.h"
#include "Display.h"
#include "FilamentScreen.h"
#include "Feedback.h"
#include "WiFiManager.h"
#include "WebService.h"
#include "WebHandlers.h"
#include "DeviceSettings.h"
#include "SplashScreen.h"
#include "AWSBitmap.h"      // project-specific boot logo data (src/)

// ---- Globals ---------------------------------------------------------------
QueueHandle_t appEventQueue = nullptr;   // defined here (declared extern in Events.h)

// Subsystems.
// Display takes panel geometry; the I2C bus is the caller's to start (see
// setup()), so the panel can share it and get board-specific bus setup.
static Display       display(OLED_WIDTH, OLED_HEIGHT, -1);
static Feedback      feedback(LED_R_PIN, LED_G_PIN, LED_B_PIN, BUZZER_PIN,
                              RGB_LED_ACTIVE_HIGH);
static RFIDReader    reader(RC522_SS_PIN, RC522_RST_PIN,
                            RC522_SCK_PIN, RC522_MISO_PIN, RC522_MOSI_PIN);
static WiFiManager   wifi("FilamentReader-Setup");   // captive-portal AP SSID
static WebService    web(80);
static DeviceSettings settings;                       // NVS-backed settings store
static FilamentDB    filamentDb;                      // JSON-backed filament tables

// Pluggable tag formats. Stage 1 registers only Anycubic, so behavior is
// unchanged; OpenSpool joins the registry in the next stage.
static TagFormatRegistry tagFormats;
static AnycubicFormat    anycubicFormat;
static OpenSpoolFormat   openSpoolFormat;
static TigerTagDB        tigerTagDb;                  // TigerTag ID lookup tables
static TigerTagFormat    tigerTagFormat(&tigerTagDb);

static const char* HOSTNAME = "filament";          // http://filament.local

// ---- Dispatcher: consumes appEventQueue ------------------------------------
static void dispatcherTask(void* pv) {
    AppEvent ev;
    for (;;) {
        // Block until an event arrives (no busy-wait, no delay()).
        if (xQueueReceive(appEventQueue, &ev, portMAX_DELAY) != pdTRUE) continue;

        switch (ev.type) {
            case EVT_FILAMENT_READ: {
                FilamentRecord rec = FilamentTag::storeGet();
                feedback.cueSuccess();
                if (display.isReady())
                    FilamentScreen::show(display.raw(), rec, reader.lastReadFormat());
                web.log("[evt] filament read OK uid=" + String(ev.filament.uid) +
                        " sku=" + String(rec.sku) +
                        " type=" + String(rec.type) +
                        " color=" + String(rec.colorHex));
                break;
            }
            case EVT_TAG_ERROR: {
                feedback.cueError();
                if (display.isReady())
                    FilamentScreen::error(display.raw(), String(ev.filament.uid),
                                          "code " + String(ev.filament.errorCode));
                web.log("[evt] tag error uid=" + String(ev.filament.uid) +
                        " code=" + String(ev.filament.errorCode));
                break;
            }
            case EVT_TAG_WRITTEN: {
                // Blue + double beep = good verified write.
                feedback.cueWrite();
                display.showStatus("Write OK", "Tag written +",
                                   "verified", reader.writeFormatName());
                web.log("[evt] tag WRITE verified uid=" + String(ev.filament.uid) +
                        " fmt=" + reader.writeFormatName());
                break;
            }
            case EVT_WRITE_ERROR: {
                // Red + long beep = bad write/verify.
                feedback.cueError();
                if (display.isReady())
                    FilamentScreen::error(display.raw(),
                                          String(ev.filament.uid), "write failed");
                web.log("[evt] tag WRITE failed uid=" + String(ev.filament.uid));
                break;
            }
            case EVT_CARD_TAP:
            default:
                break;   // not used in this project
        }
    }
}

// ---- WiFi status callbacks (mirror to OLED for the headless C6) -------------
static void onWifiConnected() {
    // Use the saved hostname if the user set one in /setup; else the default.
    String host = settings.hostname(HOSTNAME);
    web.setHostname(host);
    registerWebHandlers(web.routes(), reader, filamentDb, tigerTagDb);   // project routes BEFORE begin()
    // Enable the reusable /setup page (splash hold, diagnostics hold, board,
    // hostname). This project adds no project-specific setup fields yet; when it
    // does, call web.setSetupFieldsProvider()/setSetupSaveHandler() here.
    web.enableSetup(&settings);

    // Add a persistent read/write mode toggle to the /setup page. The dashboard
    // also has a runtime toggle; this one is the saved default applied on boot.
    web.setSetupFieldsProvider([](String& html) {
        bool wm = settings.getBool("mode_write", false);
        html += "<label>Default mode"
                "<select name=\"mode_write\">";
        html += String("<option value=\"0\"") + (!wm ? " selected" : "") + ">Read</option>";
        html += String("<option value=\"1\"") + ( wm ? " selected" : "") + ">Write</option>";
        html += "</select></label>";

        // Write format selector, built from the registered formats so it stays
        // in sync as formats are added. Reads always auto-detect; this only sets
        // which format new tags are written in.
        String cur = settings.getString("write_format", "anycubic");
        html += "<label>Write format"
                "<select name=\"write_format\">";
        for (auto* f : tagFormats.all()) {
            bool sel = (cur == f->id());
            html += String("<option value=\"") + f->id() + "\"" +
                    (sel ? " selected" : "") + ">" + f->name() + "</option>";
        }
        html += "</select></label>";
    });
    web.setSetupSaveHandler([](WebServer& s) {
        if (s.hasArg("mode_write")) {
            bool wm = (s.arg("mode_write") == "1");
            settings.setBool("mode_write", wm);
            reader.setMode(wm ? RFIDReader::Mode::Write : RFIDReader::Mode::Read);
        }
        if (s.hasArg("write_format")) {
            String wf = s.arg("write_format");
            settings.setString("write_format", wf);
            reader.setWriteFormat(tagFormats.byId(wf.c_str()));
        }
    });
    web.setStatusProvider([](String& body) {
        // Project-specific lines appended to the common /status block.
        body += "Board:    " BOARD_NAME "\n";
        body += "OLED:     " + String(display.isReady() ? "OK" : "FAIL") + "\n";
        body += "WriteFmt: " + settings.getString("write_format", "anycubic") + "\n";
        body += "Reader:   " + String(reader.isReaderPresent() ? "OK" : "OFFLINE") + "\n";
        body += "Reads:    " + String(reader.readCount()) + "\n";
        body += "Last UID: " + reader.lastUid() + "\n";
        body += "Last res: " + String(reader.lastReadOk() ? "OK" : "ERROR") + "\n";
        if (FilamentTag::storeHasData()) {
            FilamentRecord r = FilamentTag::storeGet();
            body += "Filament: " + String(r.brand) + " " + String(r.type) +
                    " " + String(r.colorHex) + "\n";
        }
    });
    // Footer theme: the dashboard uses the green accent (#5fd3a0) with a muted
    // separator, matching the rest of this project's UI. The built-in links
    // (Device status / Device log / Firmware update) are auto-added by the
    // module; this project has no extra pages, so no addFooterLink() is needed.
    web.setFooterColors("#5fd3a0", "#2a3645", "#7fe0b5");
    web.begin();
    if (display.isReady())
        FilamentScreen::waiting(display.raw(), wifi.localIP().toString());
    web.log("[net] connected, web up at http://" + wifi.localIP().toString());
}

static void onProvisioning() {
    display.showStatus("WiFi setup", "Join AP:",
                       "FilamentReader-Setup", "then open 192.168.4.1");
}

// ===========================================================================
void setup() {
    Serial.begin(115200);            // mirrored, but unwatchable on C6 reboot

    // The event queue must exist before any producer/consumer task starts.
    appEventQueue = xQueueCreate(8, sizeof(AppEvent));
    FilamentTag::storeInit();

    // Settings store first, so the saved splash hold (and other settings) are
    // available before we play the boot splash.
    settings.begin("filament");

    // Filament definition tables (JSON in LittleFS, with built-in fallback). A
    // missing or malformed file falls back to a small built-in set, surfaced on
    // /status. Loaded before web routes so /api/materials has data immediately.
    bool dbOk = filamentDb.begin("/filaments.json");
    web.log(String("[db] filament tables ") +
            (dbOk ? "loaded" : "FELL BACK to built-in") +
            (filamentDb.usingFallback() ? " (fallback)" : ""));

    // TigerTag ID lookup tables (brands/materials/aspects -> numeric IDs).
    bool ttOk = tigerTagDb.begin("/tigertag_ids.json");
    web.log(String("[db] tigertag IDs ") +
            (ttOk ? "loaded" : "FELL BACK to built-in") +
            (tigerTagDb.usingFallback() ? " (fallback)" : ""));

    // OLED first: it is our boot diagnostic surface.
    //
    // The caller owns the bus. Bring it up explicitly on the configured pins,
    // slow it down, and let the panel's internal supply settle before pushing
    // the init sequence. Display::begin() then passes periphBegin=false so
    // Adafruit's begin() cannot re-init Wire with no pin arguments and silently
    // move the bus back to the core's variant defaults.
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    Wire.setClock(100000);            // gentle, reliable
    delay(100);                       // let the panel's supply settle
    bool oledOk = display.begin(Wire, OLED_ADDR);

    // ---- Phase 1: animated boot splash, held for the configured time -------
    // The splash animates once, then stays visible for splashHoldSec. The hold
    // here uses vTaskDelay (not delay()), so it never blocks other cores/tasks;
    // at this point in setup() no app tasks depend on the screen yet.
    uint16_t splashSec = settings.splashHoldSec(5);   // default 5 s
    uint16_t diagSec   = settings.diagHoldSec(3);     // default 3 s
    if (oledOk) {
        SplashScreen splash(display.raw(), AWS_LOGO, AWS_LOGO_W, AWS_LOGO_H,
                            AWS_LOGO_REST_Y);
        splash.playAnimation();                       // runs once, before tasks
        if (splashSec) vTaskDelay(pdMS_TO_TICKS((uint32_t)splashSec * 1000UL));
    }

    // Phase 2 begins below: the per-subsystem boot diagnostics are drawn as
    // each subsystem comes up, then held for diagSec before live data. We bring
    // the subsystems up first (so the final diagnostics screen is complete),
    // then apply the diagnostics hold just before starting WiFi/live UI.

    // Feedback (LED/buzzer) up early so cues work immediately.
    feedback.begin();
    feedback.cueScan();              // blue blip = alive

    // RFID reader. Logger routes through WebService::log so lines also reach
    // /webserial + telnet (once WiFi is up); before that they go to Serial.
    reader.setLogger([](const String& s) { web.log(s); });

    // Register known tag formats and wire them into the reader. Reads
    // auto-detect among all registered formats; the write format is chosen on
    // /setup (default Anycubic).
    tagFormats.add(&anycubicFormat);
    tagFormats.add(&openSpoolFormat);
    tagFormats.add(&tigerTagFormat);
    reader.setFormatRegistry(&tagFormats);
    {
        String wf = settings.getString("write_format", "anycubic");
        reader.setWriteFormat(tagFormats.byId(wf.c_str()));
    }

    bool readerOk = reader.begin(appEventQueue);

    // Restore the saved read/write mode (set on /setup). Default is read.
    bool writeMode = settings.getBool("mode_write", false);
    reader.setMode(writeMode ? RFIDReader::Mode::Write : RFIDReader::Mode::Read);

    // ---- Phase 2: boot diagnostics, held for the configured time -----------
    // Final per-subsystem status screen. Held for diagSec so it is readable on
    // the headless C6 (USB serial is gone by now), then Phase 3 (live data via
    // onWifiConnected -> showWaiting) replaces it.
    display.showStatus("Booting...",
                       String("Board: ") + BOARD_NAME,
                       String("OLED: ") + (oledOk ? "OK" : "FAIL"),
                       String("RC522: ") + (readerOk ? "OK" : "FAIL"));
    if (diagSec) vTaskDelay(pdMS_TO_TICKS((uint32_t)diagSec * 1000UL));

    // Event dispatcher.
    xTaskCreate(dispatcherTask, "dispatch", 4096, nullptr, 1, nullptr);

    // WiFi: register callbacks, then begin(). begin() either connects (STA) and
    // fires onConnected, or starts the provisioning AP and fires onProvisioning.
    wifi.setHostname(HOSTNAME);
    wifi.onConnected(onWifiConnected);
    wifi.onProvisioningStarted(onProvisioning);
    wifi.begin();
}

void loop() {
    // Only WiFiManager housekeeping lives here; everything else is task-driven.
    wifi.loop();
    vTaskDelay(pdMS_TO_TICKS(50));   // yield; never delay()
}
