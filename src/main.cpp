#include "launcher.h"

LGFX tft;
LGFX_Sprite canvas(&tft);
SPIClass sdSPI(FSPI);

const I18nStrings kI18n[4] = {
    {"BT SIG", "WiFi",   "Gemini", "Mesh", "uPython", "Flash", "OTA", "Config", "OFFLOAD", "HUD LINK"},
    {"BT SIG", "WLAN",   "Gemini", "Mesh", "uPython", "Flash", "OTA", "Setup",  "OFFLOAD", "HUD AKTIV"},
    {"BT SIG", "WiFi",   "Gemini", "Mesh", "uPython", "Flash", "OTA", "Opt",    "OFFLOAD", "HUD СВЯЗЬ"},
    {"BT SIG", "WiFi",   "Gemini", "Mesh", "uPython", "Flash", "OTA", "Ustaw.", "OFFLOAD", "HUD AKTYWNY"}
};

TileModule tiles[8] = {
    {ModuleID::MOD_BT,       0x05FF, true, 1.0f, nullptr},
    {ModuleID::MOD_WIFI,     0x07E0, true, 1.0f, nullptr},
    {ModuleID::MOD_GEMINI,   0xF81F, true, 1.0f, nullptr},
    {ModuleID::MOD_MESH,     0xFFE0, true, 1.0f, nullptr},
    {ModuleID::MOD_MPY,      0xFD20, true, 1.0f, nullptr},
    {ModuleID::MOD_FLASH,    0xF800, true, 1.0f, nullptr},
    {ModuleID::MOD_OTA,      0x07FF, true, 1.0f, nullptr},
    {ModuleID::MOD_SETTINGS, 0xCE79, true, 1.0f, nullptr}
};

LangID currentLang       = LangID::LANG_PL;
ClusterMode currentMode  = ClusterMode::MODE_DOCKED_CLUSTER;
uint8_t selectedTile     = 0;
bool sdMounted           = false;
bool waterfallMode       = false;

TelemetryPacket rxPacket;
volatile bool packetReceived = false;
uint32_t lastPacketMs        = 0;
uint8_t peakHold[64]         = {0};
uint8_t waterfallBuf[24][64] = {{0}};

uint32_t lastRenderMs    = 0;
uint32_t btnTopPressMs   = 0;
uint32_t btnBotPressMs   = 0;
bool btnTopLast          = true;
bool btnBotLast          = true;

uint8_t calculateXOR(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
    }
    return crc;
}

uint16_t thermalColor565(uint8_t val) {
    if (val < 64)  return tft.color565(0, val * 2, 128 + val * 2);       // Niebieski -> Cyjan
    if (val < 128) return tft.color565(0, 128 + (val - 64) * 2, 255 - (val - 64) * 4); // Cyjan -> Zielony
    if (val < 192) return tft.color565((val - 128) * 4, 255, 0);         // Zielony -> Żółty
    return tft.color565(255, 255 - (val - 192) * 4, 0);                  // Żółty -> Czerwony
}

void onEspNowRecv(const uint8_t* mac, const uint8_t* incomingData, int len) {
    if (len == sizeof(TelemetryPacket) && incomingData[0] == 0xA5) {
        uint8_t expectedCrc = calculateXOR(incomingData, sizeof(TelemetryPacket) - 1);
        if (expectedCrc == incomingData[sizeof(TelemetryPacket) - 1]) {
            memcpy(&rxPacket, incomingData, sizeof(TelemetryPacket));
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
    tft.setCursor(12, 55);
    tft.setTextSize(2);
    tft.printf("%s\n", SYSTEM_PROJECT);
    tft.setTextSize(1);
    tft.setCursor(12, 82);
    tft.setTextColor(0xFFE0, TFT_BLACK);
    tft.printf("AUTH: %s | %s | SD: %s", SYSTEM_AUTHOR, FIRMWARE_VERSION, sdMounted ? "OK" : "NO");
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

    // Topbar
    canvas.fillRect(0, 0, SCREEN_W, TOPBAR_H, 0x10A2);
    canvas.setTextColor(0x07FF);
    canvas.setTextSize(1);
    canvas.setCursor(8, 8);
    canvas.printf("%s [%s]", SYSTEM_PROJECT, SYSTEM_AUTHOR);

    bool linkAlive = (millis() - lastPacketMs < 1000) && (lastPacketMs > 0);
    canvas.setTextColor(linkAlive ? 0x07E0 : 0xFBE0);
    canvas.setCursor(220, 8);
    canvas.printf("SD:%s %s", sdMounted ? "OK" : "--", linkAlive ? s.cluster_active : "LOCAL");

    // 4x2 Grid
    for (int i = 0; i < 8; i++) {
        int col = i % GRID_COLS;
        int row = i / GRID_COLS;
        int x = GRID_START_X + col * (TILE_W + TILE_GAP);
        int y = GRID_START_Y + row * (TILE_H + TILE_GAP);

        bool sel = (i == selectedTile);
        uint16_t bg = sel ? 0x2124 : 0x1082;
        canvas.fillRoundRect(x, y, TILE_W, TILE_H, 6, bg);
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
    const char* modStr = (rxPacket.activeModule == 1) ? "CC1101" :
                         (rxPacket.activeModule == 2) ? "nRF24"  : "IDLE";
    const char* modeStr = (rxPacket.operationMode == 1) ? "SPEC" :
                          (rxPacket.operationMode == 2) ? "SNIFF" : "STBY";

    // Pasek nagłówkowy HUD
    canvas.fillRect(0, 0, SCREEN_W, 22, 0x1082);
    canvas.setTextSize(1);
    canvas.setTextColor(0x07FF);
    canvas.setCursor(6, 7);
    canvas.printf("[%s] %s", modStr, modeStr);

    canvas.setTextColor(0xFFE0);
    canvas.setCursor(105, 7);
    if (rxPacket.activeModule == 2) {
        canvas.printf("2400-2525 MHz");
    } else {
        canvas.printf("%6.2f MHz", (float)rxPacket.frequencyHz / 1000000.0f);
    }

    uint16_t rssiColor = (rxPacket.rssiDbm > -55) ? 0xF800 :
                         (rxPacket.rssiDbm > -80) ? 0xFFE0 : 0x07E0;
    canvas.setTextColor(rssiColor);
    canvas.setCursor(200, 7);
    canvas.printf("%4d dBm", rxPacket.rssiDbm);

    canvas.setTextColor(linkAlive ? 0x07E0 : 0xF800);
    canvas.setCursor(262, 7);
    canvas.printf(linkAlive ? "LINK OK" : "NO ADV");

    // Obszar wykresu (Y: 24..154, Wysokość: 130 px)
    const int graphTop = 26;
    const int graphH   = 128;

    if (!waterfallMode) {
        // Siatka pomocnicza dBm
        for (int y = graphTop; y <= graphTop + graphH; y += 32) {
            canvas.drawFastHLine(0, y, SCREEN_W, 0x18E3);
        }

        for (int i = 0; i < 64; i++) {
            uint8_t val = rxPacket.spectrumBins[i];
            if (val >= peakHold[i]) {
                peakHold[i] = val;
            } else if (peakHold[i] > 2) {
                peakHold[i] -= 2;
            }

            int barH  = (val * graphH) / 255;
            int peakY = graphTop + graphH - ((peakHold[i] * graphH) / 255);
            int x     = i * 5;

            if (barH > 0) {
                canvas.fillRect(x, graphTop + graphH - barH, 4, barH, thermalColor565(val));
            }
            canvas.drawFastHLine(x, peakY, 4, 0xFFFF);
        }
    } else {
        // Tryb Waterfall (Mapa ciepła)
        for (int r = 0; r < 24; r++) {
            for (int c = 0; c < 64; c++) {
                uint8_t v = waterfallBuf[r][c];
                if (v > 0) {
                    canvas.fillRect(c * 5, graphTop + r * 5, 4, 4, thermalColor565(v));
                }
            }
        }
    }

    // Stopka informacyjna
    canvas.setTextColor(0x7BEF);
    canvas.setCursor(6, 158);
    canvas.printf("TOP[0]: %s | BOT[14]: UI/Launcher | %s", waterfallMode ? "WATERFALL" : "SPECTRUM", SYSTEM_AUTHOR);

    canvas.pushSprite(0, 0);
}

void handleButtons() {
    bool topNow = digitalRead(PIN_BTN_TOP);
    bool botNow = digitalRead(PIN_BTN_BOTTOM);

    // Górny przycisk (GPIO 0)
    if (!topNow && btnTopLast) {
        btnTopPressMs = millis();
    } else if (topNow && !btnTopLast && (millis() - btnTopPressMs > 30)) {
        if (currentMode == ClusterMode::MODE_DOCKED_CLUSTER) {
            waterfallMode = !waterfallMode;
        } else {
            selectedTile = (selectedTile + 1) % 8;
        }
    }
    btnTopLast = topNow;

    // Dolny przycisk (GPIO 14)
    if (!botNow && btnBotLast) {
        btnBotPressMs = millis();
    } else if (botNow && !btnBotLast) {
        uint32_t held = millis() - btnBotPressMs;
        if (held > 600) {
            currentLang = static_cast<LangID>((static_cast<uint8_t>(currentLang) + 1) % 4);
        } else if (held > 30) {
            currentMode = (currentMode == ClusterMode::MODE_DOCKED_CLUSTER)
                          ? ClusterMode::MODE_STANDALONE_UI
                          : ClusterMode::MODE_DOCKED_CLUSTER;
        }
    }
    btnBotLast = botNow;
}

void setup() {
    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, HIGH);

    pinMode(PIN_ANTENNA_SW, OUTPUT);
    digitalWrite(PIN_ANTENNA_SW, ADV_2G4_ENABLED);

    pinMode(PIN_BTN_TOP, INPUT_PULLUP);
    pinMode(PIN_BTN_BOTTOM, INPUT_PULLUP);

    Serial.begin(115200);

    tft.init();
    tft.setRotation(1);
    tft.setBrightness(220);

    canvas.setColorDepth(16);
    canvas.createSprite(SCREEN_W, SCREEN_H);

    // Inicjalizacja karty SD na niezależnej szynie SPI
    sdSPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    sdMounted = SD.begin(SD_CS_PIN, sdSPI, 16000000);

    if (validateBootSignature()) {
        renderBootSignatureStatic();
    }

    // Inicjalizacja odbiornika ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(esp_now_recv_cb_t(onEspNowRecv));
    }

    memset(&rxPacket, 0, sizeof(rxPacket));
    rxPacket.activeModule  = 1;
    rxPacket.operationMode = 1;
    rxPacket.frequencyHz   = 433920000UL;
    rxPacket.rssiDbm       = -93;
}

void loop() {
    handleButtons();

    if (packetReceived) {
        packetReceived = false;
        for (int r = 23; r > 0; r--) {
            memcpy(waterfallBuf[r], waterfallBuf[r - 1], 64);
        }
        memcpy(waterfallBuf[0], rxPacket.spectrumBins, 64);
    }

    if (millis() - lastRenderMs >= 33) {
        lastRenderMs = millis();
        if (currentMode == ClusterMode::MODE_DOCKED_CLUSTER) {
            renderTacticalHUD();
        } else {
            renderLauncherUI();
        }
    }
}
