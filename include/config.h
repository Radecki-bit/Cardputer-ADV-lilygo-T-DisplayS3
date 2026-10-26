#pragma once

#define SYSTEM_AUTHOR "@whoami"
#define SYSTEM_PROJECT "Smolinski|Labs Launcher"
#define FIRMWARE_VERSION "v1.0.8-PROTOTYPE"

#define LGFX_USE_V1

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <SPIFFS.h>
#include <SPI.h>
#include <SD.h>
#include <LovyanGFX.hpp>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp_err.h>
#include <esp_task_wdt.h>
#include <vector>
#include <cmath>
#include <cstring>

#define SD_CS_PIN        10
#define SD_MOSI_PIN      11
#define SD_MISO_PIN      13
#define SD_SCK_PIN       12

#define PIN_POWER_ON       15
#define PIN_BTN_TOP        0
#define PIN_BTN_BOTTOM     14
#define PIN_LCD_BL         38
#define PIN_ANTENNA_SW     16
#define PIN_ADV_DETECT     43
#define PIN_GROVE_SDA      18
#define PIN_GROVE_SCL      17

#define ADV_2G4_DISABLED   0
#define ADV_2G4_ENABLED    1

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789  _panel_instance;
    lgfx::Bus_Parallel8 _bus_instance;
    lgfx::Light_PWM     _light_instance;

public:
    LGFX(void) {
        {
            auto cfg = _bus_instance.config();
            cfg.freq_write = 20000000;
            cfg.pin_wr = 8;
            cfg.pin_rd = 9;
            cfg.pin_rs = 7;
            cfg.pin_d0 = 39;
            cfg.pin_d1 = 40;
            cfg.pin_d2 = 41;
            cfg.pin_d3 = 42;
            cfg.pin_d4 = 45;
            cfg.pin_d5 = 46;
            cfg.pin_d6 = 47;
            cfg.pin_d7 = 48;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs           = 6;
            cfg.pin_rst          = 5;
            cfg.pin_busy         = -1;
            cfg.panel_width      = 170;
            cfg.panel_height     = 320;
            cfg.offset_x         = 35;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable         = true;
            cfg.invert           = true;
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            _panel_instance.config(cfg);
        }
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl      = PIN_LCD_BL;
            cfg.invert      = false;
            cfg.freq        = 12000;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }
        setPanel(&_panel_instance);
    }

    bool init_impl(bool use_reset, bool use_clear) override {
        pinMode(PIN_POWER_ON, OUTPUT);
        digitalWrite(PIN_POWER_ON, HIGH);
        delay(10);
        return lgfx::LGFX_Device::init_impl(use_reset, use_clear);
    }
};

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

struct AdvScheduleItem {
    uint32_t startAtMs;
    uint32_t endAtMs;
    bool enabled;
};

extern const I18nStrings kI18n[4];
extern LGFX tft;

bool validateBootSignature();
void renderBootSignatureStatic();
