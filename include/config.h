#pragma once

#define SYSTEM_AUTHOR "@whoami"
#define SYSTEM_PROJECT "Smolinski|Labs Launcher"
#define FIRMWARE_VERSION "v1.0.8-PROTOTYPE"

#define LGFX_USE_V1
#define LGFX_AUTODETECT

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <SPIFFS.h>
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_task_wdt.h>
#include <vector>
#include <cmath>
#include <cstring>

#define PIN_BTN_TOP        0
#define PIN_BTN_BOTTOM     14
#define PIN_LCD_BL         38
#define PIN_ANTENNA_SW     16
#define PIN_ADV_DETECT     43
#define PIN_GROVE_SDA      18
#define PIN_GROVE_SCL      17

constexpr int SCREEN_W     = 320;
constexpr int SCREEN_H     = 170;
constexpr int TOPBAR_H     = 24;
constexpr int GRID_COLS    = 4;
constexpr int GRID_ROWS    = 2;
constexpr int TILE_GAP     = 8;
constexpr int TILE_W       = 69;
constexpr int TILE_H       = 63;
constexpr int GRID_START_X = 10;
constexpr int GRID_START_Y = TOPBAR_H + 6;

#define MOD_LCTRL      0x01
#define MOD_FN         0x02
#define KEY_UP         0x52
#define KEY_DOWN       0x51
#define KEY_LEFT       0x50
#define KEY_RIGHT      0x4F
#define KEY_ENTER      0x28
#define KEY_ESC        0x29
#define KEY_9          0x26
#define KEY_0          0x27

enum class LangID : uint8_t {
    LANG_EN = 0,
    LANG_DE = 1,
    LANG_RU = 2,
    LANG_PL = 3
};

enum class ClusterMode : uint8_t {
    MODE_STANDALONE_UI = 0,
    MODE_DOCKED_CLUSTER = 1,
    MODE_COMPUTE_OFFLOAD = 2
};

enum class ModuleID : uint8_t {
    MOD_BT = 0,
    MOD_WIFI,
    MOD_GEMINI,
    MOD_MESH,
    MOD_MPY,
    MOD_FLASH,
    MOD_OTA,
    MOD_SETTINGS
};

struct I18nStrings {
    const char* bt;
    const char* wifi;
    const char* gemini;
    const char* mesh;
    const char* mpy;
    const char* flash;
    const char* ota;
    const char* settings;
    const char* cluster_offload;
    const char* cluster_active;
};

struct TileModule {
    ModuleID id;
    uint16_t accentColor565;
    bool installed;
    float clickScale;
    void (*onActivate)();
};

extern const I18nStrings kI18n[4];
extern LGFX tft;

bool validateBootSignature();
void renderBootSignatureStatic();
