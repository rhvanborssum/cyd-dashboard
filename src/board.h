#pragma once
// Differences between the supported boards. The PlatformIO environment picks
// one: `cyd` (default) or `e32r40t`. Pins for the display itself are build
// flags in platformio.ini, since TFT_eSPI reads them from there.

#if defined(BOARD_E32R40T)
// LCDwiki / HOYSOND 4.0" ESP32-32E (E32R40T): ST7796S 480x320,
// XPT2046 resistive touch sharing the display's SPI bus (CS 33).
#define BOARD_NAME "ESP32-32E 4.0in"
#define OTA_MANIFEST_NAME "manifest-e32r40t.json"
#define SCREEN_W 480
#define SCREEN_H 320
#define TOUCH_ON_TFT_BUS 1
// Raw XPT2046 readings -> screen. Starting values; refine from the logged taps.
#define TOUCH_SWAP_XY 1
#define TOUCH_X_MIN 300
#define TOUCH_X_MAX 3800
#define TOUCH_Y_MIN 300
#define TOUCH_Y_MAX 3800
#define AUDIO_ENABLE_PIN 4  // speaker amp enable, active low: held off

#else
// ESP32-2432S028R "Cheap Yellow Display": 320x240, XPT2046 on its own SPI bus.
#define BOARD_NAME "CYD 2.8in"
#define OTA_MANIFEST_NAME "manifest.json"
#define SCREEN_W 320
#define SCREEN_H 240
#define TOUCH_ON_TFT_BUS 0
#define TOUCH_SWAP_XY 0
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3700
#define TOUCH_Y_MIN 240
#define TOUCH_Y_MAX 3800
#endif

// Everything is laid out for 320x240; a bigger screen shows it centred.
#define UI_W 320
#define UI_H 240
#define UI_X ((SCREEN_W - UI_W) / 2)
#define UI_Y ((SCREEN_H - UI_H) / 2)
