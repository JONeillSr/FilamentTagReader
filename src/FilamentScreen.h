/**
 * @file    FilamentScreen.h
 * @brief   Project-specific OLED screens for this filament reader.
 *
 * These are intentionally NOT part of the reusable Display library. show()
 * depends on FilamentRecord (the project's tag codec); waiting() and error()
 * carry filament wording ("Scan tag", "Place spool on reader", "Tag err") that
 * has no meaning in another project. Display depends only on String +
 * Adafruit_SSD1306, which is what lets it drop into any ESP32 project unchanged.
 *
 * They draw straight onto the shared panel via Display::raw(), reusing the same
 * initialised SSD1306 instance Display owns.
 *
 * show() layout (128x64, two-color panel):
 *   y0   type (size 2 if <=10 chars, else size 1 so long names don't wrap)
 *   y16  Fmt: <format>     (the detected/!written standard; blank if none)
 *   y24  brand
 *   y32  color hex
 *   y40  nozzle temp
 *   y48  bed temp
 *   y56  diameter
 */

#pragma once
#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include "FilamentTag.h"      // FilamentRecord

namespace FilamentScreen {

/// Idle prompt shown while waiting for a tag.
inline void waiting(Adafruit_SSD1306& oled, const String& ipLine) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(2);
    oled.setCursor(0, 8);
    oled.println("Scan tag");
    oled.setTextSize(1);
    oled.setCursor(0, 40);
    oled.println("Place spool on reader");
    oled.setCursor(0, 54);
    oled.println(ipLine);
    oled.display();
}

/// Tag present but unreadable, undecodable, or failed to write.
inline void error(Adafruit_SSD1306& oled, const String& uid,
                  const String& reason) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(2);
    oled.setCursor(0, 4);
    oled.println("Tag err");
    oled.setTextSize(1);
    oled.setCursor(0, 36);
    oled.println("UID:" + uid);
    oled.setCursor(0, 48);
    oled.println(reason);
    oled.display();
}

// Render a decoded filament record onto the given panel. `format` is the tag
// standard name (e.g. "Anycubic"); pass "" to omit that line.
inline void show(Adafruit_SSD1306& oled,
                 const FilamentRecord& rec,
                 const String& format = "") {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);

    // Line 1 (y=0): type. Large when it fits (<=10 chars at size 2), otherwise
    // size 1 so a long multi-word type ("PLA High Speed Marble") stays on one
    // line instead of wrapping and pushing the layout down. No color swatch --
    // a two-color panel can't show the filament color.
    String type = String(rec.type);
    if (type.length() <= 10) {
        oled.setTextSize(2);
        oled.setCursor(0, 0);
        oled.println(type);
    } else {
        oled.setTextSize(1);
        oled.setCursor(0, 4);
        if (type.length() > 21) type = type.substring(0, 21);   // defensive
        oled.println(type);
    }

    // Remaining lines at text size 1 (~21 chars wide), each on its own line so
    // nothing wraps, even with long brand names.
    oled.setTextSize(1);

    // y=16: Format (relevant to every tag standard, unlike the Anycubic-only
    // SKU). Omitted if not supplied.
    oled.setCursor(0, 16);
    if (format.length())
        oled.println("Fmt: " + format);

    // y=24: Brand.
    oled.setCursor(0, 24);
    oled.println(String(rec.brand));

    // y=32: Color hex (e.g. #00FF00).
    oled.setCursor(0, 32);
    oled.println(String(rec.colorHex));

    // y=40: Nozzle temp.
    oled.setCursor(0, 40);
    oled.println("Noz " + String(rec.extruderMinC) + "-" +
                 String(rec.extruderMaxC) + "C");

    // y=48: Bed temp.
    oled.setCursor(0, 48);
    oled.println("Bed " + String(rec.bedMinC) + "-" +
                 String(rec.bedMaxC) + "C");

    // y=56: Diameter.
    oled.setCursor(0, 56);
    oled.println("Dia " + String(rec.diameterMm, 2) + "mm");

    oled.display();
}

}  // namespace FilamentScreen
