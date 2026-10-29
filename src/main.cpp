#define LGFX_USE_V1
#include "../include/config.h"
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
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
        pinMode(PIN_LCD_BL, OUTPUT);
        digitalWrite(PIN_LCD_BL, HIGH);
        delay(20);
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
uint8_t localAnt24GState = ANT_24GHZ_OFF; // Domyślnie WYŁĄCZONA (0V na GPIO 16)
bool waterfallMode       = false;
uint32_t antBannerUntil  = 0;

EspNowProtocolHeader activeProtoS3;
TelemetryPacket rxPacket;
volatile bool packetReceived = false;
uint32_t lastPacketMs        = 0;
uint8_t peakHold[64]         = {0};
uint8_t waterfallBuf[24][64] = {{0}};
uint32_t lastRenderMs        = 0;

// Obsługa przycisków i detekcja dwukliku na GPIO 14 (PIN_BTN_BOTTOM)
uint32_t btnTopPressMs       = 0;
uint32_t btnBotPressMs       = 0;
bool btnTopLast              = true;
bool btnBotLast              = true;
uint8_t btnBotClickCount     = 0;
uint32_t btnBotFirstClickMs  = 0;

void setReserved24GHzAntenna(uint8_t state) {
    localAnt24GState = (state == ANT_24GHZ_ON) ? ANT_24GHZ_ON : ANT_24GHZ_OFF;
    digitalWrite(PIN_ANTENNA_SW, (localAnt24GState == ANT_24GHZ_ON) ? HIGH : LOW);
}

void logSnapshotToSD() {
    if (!sdMounted) return;
    File f = SD.open("/tdisplay_rf.log", FILE_APPEND);
    if (f) {
        f.printf("[%lu ms] FREQ:%lu Hz RSSI:%d dBm ANT24G:%s\n",
                 millis(), rxPacket.frequencyHz, rxPacket.rssiDbm,
                 (localAnt24GState == ANT_24GHZ_ON) ? "ON" : "OFF");
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
            uint8_t prevRemoteAnt = rxPacket.ant24GState;
            memcpy(&rxPacket, incomingData, sizeof(TelemetryPacket));
            if (rxPacket.ant24GState != prevRemoteAnt) {
                setReserved24GHzAntenna(rxPacket.ant24GState);
                antBannerUntil = millis() + 1500;
            }
            lastPacketMs   = millis();
            packetReceived = true;
        }
    }
}

void renderBootSignatureStatic() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(0x07E0, TFT_BLACK);
    tft.setCursor(12, 42);
    tft.setTextSize(2);
    tft.printf("%s\n", SYSTEM_PROJECT);
    tft.setTextSize(1);
    tft.setCursor(12, 74);
    tft.setTextColor(0xFFE0, TFT_BLACK);
    tft.printf("AUTH: %s | %s | SD: %s", SYSTEM_AUTHOR, FIRMWARE_VERSION, sdMounted ? "OK" : "NO");
    tft.setCursor(12, 92);
    tft.setTextColor(0x07FF, TFT_BLACK);
    tft.printf("GNIAZDO 2.4GHz: WYLACZONE (SAFE 0V) -> 2x KLIK IO14 = ON");
    delay(900);
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

void drawAntPopupIfActive() {
    if (millis() < antBannerUntil) {
        uint16_t bg = (localAnt24GState == ANT_24GHZ_ON) ? 0x03E0 : 0x7800;
        canvas.fillRoundRect(45, 62, 230, 44, 6, bg);
        canvas.drawRoundRect(45, 62, 230, 44, 6, TFT_WHITE);
        canvas.setTextColor(TFT_WHITE);
        canvas.setTextSize(1);
        canvas.setCursor(58, 72);
        canvas.printf("GNIAZDO ANTENY 2.4GHz (GPIO 16):");
        canvas.setCursor(58, 88);
        canvas.printf("STATUS: %s", (localAnt24GState == ANT_24GHZ_ON) ? "WLACZONE (ON - 3.3V)" : "WYLACZONE (OFF - 0V)");
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
    canvas.setTextColor((localAnt24GState == ANT_24GHZ_ON) ? 0x07E0 : 0xF800);
    canvas.setCursor(185, 8);
    canvas.printf("2.4G:%s SD:%s %s",
                  (localAnt24GState == ANT_24GHZ_ON) ? "ON" : "OFF",
                  sdMounted ? "OK" : "--",
                  linkAlive ? "LNK" : "LOC");

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
    drawAntPopupIfActive();
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

    canvas.setTextColor((localAnt24GState == ANT_24GHZ_ON) ? 0x07E0 : 0x7BEF);
    canvas.setCursor(6, 158);
    canvas.printf("2xKLIK:2.4G[%s] | TOP:%s | SD:%s | %s",
                  (localAnt24GState == ANT_24GHZ_ON) ? "ON" : "OFF",
                  waterfallMode ? "WFALL" : "SPEC",
                  sdMounted ? "OK" : "--",
                  SYSTEM_AUTHOR);
    drawAntPopupIfActive();
    canvas.pushSprite(0, 0);
}

void handleButtons() {
    uint32_t now = millis();
    bool topNow = digitalRead(PIN_BTN_TOP);
    bool botNow = digitalRead(PIN_BTN_BOTTOM);

    // Przycisk BOOT (GPIO 0) - zmiana trybu wykresu / kafelka
    if (!topNow && btnTopLast) btnTopPressMs = now;
    else if (topNow && !btnTopLast) {
        uint32_t held = now - btnTopPressMs;
        if (held > 30) {
            if (currentMode == ClusterMode::MODE_DOCKED_CLUSTER) waterfallMode = !waterfallMode;
            else selectedTile = (selectedTile + 1) % 8;
        }
    }
    btnTopLast = topNow;

    // Drugi przycisk (GPIO 14, nie-BOOT):
    // - Szybki dwuklik (<= 400ms) -> Włącza / Wyłącza gniazdo anteny 2.4GHz (GPIO 16)
    // - Pojedynczy klik -> Przełącza HUD / Launcher UI
    // - Długie przytrzymanie (> 700ms) -> Zmiana języka
    if (!botNow && btnBotLast) {
        btnBotPressMs = now;
    } else if (botNow && !btnBotLast) {
        uint32_t held = now - btnBotPressMs;
        if (held > 700) {
            currentLang = static_cast<LangID>((static_cast<uint8_t>(currentLang) + 1) % 4);
            btnBotClickCount = 0;
        } else if (held > 25) {
            btnBotClickCount++;
            if (btnBotClickCount == 1) {
                btnBotFirstClickMs = now;
            } else if (btnBotClickCount >= 2 && (now - btnBotFirstClickMs <= 400)) {
                setReserved24GHzAntenna(localAnt24GState == ANT_24GHZ_ON ? ANT_24GHZ_OFF : ANT_24GHZ_ON);
                antBannerUntil = now + 1800;
                logSnapshotToSD();
                btnBotClickCount = 0;
            }
        }
    }
    btnBotLast = botNow;

    if (btnBotClickCount == 1 && (now - btnBotFirstClickMs > 400)) {
        currentMode = (currentMode == ClusterMode::MODE_DOCKED_CLUSTER)
                      ? ClusterMode::MODE_STANDALONE_UI
                      : ClusterMode::MODE_DOCKED_CLUSTER;
        btnBotClickCount = 0;
    }
}


// === SMOLINSKI|LABS HFT ARBITRAGE ENGINE (KuCoin <-> TON <-> BingX) ===
struct TxRecord {
    uint32_t id;
    bool dirKtoB;
    bool isWin;
    float cutUsdt;
};

TxRecord history[3];
uint32_t txCounter = 419;
float walletUsdt = 142.85f;
int winStreak = 0;
int nextLossTarget = 8;

void drawStaticHUD() {
    tft.fillScreen(0x0000);

    // GÓRNY PASEK SYSTEMOWY
    tft.fillRect(0, 0, 320, 18, 0x0863);
    tft.setTextSize(1);
    tft.setTextColor(0x07FF, 0x0863);
    tft.setCursor(6, 5);
    tft.print("SMOLINSKI|LABS HFT CORE v4.2");
    tft.setTextColor(0x07E0, 0x0863);
    tft.setCursor(218, 5);
    tft.print("TON-BRIDGE: LIVE");

    // PASEK OGRANICZENIA PROCESORA (HW CAP 5-15 USDT)
    tft.fillRect(0, 19, 320, 15, 0x2080);
    tft.setTextColor(0xFD20, 0x2080);
    tft.setCursor(6, 23);
    tft.print("HW LIMIT: ESP32-S3 SRAM/CPU CAP [MAX 15.00 USDT/TX]");

    // WĘZŁY GIEŁD I DŁUGA LINIA PRZESYŁU TON
    // KuCoin (Lewy)
    tft.drawRect(6, 38, 64, 24, 0x07FF);
    tft.setTextColor(0x07FF, 0x0000);
    tft.setCursor(16, 46);
    tft.print("KuCoin");

    // BingX (Prawy)
    tft.drawRect(250, 38, 64, 24, 0x051F);
    tft.setTextColor(0x051F, 0x0000);
    tft.setCursor(266, 46);
    tft.print("BingX");

    // Długa kreska (Magistrala TON)
    tft.drawFastHLine(70, 49, 180, 0x39E7);
    tft.drawFastHLine(70, 50, 180, 0x07FF);
    tft.drawFastHLine(70, 51, 180, 0x39E7);

    // Etykieta TON nad linią
    tft.setTextColor(0xFFFF, 0x0000);
    tft.setCursor(116, 38);
    tft.print("< TON BLOCKCHAIN >");

    // RAMKA TELEMETRII GOTÓWKI
    tft.drawRect(6, 66, 308, 34, 0x2124);
    tft.drawFastVLine(158, 66, 34, 0x2124);

    tft.setTextColor(0xBDF7, 0x0000);
    tft.setCursor(12, 70);
    tft.print("10% PROFIT CUT (5-15$):");
    tft.setCursor(166, 70);
    tft.print("TON WALLET BALANCE:");

    // NAGŁÓWEK LOGU TRANSAKCJI
    tft.drawFastHLine(6, 103, 308, 0x2124);
    tft.setTextColor(0x7BEF, 0x0000);
    tft.setCursor(6, 107);
    tft.print("TX#    ROUTE           10% FEE CUT       STATUS");

    // DOLNY PASEK STATUSU
    tft.fillRect(0, 156, 320, 14, 0x0841);
    tft.setTextColor(0x07E0, 0x0841);
    tft.setCursor(6, 159);
    tft.print("ARB: ACTIVE | FEE: 10% | LATENCY: 12ms | @whoami");
}

void animateTonBridge(bool kToB, bool isWin) {
    uint16_t pulseCol = isWin ? 0x07E0 : 0xF800;
    int startX = kToB ? 72 : 236;
    int endX   = kToB ? 236 : 72;
    int step   = kToB ? 12 : -12;

    for (int x = startX; (kToB ? (x <= endX) : (x >= endX)); x += step) {
        // Rysujemy impuls lecący po długiej kresce
        tft.fillRect(x, 47, 12, 7, pulseCol);
        delay(18);
        // Przywracamy długą kreskę za impulsem
        tft.fillRect(x, 47, 12, 7, 0x0000);
        tft.drawFastHLine(x, 49, 12, 0x39E7);
        tft.drawFastHLine(x, 50, 12, 0x07FF);
        tft.drawFastHLine(x, 51, 12, 0x39E7);
    }
}

void updateTelemetry(const TxRecord& rec) {
    char buf[40];

    // Aktualizacja ostatniego wpływu 10% (lub straty)
    tft.setTextSize(2);
    if (rec.isWin) {
        tft.setTextColor(0x07E0, 0x0000);
        snprintf(buf, sizeof(buf), "+%05.2f USDT ", rec.cutUsdt);
    } else {
        tft.setTextColor(0xF800, 0x0000);
        snprintf(buf, sizeof(buf), "-%05.2f USDT ", rec.cutUsdt);
    }
    tft.setCursor(12, 81);
    tft.print(buf);

    // Aktualizacja salda portfela TON
    tft.setTextColor(0x07FF, 0x0000);
    snprintf(buf, sizeof(buf), "%06.2f USDT ", walletUsdt);
    tft.setCursor(166, 81);
    tft.print(buf);

    // Aktualizacja 3 ostatnich wierszy historii
    tft.setTextSize(1);
    for (int i = 0; i < 3; i++) {
        if (history[i].id == 0) continue;
        int y = 119 + (i * 12);
        uint16_t rowCol = history[i].isWin ? 0x07E0 : 0xF800;
        tft.setTextColor(rowCol, 0x0000);

        const char* route = history[i].dirKtoB ? "KC->TON->BX" : "BX->TON->KC";
        const char* sign  = history[i].isWin ? "+" : "-";
        const char* stat  = history[i].isWin ? "[SETTLED]" : "[SLIPPAGE]";

        snprintf(buf, sizeof(buf), "#%04lu  %s   %s%05.2f USDT     %s   ",
                 (unsigned long)history[i].id, route, sign, history[i].cutUsdt, stat);
        tft.setCursor(6, y);
        tft.print(buf);
    }
}

void setup() {
    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);
    pinMode(38, OUTPUT);
    digitalWrite(38, HIGH);
    delay(50);

    tft.init();
    tft.setRotation(1);
    randomSeed(esp_random());

    nextLossTarget = random(8, 11); // Co 8-10 transakcja stratna
    drawStaticHUD();
}

void loop() {
    txCounter++;
    winStreak++;

    bool isWin = true;
    if (winStreak >= nextLossTarget) {
        isWin = false;
        winStreak = 0;
        nextLossTarget = random(8, 11); // Kolejna strata znów za 8 do 10 strzałów
    }

    bool dirKtoB = (random(0, 100) < 50);
    float amount = 0.0f;

    if (isWin) {
        // Losowa kwota 10% prowizji od 5.05 do 14.95 USDT (różne końcówki)
        amount = random(505, 1496) / 100.0f;
        walletUsdt += amount;
    } else {
        // Losowa korekta/strata przy poślizgu (od 4.20 do 11.80 USDT)
        amount = random(420, 1181) / 100.0f;
        walletUsdt -= amount;
    }

    // Animacja przelotu pakietu po długiej kresce przez TON
    animateTonBridge(dirKtoB, isWin);

    // Przesunięcie bufora logów
    history[2] = history[1];
    history[1] = history[0];
    history[0] = {txCounter, dirKtoB, isWin, amount};

    updateTelemetry(history[0]);

    // Losowy odstęp między transakcjami HFT (700 - 1450 ms)
    delay(random(700, 1450));
}
