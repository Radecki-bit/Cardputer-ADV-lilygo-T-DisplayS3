#define LGFX_USE_V1
#include "../include/config.h"
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <LittleFS.h>
#include <SPIFFS.h>
#include <SPI.h>
#include <SD.h>
#include <LovyanGFX.hpp>

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

LGFX tft;
LGFX_Sprite canvas(&tft);
SPIClass sdSPI(FSPI);

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

enum class LangID : uint8_t { LANG_EN = 0, LANG_DE = 1, LANG_RU = 2, LANG_PL = 3 };
enum class ClusterMode : uint8_t { MODE_STANDALONE_UI = 0, MODE_DOCKED_CLUSTER = 1 };
enum class ModuleID : uint8_t { MOD_BT = 0, MOD_WIFI, MOD_GEMINI, MOD_MESH, MOD_MPY, MOD_FLASH, MOD_OTA, MOD_SETTINGS };

struct I18nStrings {
    const char *bt, *wifi, *gemini, *mesh, *mpy, *flash, *ota, *settings, *offload, *active;
};

const I18nStrings kI18n[4] = {
    {"BT SIG", "WiFi", "Gemini", "Mesh", "uPython", "Flash", "OTA", "Config", "OFFLOAD", "HUD LINK"},
    {"BT SIG", "WLAN", "Gemini", "Mesh", "uPython", "Flash", "OTA", "Setup",  "OFFLOAD", "HUD AKTIV"},
    {"BT SIG", "WiFi", "Gemini", "Mesh", "uPython", "Flash", "OTA", "Opt",    "OFFLOAD", "HUD СВЯЗЬ"},
    {"BT SIG", "WiFi", "Gemini", "Mesh", "uPython", "Flash", "OTA", "Ustaw.", "OFFLOAD", "HUD AKTYWNY"}
};

struct TileModule {
    ModuleID id;
    uint16_t accentColor565;
    bool installed;
};

TileModule tiles[8] = {
    {ModuleID::MOD_BT,       0x05FF, true},
    {ModuleID::MOD_WIFI,     0x07E0, true},
    {ModuleID::MOD_GEMINI,   0xF81F, true},
    {ModuleID::MOD_MESH,     0xFFE0, true},
    {ModuleID::MOD_MPY,      0xFD20, true},
    {ModuleID::MOD_FLASH,    0xF800, true},
    {ModuleID::MOD_OTA,      0x07FF, true},
    {ModuleID::MOD_SETTINGS, 0xCE79, true}
};

LangID currentLang       = LangID::LANG_PL;
ClusterMode currentMode  = ClusterMode::MODE_DOCKED_CLUSTER;
uint8_t selectedTile     = 0;
bool sdMounted           = false;
bool protoLoaded         = false;
uint32_t sdSizeMB        = 0;
uint8_t localAnt24GState = ANT_24GHZ_EXTERNAL;
bool waterfallMode       = false;

EspNowProtocolHeader activeProtoS3;
EspNowProtocolHeader activeProtoEsp32U;
TelemetryPacket rxPacket;
volatile bool packetReceived = false;
uint32_t lastPacketMs        = 0;
uint8_t peakHold[64]         = {0};
uint8_t waterfallBuf[24][64] = {{0}};
uint32_t lastRenderMs        = 0;
uint32_t btnTopPressMs       = 0;
uint32_t btnBotPressMs       = 0;
bool btnTopLast              = true;
bool btnBotLast              = true;

void setReserved24GHzAntenna(uint8_t state) {
    localAnt24GState = (state == ANT_24GHZ_EXTERNAL) ? ANT_24GHZ_EXTERNAL : ANT_24GHZ_INTERNAL;
    digitalWrite(PIN_ANTENNA_SW, (localAnt24GState == ANT_24GHZ_EXTERNAL) ? HIGH : LOW);
}

void logSnapshotToSD() {
    if (!sdMounted) return;
    File f = SD.open("/tdisplay_rf.log", FILE_APPEND);
    if (f) {
        f.printf("[%lu ms] FREQ:%lu Hz RSSI:%d dBm ANT24G:%s PROTO:%s\n",
                 millis(), rxPacket.frequencyHz, rxPacket.rssiDbm,
                 (localAnt24GState == ANT_24GHZ_EXTERNAL) ? "EXT" : "INT",
                 protoLoaded ? "SMRA_OK" : "DEF");
        f.close();
    }
}

uint16_t thermalColor565(uint8_t val) {
    if (val < 64)  return tft.color565(0, val * 2, 128 + val * 2);
    if (val < 128) return tft.color565(0, 128 + (val - 64) * 2, 255 - (val - 64) * 4);
    if (val < 192) return tft.color565((val - 128) * 4, 255, 0);
    return tft.color565(255, 255 - (val - 192) * 4, 0);
}

void onEspNowRecv(const uint8_t*, const uint8_t* incomingData, int len) {
    if (len == sizeof(TelemetryPacket) && incomingData[0] == 0xA5) {
        if (calculateXOR(incomingData, sizeof(TelemetryPacket) - 1) == incomingData[sizeof(TelemetryPacket) - 1]) {
            memcpy(&rxPacket, incomingData, sizeof(TelemetryPacket));
            setReserved24GHzAntenna(rxPacket.ant24GState);
            lastPacketMs   = millis();
            packetReceived = true;
        }
    }
}

bool validateBootSignature() {
    return (strcmp(SYSTEM_AUTHOR, "@whoami") == 0);
}

void renderBootSignatureStatic() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(0x07E0, TFT_BLACK);
    tft.setCursor(12, 50);
    tft.setTextSize(2);
    tft.printf("%s\n", SYSTEM_PROJECT);
    tft.setTextSize(1);
    tft.setCursor(12, 78);
    tft.setTextColor(0xFFE0, TFT_BLACK);
    if (sdMounted) {
        tft.printf("AUTH:%s | SD:%luMB | PROTO:%s | 2.4G:%s", SYSTEM_AUTHOR, sdSizeMB,
                   protoLoaded ? "S3+32U" : "CFG",
                   (localAnt24GState == ANT_24GHZ_EXTERNAL) ? "EXT" : "INT");
    } else {
        tft.printf("AUTH: %s | SD: NO | 2.4G:%s", SYSTEM_AUTHOR,
                   (localAnt24GState == ANT_24GHZ_EXTERNAL) ? "EXT" : "INT");
    }
    delay(600);
}

const char* getTileTitle(ModuleID id, const I18nStrings& s) {
    switch (id) {
        case ModuleID::MOD_BT:       return s.bt;
        case ModuleID::MOD_WIFI:     return s.wifi;
        case ModuleID::MOD_GEMINI:   return s.gemini;
        case ModuleID::MOD_MESH:     return s.mesh;
        case ModuleID::MOD_MPY:      return s.mpy;
        case ModuleID::MOD_FLASH:    return s.flash;
        case ModuleID::MOD_OTA:      return s.ota;
        case ModuleID::MOD_SETTINGS: return s.settings;
        default:                     return "";
    }
}

void renderLauncherUI() {
    canvas.fillSprite(0x0841);
    const I18nStrings& s = kI18n[static_cast<uint8_t>(currentLang)];

    canvas.fillRect(0, 0, SCREEN_W, TOPBAR_H, 0x10A2);
    canvas.setTextColor(0x07FF);
    canvas.setTextSize(1);
    canvas.setCursor(8, 8);
    canvas.printf("%s [%s]", SYSTEM_PROJECT, SYSTEM_AUTHOR);

    bool linkAlive = (millis() - lastPacketMs < 1000) && (lastPacketMs > 0);
    canvas.setTextColor(linkAlive ? 0x07E0 : 0xFBE0);
    canvas.setCursor(192, 8);
    canvas.printf("SD:%s 2.4G:%s %s",
                  sdMounted ? "OK" : "--",
                  (localAnt24GState == ANT_24GHZ_EXTERNAL) ? "EXT" : "INT",
                  linkAlive ? "LINK" : "LOC");

    for (int i = 0; i < 8; i++) {
        int col = i % GRID_COLS;
        int row = i / GRID_COLS;
        int x = GRID_START_X + col * (TILE_W + TILE_GAP);
        int y = GRID_START_Y + row * (TILE_H + TILE_GAP);

        bool sel = (i == selectedTile);
        canvas.fillRoundRect(x, y, TILE_W, TILE_H, 6, sel ? 0x2124 : 0x1082);
        canvas.drawRoundRect(x, y, TILE_W, TILE_H, 6, sel ? 0xFFFF : tiles[i].accentColor565);
        canvas.fillCircle(x + 14, y + 18, 5, tiles[i].accentColor565);
        canvas.setTextColor(sel ? 0xFFFF : 0xCE79);
        canvas.setCursor(x + 8, y + 42);
        canvas.printf("%s", getTileTitle(tiles[i].id, s));
    }
    canvas.pushSprite(0, 0);
}

void renderTacticalHUD() {
    canvas.fillSprite(TFT_BLACK);
    bool linkAlive = (millis() - lastPacketMs < 600) && (lastPacketMs > 0);
    const char* modStr = (rxPacket.activeModule == 1) ? "CC1101" : (rxPacket.activeModule == 2) ? "nRF24" : "IDLE";
    const char* modeStr = (rxPacket.operationMode == 1) ? "SPEC" : (rxPacket.operationMode == 2) ? "SNIFF" : "STBY";

    canvas.fillRect(0, 0, SCREEN_W, 22, 0x1082);
    canvas.setTextSize(1);
    canvas.setTextColor(0x07FF);
    canvas.setCursor(6, 7);
    canvas.printf("[%s] %s", modStr, modeStr);

    canvas.setTextColor(0xFFE0);
    canvas.setCursor(105, 7);
    if (rxPacket.activeModule == 2) canvas.printf("2400-2525 MHz");
    else canvas.printf("%6.2f MHz", (float)rxPacket.frequencyHz / 1000000.0f);

    uint16_t rssiColor = (rxPacket.rssiDbm > -55) ? 0xF800 : (rxPacket.rssiDbm > -80) ? 0xFFE0 : 0x07E0;
    canvas.setTextColor(rssiColor);
    canvas.setCursor(200, 7);
    canvas.printf("%4d dBm", rxPacket.rssiDbm);

    canvas.setTextColor(linkAlive ? 0x07E0 : 0xF800);
    canvas.setCursor(262, 7);
    canvas.printf(linkAlive ? "LINK OK" : "NO ADV");

    const int graphTop = 26, graphH = 128;
    if (!waterfallMode) {
        for (int y = graphTop; y <= graphTop + graphH; y += 32) canvas.drawFastHLine(0, y, SCREEN_W, 0x18E3);
        for (int i = 0; i < 64; i++) {
            uint8_t val = rxPacket.spectrumBins[i];
            if (val >= peakHold[i]) peakHold[i] = val;
            else if (peakHold[i] > 2) peakHold[i] -= 2;

            int barH = (val * graphH) / 255;
            int peakY = graphTop + graphH - ((peakHold[i] * graphH) / 255);
            int x = i * 5;

            if (barH > 0) canvas.fillRect(x, graphTop + graphH - barH, 4, barH, thermalColor565(val));
            canvas.drawFastHLine(x, peakY, 4, 0xFFFF);
        }
    } else {
        for (int r = 0; r < 24; r++) {
            for (int c = 0; c < 64; c++) {
                uint8_t v = waterfallBuf[r][c];
                if (v > 0) canvas.fillRect(c * 5, graphTop + r * 5, 4, 4, thermalColor565(v));
            }
        }
    }

    canvas.setTextColor(0x7BEF);
    canvas.setCursor(6, 158);
    canvas.printf("TOP:%s | 2.4G:%s | SD:%s | %s",
                  waterfallMode ? "WFALL" : "SPEC",
                  (localAnt24GState == ANT_24GHZ_EXTERNAL) ? "EXT" : "INT",
                  sdMounted ? "OK" : "--",
                  SYSTEM_AUTHOR);
    canvas.pushSprite(0, 0);
}

void handleButtons() {
    bool topNow = digitalRead(PIN_BTN_TOP);
    bool botNow = digitalRead(PIN_BTN_BOTTOM);

    if (!topNow && btnTopLast) btnTopPressMs = millis();
    else if (topNow && !btnTopLast) {
        uint32_t held = millis() - btnTopPressMs;
        if (held > 600) {
            setReserved24GHzAntenna(localAnt24GState == ANT_24GHZ_EXTERNAL ? ANT_24GHZ_INTERNAL : ANT_24GHZ_EXTERNAL);
            logSnapshotToSD();
        } else if (held > 30) {
            if (currentMode == ClusterMode::MODE_DOCKED_CLUSTER) waterfallMode = !waterfallMode;
            else selectedTile = (selectedTile + 1) % 8;
        }
    }
    btnTopLast = topNow;

    if (!botNow && btnBotLast) btnBotPressMs = millis();
    else if (botNow && !btnBotLast) {
        uint32_t held = millis() - btnBotPressMs;
        if (held > 600) currentLang = static_cast<LangID>((static_cast<uint8_t>(currentLang) + 1) % 4);
        else if (held > 30) currentMode = (currentMode == ClusterMode::MODE_DOCKED_CLUSTER) ? ClusterMode::MODE_STANDALONE_UI : ClusterMode::MODE_DOCKED_CLUSTER;
    }
    btnBotLast = botNow;
}

void setup() {
    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, HIGH);
    pinMode(PIN_ANTENNA_SW, OUTPUT);
    setReserved24GHzAntenna(ANT_24GHZ_EXTERNAL);
    pinMode(PIN_BTN_TOP, INPUT_PULLUP);
    pinMode(PIN_BTN_BOTTOM, INPUT_PULLUP);

    Serial.begin(115200);
    Serial1.begin(115200, SERIAL_8N1, PIN_TDISPLAY_GROVE_RX, PIN_TDISPLAY_GROVE_TX);

    tft.init();
    tft.setRotation(3);
    tft.setBrightness(220);

    canvas.setColorDepth(16);
    canvas.createSprite(SCREEN_W, SCREEN_H);

    sdSPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    sdMounted = SD.begin(SD_CS_PIN, sdSPI, 16000000);
    if (sdMounted) {
        sdSizeMB = (uint32_t)(SD.cardSize() / (1024ULL * 1024ULL));
        provisionDefaultSDConfigs();

        bool okS3  = loadEspNowProtocolBin("/protocols/espnow_tdisplay_s3.bin", activeProtoS3);
        bool ok32U = loadEspNowProtocolBin("/protocols/espnow_esp32_u.bin", activeProtoEsp32U);
        protoLoaded = (okS3 && ok32U);

        String antCfg = readCfgValue("/config/tdisplay_s3.cfg", "ant_24ghz_mode", "EXT");
        setReserved24GHzAntenna(antCfg == "INT" ? ANT_24GHZ_INTERNAL : ANT_24GHZ_EXTERNAL);

        int langCfg = readCfgValue("/config/tdisplay_s3.cfg", "language_id", "3").toInt();
        currentLang = static_cast<LangID>(constrain(langCfg, 0, 3));

        int modeCfg = readCfgValue("/config/tdisplay_s3.cfg", "cluster_mode", "1").toInt();
        currentMode = (modeCfg == 0) ? ClusterMode::MODE_STANDALONE_UI : ClusterMode::MODE_DOCKED_CLUSTER;

        int wfCfg = readCfgValue("/config/tdisplay_s3.cfg", "waterfall_default", "0").toInt();
        waterfallMode = (wfCfg == 1);

        int blCfg = readCfgValue("/config/tdisplay_s3.cfg", "lcd_brightness", "220").toInt();
        tft.setBrightness(constrain(blCfg, 20, 255));
    }

    if (validateBootSignature()) renderBootSignatureStatic();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    if (esp_now_init() == ESP_OK) esp_now_register_recv_cb(esp_now_recv_cb_t(onEspNowRecv));

    memset(&rxPacket, 0, sizeof(rxPacket));
    rxPacket.activeModule  = 1;
    rxPacket.operationMode = 1;
    rxPacket.frequencyHz   = FREQ_433_92_MHZ;
    rxPacket.rssiDbm       = -93;
    rxPacket.ant24GState   = localAnt24GState;
    rxPacket.sdStatus      = sdMounted ? 1 : 0;
}

void loop() {
    handleButtons();
    if (packetReceived) {
        packetReceived = false;
        for (int r = 23; r > 0; r--) memcpy(waterfallBuf[r], waterfallBuf[r - 1], 64);
        memcpy(waterfallBuf[0], rxPacket.spectrumBins, 64);
    }

    if (millis() - lastRenderMs >= 33) {
        lastRenderMs = millis();
        if (currentMode == ClusterMode::MODE_DOCKED_CLUSTER) renderTacticalHUD();
        else renderLauncherUI();
    }
}
