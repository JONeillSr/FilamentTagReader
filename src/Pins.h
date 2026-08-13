/**
 * @file    Pins.h
 * @brief   Project peripheral pin map for the Filament Tag Reader / Writer.
 *
 * This header answers "where is each peripheral wired for THIS project?" — the
 * RC522 SPI bus, the active buzzer, and the common-cathode tri-color LED. It
 * deliberately holds no board-identity facts; those come from BoardConfig.h
 * (I2C defaults, native-USB flag, strapping warnings). We include BoardConfig
 * here so the OLED can fall back to the board's known-good I2C pins.
 *
 * Pin choices deliberately avoid the strapping / flash / USB GPIOs documented
 * in BoardConfig.h for each board. Where a board has tight GPIO budget (C3),
 * pins are chosen from the remaining safe set.
 *
 * The OLED is a 0.96" SSD1306 two-color unit at I2C address 0x3C, sharing the
 * single Wire bus (Wire1 is avoided per the C6 core bug).
 *
 * Exactly one BOARD_* flag must be defined in platformio.ini (BoardConfig.h
 * enforces this with an #error).
 */

#ifndef PINS_H
#define PINS_H

#include "BoardConfig.h"

// ---- OLED (SSD1306, 0x3C, shared Wire bus) ---------------------------------
// Use the board's vetted default I2C pins from BoardConfig.h. All I2C devices
// share one bus on these pins (single-bus rule from the C6 Wire1 bug).
#define OLED_SDA_PIN   BOARD_I2C_SDA_DEFAULT
#define OLED_SCL_PIN   BOARD_I2C_SCL_DEFAULT
#define OLED_ADDR      0x3C
#define OLED_WIDTH     128
#define OLED_HEIGHT    64

// ============================================================================
//  Per-board peripheral assignments
//  RC522 uses hardware SPI (SCK/MISO/MOSI) + SS + RST. Buzzer is a single
//  digital output. RGB LED is three digital outputs (common cathode => drive
//  HIGH to light each leg).
// ============================================================================

#if defined(BOARD_XIAO_ESP32C6)
    // XIAO C6: SPI on the labeled SPI header. GPIO1 is a plain GPIO here (not a
    // strapping, flash, or USB pin per BoardConfig.h), so it is safe as a
    // general-purpose output. Avoid the strapping pins 4, 5, 8, 9, 15.
    #define RC522_SCK_PIN    19   // XIAO "SCK"
    #define RC522_MISO_PIN   20   // XIAO "MISO"
    #define RC522_MOSI_PIN   18   // XIAO "MOSI"
    #define RC522_SS_PIN     17   // XIAO "SS" / D7
    #define RC522_RST_PIN    16   // XIAO D6
    #define BUZZER_PIN        1    // plain GPIO (not strapping/flash/USB)
    #define LED_R_PIN         2
    #define LED_G_PIN         21
    #define LED_B_PIN         23   // NOTE: shares default SCL slot conceptually;
                                   // OLED SCL default is 23 -> see override below

// ============================================================================
#elif defined(BOARD_ESP32C3)
    // C3 has a tight safe-GPIO budget. Flash: 12-17. USB-JTAG: 18,19.
    // Strapping: 2,8,9. Remaining usable: 0,1,3,4,5,6,7,10,20,21.
    #define RC522_SCK_PIN    4
    #define RC522_MISO_PIN   5
    #define RC522_MOSI_PIN   6
    #define RC522_SS_PIN     7
    #define RC522_RST_PIN    10
    #define BUZZER_PIN        3
    #define LED_R_PIN         0
    #define LED_G_PIN         1
    #define LED_B_PIN         20

// ============================================================================
#elif defined(BOARD_ESP32S3)
    // S3 is GPIO-rich. Avoid strapping 0,3,45,46; flash/PSRAM 26-37; USB 19,20.
    #define RC522_SCK_PIN    12
    #define RC522_MISO_PIN   13
    #define RC522_MOSI_PIN   11
    #define RC522_SS_PIN     10
    #define RC522_RST_PIN    14
    #define BUZZER_PIN        4
    #define LED_R_PIN         5
    #define LED_G_PIN         6
    #define LED_B_PIN         7

// ============================================================================
#elif defined(BOARD_ESP32DEV)
    // Classic ESP32. Avoid strapping 0,2,5,12,15; flash 6-11; input-only 34-39.
    // GPIO17 is the correct SS choice (GPIO12 strapping would block boot).
    #define RC522_SCK_PIN    18   // VSPI SCK
    #define RC522_MISO_PIN   19   // VSPI MISO
    #define RC522_MOSI_PIN   23   // VSPI MOSI
    #define RC522_SS_PIN     17   // safe SS (NOT 5/12)
    #define RC522_RST_PIN     4   // plain GPIO, safe as a reset output
    #define BUZZER_PIN       13
    #define LED_R_PIN        25
    #define LED_G_PIN        26
    #define LED_B_PIN        27

#else
    #error "Pins.h: no known BOARD_* defined. Set one in platformio.ini build_flags."
#endif

// ---- RGB LED polarity ------------------------------------------------------
// Common-cathode LED: each color leg lights when driven HIGH. If you wire a
// common-anode part instead, flip this to 0 and the LED helper inverts.
#define RGB_LED_ACTIVE_HIGH   1

// ---- C6 OLED/LED_B conflict guard ------------------------------------------
// On the XIAO C6, the OLED default SCL is GPIO23. If you also use GPIO23 for
// the LED blue leg you will get a conflict. Move the blue leg off 23 here.
#if defined(BOARD_XIAO_ESP32C6)
  #undef  LED_B_PIN
  #define LED_B_PIN   0    // relocate blue leg off the I2C SCL pin
#endif

#endif  // PINS_H
