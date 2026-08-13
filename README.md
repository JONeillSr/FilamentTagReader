# Filament Tag Reader / Writer

An ESP32 + MFRC522 (RC522 / HW-126) reader/writer for **3D-printer filament spool
NFC tags**. It reads and writes multiple tag standards &mdash; **Anycubic**,
**OpenSpool**, and **TigerTag** &mdash; auto-detecting the format on read and
writing whichever format you select. Filament data (type, brand, color,
temperatures, diameter, weight) is shown on a 0.96" SSD1306 OLED and a web
dashboard, and new tags are written from a web form with read-back verification.
A common-cathode tri-color LED and active buzzer give immediate read/write
feedback.

Built from a set of self-contained, application-agnostic modules (board config,
event bus, WiFi provisioning, web service, display, RFID, feedback, and a
pluggable tag-format layer) so each one can be lifted into another ESP32 project
unchanged.

---

## Features

- **Multi-format support** &mdash; reads and writes three filament-tag standards
  through one pluggable interface:
  - **Anycubic** &mdash; the native NTAG21x binary page format (SKU, brand, type,
    color, nozzle/bed temps, diameter, length).
  - **OpenSpool** &mdash; an NDEF/JSON record on NTAG215/216 (type, color, brand,
    single temp range); the simplest cross-tool standard.
  - **TigerTag** &mdash; a big-endian binary structure on NTAG213 using numeric
    material/brand/aspect IDs; read by printers such as the Snapmaker U1.
- **Auto-detect on read** &mdash; tap any tag and the device identifies its format
  (Anycubic header magic, OpenSpool NDEF marker, or TigerTag version field) and
  decodes it. No need to pre-select.
- **Format-aware writes** &mdash; pick the write format on `/setup`; the dashboard
  shows the active format. Writes are read back and **byte-verified** before
  reporting success.
- **Read / write mode** toggled live on the dashboard or set as a persistent
  default on `/setup`.
- **Editable lookup tables** in flash, with browser editors (validated before
  saving, with built-in fallbacks): Anycubic filament definitions (`/filaments`)
  and the TigerTag ID database (`/tigertag`).
- **Color converter** on the write panel: enter HEX, RGB, ABGR, or RGBA and the
  others update live &mdash; each hex form matches the byte order its format
  stores (Anycubic ABGR, TigerTag RGBA).
- **Web dashboard** (`/`) showing all fields, with the filament color rendered
  as a colored circle.
- **Status page** (`/status`) with module health, last UID, write format, and
  last result &mdash; themed, with a dashboard link.
- **WebSerial** (`/webserial`) + telnet (port 23) live diagnostic log &mdash;
  essential because the ESP32-C6 drops USB serial on reboot.
- OTA updates (`/update`) via ElegantOTA.
- WiFi captive-portal provisioning (no hardcoded credentials).
- mDNS: reachable at `http://filament.local`.
- Fully event-driven, FreeRTOS task separation, **no blocking `delay()`**.

---

## Supported boards

Build one environment per board (`platformio.ini`):

| Board                  | Env          | Notes                                  |
|------------------------|--------------|----------------------------------------|
| Seeed XIAO ESP32-C6    | `xiao_c6`    | Primary target. Single I2C bus only.   |
| ESP32-C3 devkit        | `esp32c3`    | Tight GPIO budget.                     |
| ESP32-S3 devkitc-1     | `esp32s3`    | GPIO-rich.                             |
| Classic ESP32 DevKit   | `esp32dev`   | Serial survives reboot (watchable).    |

```
pio run -e xiao_c6
pio run -e xiao_c6 -t upload
```

Uses the **pioarduino** fork of the espressif32 platform (required for current
ESP32-C6 / Arduino-ESP32 3.x support).

---

## Hardware wiring

OLED is SSD1306, I2C address `0x3C`, on the board's default I2C pins (shared
`Wire` bus &mdash; `Wire1` is avoided per the C6 core bug #10685). RC522 uses
hardware SPI. RGB LED is common-cathode (each leg lit HIGH); active buzzer
sounds when driven HIGH.

Exact GPIO assignments per board live in `src/Pins.h`, chosen to avoid each
board's strapping / flash / USB pins (documented in `lib/BoardConfig`).

### Classic ESP32 DevKit V1 (first bring-up)

Wiring for the `esp32dev` environment, matching the project wiring diagram:

| Function    | GPIO | Notes                          |
|-------------|------|--------------------------------|
| OLED SDA    | 21   | shared I2C bus                 |
| OLED SCL    | 22   | shared I2C bus                 |
| RC522 SDA/SS| 17   | VSPI CS                        |
| RC522 SCK   | 18   | VSPI SCK                       |
| RC522 MOSI  | 23   | VSPI MOSI                      |
| RC522 MISO  | 19   | VSPI MISO                      |
| RC522 RST   | 4    | plain GPIO                     |
| Buzzer SPK+ | 13   | SPK- to GND                    |
| LED Red     | 25   | common cathode                 |
| LED Green   | 26   | common cathode                 |
| LED Blue    | 27   | common cathode                 |

> The RC522 runs on **3.3V** &mdash; it is not 5V-tolerant. If the version
> register reads erratically under load, add a 100&micro;F cap across the
> module's 3.3V / GND pins to absorb inrush.

### XIAO ESP32-C6

| Function    | GPIO |
|-------------|------|
| OLED SDA    | 22   |
| OLED SCL    | 23   |
| RC522 SCK   | 19   |
| RC522 MISO  | 20   |
| RC522 MOSI  | 18   |
| RC522 SS    | 17   |
| RC522 RST   | 16   |
| Buzzer      | 1    |
| LED R       | 2    |
| LED G       | 21   |
| LED B       | 0    |

> See `src/Pins.h` for the C3 / S3 maps.

---

## Tag formats

The device understands three filament-tag standards through a common
`TagFormat` interface. On **read**, each format's detector is tried against the
raw bytes and the matching one decodes; on **write**, the format selected on
`/setup` is used. All three translate to/from one neutral `FilamentRecord`, so
the dashboard, OLED, and feedback are format-independent.

| Format    | Chip          | Encoding              | Detected by                    |
|-----------|---------------|-----------------------|--------------------------------|
| Anycubic  | NTAG21x       | binary pages, LE      | header magic `0x7B` at page 4  |
| OpenSpool | NTAG215/216   | NDEF + JSON           | NDEF record + `openspool` marker |
| TigerTag  | NTAG213       | binary struct, BE     | version field at page 4        |

### Anycubic

NTAG21x (4 bytes per page). Multi-byte numbers are **little-endian**; strings are
NUL-padded ASCII across their page range.

| Page  | Example      | Field                                         |
|-------|--------------|-----------------------------------------------|
| 4     | `7B 00 65 00`| header (magic `0x7B` / length) &mdash; not decoded |
| 5&ndash;8   | ASCII  | **SKU** &rarr; `AHPLLB-103`                    |
| 10&ndash;13 | ASCII  | **Brand** &rarr; `AC` (Anycubic)              |
| 15&ndash;18 | ASCII  | **Type** &rarr; `PLA`                         |
| 20    | `FF 00 FF 00`| **Color**, ABGR byte order &rarr; `#00FF00`   |
| 24    | `C8 00 D2 00`| **Extruder temp** min/max &rarr; 200&ndash;210 &deg;C |
| 29    | `32 00 3C 00`| **Bed temp** min/max &rarr; 50&ndash;60 &deg;C |
| 30    | `AF 00 4A 01`| **Diameter** (`175`/100 = 1.75 mm), **Length** raw `330` |
| 31    | `E8 03 00 00`| unknown (`1000`) &mdash; not decoded          |

**Color note:** page 20 is decoded as ABGR (bytes A, B, G, R). The example
`FF 00 FF 00` therefore yields R=0, G=255, B=0 = **green `#00FF00`**, *not*
magenta. If real spools show the wrong swatch, the byte order is the first
thing to revisit.

**Length note:** `lengthRaw` (e.g. `330`) is surfaced raw; its exact units
(meters vs a packed value) are unconfirmed and shown as-is.

Writing touches only the pages this format defines (4, 5&ndash;8, 10&ndash;13,
15&ndash;18, 20, 24, 29&ndash;31); lock and configuration pages are left
untouched.

### OpenSpool

A single NDEF record of MIME type `application/json` on NTAG215/216, wrapped in
the NTAG NDEF-message TLV. The payload is human-readable:

```
{ "protocol":"openspool", "version":"1.0", "type":"PLA",
  "color_hex":"FFAABB", "brand":"Generic",
  "min_temp":"220", "max_temp":"240" }
```

Brands are constrained to the OpenSpool set (Generic, Overture, PolyLite, eSun,
PolyTerra); unknown brands write as `Generic`. OpenSpool carries a single
(nozzle) temp range; bed temps are not part of the standard.

### TigerTag

A fixed **big-endian** binary structure on NTAG213, pages 4&ndash;23 (80 bytes);
pages 24&ndash;39 hold an optional ECDSA signature that this firmware does not
read or write (basic read/write scope). Material, brand, and finish ("aspect")
are stored as **numeric IDs** from the TigerTag database rather than text.

| Bytes (from page 4) | Field                                  |
|---------------------|----------------------------------------|
| 0&ndash;3           | ID TigerTag (format/version)           |
| 4&ndash;7           | ID Product (`FFFFFFFF` for Maker tags) |
| 8&ndash;9           | Material ID                            |
| 10                  | Diameter ID (`0x38`=1.75, `0xDD`=2.85) |
| 11&ndash;12         | Aspect 1 / 2 (finish)                  |
| 13                  | Type (`0x8E`=Filament)                 |
| 14&ndash;15         | Brand ID                               |
| 16                  | Unit (`0x15`=g)                        |
| 17&ndash;20         | Color1 RGBA                            |
| 29&ndash;31         | Weight (grams)                         |
| 32&ndash;37         | Nozzle min/max, dry temp/time, bed min/max |
| 38&ndash;41         | Timestamp (also twin-tag pairing ID)   |
| 54&ndash;79         | Custom message (up to 26 bytes)        |

The numeric IDs are resolved via `data/tigertag_ids.json` (the official TigerTag
database, brand/material/aspect &rarr; ID), with an alias layer that maps common
local naming (e.g. `Matte` &rarr; `Matt`, `PLA` + `High Speed` &rarr; the
`PLA High Speed` material) onto the official IDs. Brands not in the database
write as Generic, with the real brand name kept in the custom-message field.

> **Note:** implemented purely from the public TigerTag specification (not their
> GPLv3 code) to keep this project's MIT license clean. "TigerTag" is referenced
> only as a supported format name, per their trademark policy.

All three codecs are **bidirectional**: a tag written by this device reads back
identically, and each `encode()` is the inverse of its `decode()`.

---

## Reading and writing

The device runs in one of two modes, shown in the dashboard header:

- **Read** (default) &mdash; a tap auto-detects the tag's format and shows the
  decoded spool (including which format it was) on the OLED and dashboard.
- **Write** &mdash; the **Write a spool tag** panel appears (with the active
  write format shown as a badge). Pick a material, color, and weight, then
  **Write tag** and tap. The device encodes the record in the selected format,
  writes it, reads it back, and only reports success if every written page
  verifies.

Toggle the mode live from the dashboard, or set a persistent power-on default on
`/setup`. The **write format** (Anycubic / OpenSpool / TigerTag) is also chosen
on `/setup` and shown on `/status`. Material temps and brand are filled in
automatically for the chosen material; color comes from the picker; weight maps
to the format's weight/length field.

### Color converter

The write panel includes a four-way color converter: enter a value as **HEX**,
**RGB**, **ABGR**, or **RGBA** and the others update live; picking a color in the
color picker updates the boxes too. Each hex form uses the byte order its format
stores &mdash; ABGR for Anycubic, RGBA for TigerTag &mdash; so it doubles as a
tool for cross-checking raw tag bytes. **Use** sets the write color from the
converter.

### Lookup tables

Two editable JSON tables live in the flash filesystem:

- **`data/filaments.json`** (editable at **`/filaments`**) &mdash; Anycubic
  material definitions: extruder/bed temps, SKU, brand, and a weight&rarr;length
  map.
- **`data/tigertag_ids.json`** (editable at **`/tigertag`**) &mdash; the TigerTag
  brand/material/aspect ID database plus the local-naming alias layers.

Both have a browser editor (linked from the dashboard header) with the same
validated-save behavior. Edits are written straight to LittleFS, so adding a
brand or fixing a mapping no longer needs a re-flash.

Edits are validated before saving &mdash; malformed JSON is rejected and the
previous table kept &mdash; and small built-in fallback sets are used if a file
is missing or invalid (the fallback state is surfaced on `/status`).

### Read / write feedback

| Outcome        | LED   | Buzzer            |
|----------------|-------|-------------------|
| Good read      | green | two short beeps   |
| Good write     | blue  | two short beeps   |
| Bad read/write | red   | one long beep     |

The OLED "Write OK" screen also shows which format was written.

> A hardware mode button is a planned option; for now the mode is set from the
> web UI.

---

## Architecture

Event-driven with FreeRTOS task separation. In read mode the reader task decodes
a tag and pushes an `AppEvent`; in write mode it writes the queued record and
verifies it, pushing a write-result event. A dispatcher task consumes the queue
and updates the OLED, LED/buzzer, and logs.

```
[RFIDReader task] read mode  --detect+decode--> FilamentTag store (mutex)
                  write mode --encode+write+verify-->
        |          (via the selected/auto-detected TagFormat)
        +-----> appEventQueue ---------+
                       |   EVT_FILAMENT_READ / EVT_TAG_ERROR
                       |   EVT_TAG_WRITTEN  / EVT_WRITE_ERROR
                       v
              [dispatcher task]  -> OLED / LED / buzzer / webserial log

[WebService task]   /  /api/filament  /api/write  /api/materials  /filaments  /tigertag
                    /status  /setup  /update  /webserial
[WiFiManager task]  captive portal (AP) or STA reconnect
[Feedback task]     non-blocking LED + buzzer timing
```

The reader is format-agnostic: a `TagFormatRegistry` holds the known formats,
detects the right one on read, and supplies the selected one on write. Adding a
new standard means writing one more `TagFormat` &mdash; nothing else changes.

### Modules

Reusable (`lib/`, app-agnostic, shared across projects):

| Module        | Responsibility                                            |
|---------------|-----------------------------------------------------------|
| `BoardConfig` | Per-chip facts: I2C defaults, USB/Wire1 flags, strapping. |
| `Events`      | App-wide event bus types + `appEventQueue`.               |
| `WiFiManager` | NVS credentials, captive-portal provisioning, mDNS, optional NTP time sync. |
| `WebService`  | `/status`, `/setup` (incl. a **Reboot device** button), `/update` (OTA), `/webserial`, telnet.|
| `DeviceIdentity`| MAC-derived device ID + mDNS hostname unique per unit + location labels. Shared with RFID_Access; available here but not yet wired in. |
| `Display`     | App-agnostic SSD1306 helper: status/message screens, header band, `raw()`. Caller owns the I2C bus. No project dependencies; identical to RFID_Access's copy. |
| `RFIDReader`  | MFRC522 wrapper + reader task; reads (auto-detect) **and** writes/verifies via the selected format; emits events. |
| `FilamentTag` | Anycubic NTAG codec (`decode`/`encode`) + the neutral `FilamentRecord` + mutex-guarded last record. |
| `FilamentDB`  | JSON-backed Anycubic filament definitions (temps/SKU/brand/weights) with validation + fallback. Lives with `FilamentTag`. |
| `TagFormat`   | Pluggable format layer: `TagFormat` interface, `TagFormatRegistry`, and the `Anycubic` / `OpenSpool` / `TigerTag` implementations + `TigerTagDB` (ID lookups). |
| `DeviceSettings`| NVS-backed settings (splash holds, hostname, default mode, write format). |
| `Feedback`    | Non-blocking RGB LED + active buzzer cues (read/write/error). |

Project-specific (`src/`):

| File              | Responsibility                                        |
|-------------------|-------------------------------------------------------|
| `Pins.h`          | Per-board peripheral pin map (board-guarded).         |
| `FilamentScreen.h`| Project OLED screens — decoded filament record, "Scan tag" idle prompt, tag-error screen; all draw on `Display::raw()` (keeps `Display` app-agnostic). |
| `HtmlPages.h`     | Dashboard (read view + write form + color converter), `/filaments` and `/tigertag` editors (PROGMEM). |
| `WebHandlers.*`   | `/`, `/api/filament`, `/api/write`, `/api/mode`, `/api/materials`, `/api/db` + `/filaments`, `/api/ttdb` + `/tigertag`. |
| `main.cpp`        | Wiring, format registration, and the event dispatcher. |

---

## Diagnostics

Because the **ESP32-C6 drops USB serial on reboot** (boot messages scroll past
before the monitor reconnects), all important logging is mirrored to:

- the **OLED** during startup (each subsystem reports OK/FAIL),
- **`/webserial`** and **telnet :23** once WiFi is up,
- plus `Serial` (watchable on the classic ESP32, which uses an external
  USB-UART).

The **`/status`** and **`/webserial`** pages refresh their content by polling a
small text endpoint (`/status.txt`, `/webserial.txt`) and swapping only the
changing block, rather than reloading the whole page. This keeps the header and
the (separately fetched) footer from flickering on every update, and cuts the
per-refresh payload to a few hundred bytes. The initial content is still
server-rendered into the page, so both work with JavaScript disabled &mdash; they
just stop live-updating.

---

## Build notes

- Uses the **pioarduino** `stable` platform URL, which tracks the latest tested
  ESP32 Arduino 3.x release. If you pin an old tagged version instead and PlatformIO
  reports `MissingPackageManifestError: Could not find one of 'package.json'`,
  the pinned version is stale or its cached download is corrupt: clear it with
  `pio system prune --force` (and delete the stale folder under
  `<core_dir>/platforms/`), then rebuild to re-fetch.
- `board_build.partitions = min_spiffs.csv` &mdash; buys app flash space for
  WiFi + web + OTA. Still leaves ~190&nbsp;KB of filesystem, ample for the lookup
  tables (Anycubic filaments ~2&nbsp;KB + TigerTag ID database ~18&nbsp;KB).
- `board_build.filesystem = littlefs` &mdash; the lookup tables live as JSON
  files on **LittleFS** (more robust than SPIFFS, better power-loss behavior).
  **Upload the filesystem before the firmware** so the tables exist on first
  boot:

  ```
  pio run -e esp32dev -t uploadfs    # uploads data/ (filaments.json + tigertag_ids.json)
  pio run -e esp32dev -t upload      # uploads firmware
  ```

  If the filesystem is never uploaded, each format falls back to a small
  built-in table (and says so on `/status`). Re-run `uploadfs` after editing a
  table file locally.
- `ELEGANTOTA_USE_ASYNC_WEBSERVER=0` &mdash; ElegantOTA runs on the sync
  `WebServer` that `WebService` owns (no ESPAsyncWebServer dependency).
- **Async libraries are intentionally blocked.** This project is sync-only, but
  ElegantOTA's manifest *declares* `AsyncTCP` and `ESPAsyncWebServer` as
  dependencies. If the Library Dependency Finder is allowed to pull them in
  (e.g. under `lib_ldf_mode = deep`/`deep+`), they fail to compile against the
  Arduino-ESP32 3.x core with `fatal error: Network.h` /
  `NetworkInterface.h: No such file or directory`. The build flag alone does
  not prevent this &mdash; it only changes which headers ElegantOTA's own source
  includes, not which libraries the finder compiles. Two settings keep them
  out: `lib_ldf_mode = chain` (does not crawl third-party manifests) and an
  explicit `lib_ignore` for both async libraries. If you ever see those missing
  `Network.h` errors, that async stack got pulled in somewhere &mdash; check
  both settings.

---

## License

MIT &mdash; AWS Solutions LLC dba Azure Innovators.

---

## Changelog

- **v1.3** &mdash; **Shared-library parity with RFID_Access.** Every reusable
  module (`Display`, `WebService`, `WiFiManager`, `DeviceSettings`,
  `SplashScreen`, `DeviceIdentity`) is now byte-identical across both projects.
  - `Display` consolidated onto one implementation: the driver is a member
    rather than a heap-allocated file-static singleton (so `raw()` can no longer
    dereference null, and a project may hold more than one panel), and the
    **caller now owns the I2C bus**. That last change fixes a real latent bug —
    this project previously let Adafruit's `begin()` re-initialise `Wire` with no
    pin arguments, which silently moves the bus back to the core's variant
    defaults. It only worked because the configured pins happened to match.
  - Filament-specific screens (`showWaiting`, `showError`) moved out of
    `Display` into `FilamentScreen`, where the project's other screens already
    live; the unused `showSplash` was removed.
  - `WebService` gained a **Reboot device** button on `/setup` (POST-only) and
    proper attribute escaping, so an apostrophe in a saved value can no longer
    corrupt the settings form.
  - `WiFiManager` gained optional NTP time sync (opt-in; no behavior change
    unless `setTimeSync()` is called).
- **v1.2** &mdash; **Multi-format support.** Added a pluggable `TagFormat` layer
  (`TagFormat` interface + `TagFormatRegistry`) with three formats: **Anycubic**
  (refactored behind the interface, no behavior change), **OpenSpool**
  (NDEF/JSON on NTAG215/216), and **TigerTag** (big-endian binary on NTAG213
  with numeric IDs via `TigerTagDB` + `data/tigertag_ids.json`). Reads
  auto-detect the format; writes use the format chosen on `/setup` (shown on the
  dashboard, `/status`, and the OLED write screen). Added a four-way HEX/RGB/
  ABGR/RGBA color converter to the write panel (synced to the color picker).
  Both lookup tables (`/filaments`, `/tigertag`) have browser editors with
  validated saves straight to LittleFS. Themed `/status` and `/webserial` pages with a dashboard link and shared
  footer, refreshing via content polling (`/status.txt`, `/webserial.txt`) so
  the footer no longer flickers. Read views (dashboard + OLED) show the detected
  format; OLED read layout reworked (per-field lines, no swatch). Renamed the
  device to "Filament Tag Reader". Minor dashboard fixes (last-read hidden in
  write mode, clearer labels, readable mode toggle, "Write tag" button); removed
  the misleading "external" arrow on the footer's Firmware-update link.
  Decoupled `Display` from the filament codec: the record screen moved to a
  project-side `FilamentScreen` helper drawing on `Display::raw()`, so `Display`
  now depends only on Adafruit_SSD1306 and drops into any project unchanged.
- **v1.1** &mdash; Combined reader/**writer**: write spool tags from the
  dashboard (material/color/weight) with read-back verification; live read/write
  mode toggle + persistent default on `/setup`; bidirectional `FilamentTag`
  codec (`encode` added); new `FilamentDB` JSON-backed, web-editable filament
  tables (`/filaments`) with validation and built-in fallback; blue/green/red
  LED + beep cues for write/read/error; migrated filesystem to LittleFS.
- **v1.0** &mdash; Initial: NTAG decode, OLED + web dashboard with color circle,
  LED/buzzer feedback, WebSerial diagnostics, OTA, four board environments.
