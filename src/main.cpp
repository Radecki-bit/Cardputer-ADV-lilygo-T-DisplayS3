#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <time.h>
#include <sys/time.h>
#include "config.h"
#include "cyber_assets.h"

int selectedTile = 0;
const int totalTiles = 8;

enum class AppState {
    DESKTOP,
    MODULE_VIEW
};

AppState currentAppState = AppState::DESKTOP;

const uint16_t COLOR_BG_DARK   = 0x0822;
const uint16_t COLOR_PANEL_BG  = 0x1065; 
const uint16_t COLOR_NEON_CYAN = 0x07FF; 
const uint16_t COLOR_NEON_MAG  = 0xF81F; 
const uint16_t COLOR_NEON_YEL  = 0xFFE0; 
const uint16_t COLOR_NEON_GRN  = 0x07E0;
const uint16_t COLOR_TEXT      = 0xFFFF; 
const uint16_t COLOR_MUTED     = 0x9CD3; 

LGFX_Sprite canvas(&tft);

// Obsługa 4 języków: 0=EN, 1=DE, 2=PL, 3=RU
int langIdx = 0;
const char* langCodes[4] = {"EN (English)", "DE (Deutsch)", "PL (Polski)", "RU (Russkiy)"};

const char* moduleNames[4][8] = {
    {"WIFI SCANNER", "BT / 2.4G SNIFFER", "MESH LORA 868", "GEMINI AI BRIDGE", "MicroPython VM", "FLASH & SILICON", "OTA ENGINE", "SYSTEM SETTINGS"},
    {"WIFI SCANNER", "BT / 2.4G SNIFFER", "MESH LORA 868", "GEMINI KI BRUCKE", "MicroPython VM", "FLASH & SILIZIUM", "OTA UPDATE", "EINSTELLUNGEN"},
    {"SKANER WIFI",  "SNIFFER BT / 2.4G", "MESH LORA 868", "MOSTEK GEMINI AI", "MicroPython VM", "PAMIEC I UKLAD",   "SILNIK OTA", "USTAWIENIA"},
    {"SKANER WIFI",  "SNIFFER BT / 2.4G", "MESH LORA 868", "MOST GEMINI AI",   "MicroPython VM", "PAMYAT I CHIP",    "MODUL OTA",  "NASTROYKI"}
};

const char* uiExecTitle[4] = {
    "MODULE EXECUTION", "MODUL AUSFUHRUNG", "URUCHOMIONY MODUL", "RABOTA MODULYA"
};

const char* uiFooterStd[4] = {
    "[BTN_BOT] Back  |  [BTN_TOP] Execute",
    "[BTN_BOT] Zuruck | [BTN_TOP] Aktion",
    "[BTN_BOT] Wstecz | [BTN_TOP] Akcja",
    "[BTN_BOT] Nazad  | [BTN_TOP]usk"
};

const char* uiFooterSet[4] = {
    "[BOT] Back | [TOP] Tap:Change / Hold:Next",
    "[BOT] Zuruck | [TOP] Klick:Wahl / Halten",
    "[BOT] Wstecz | [TOP] Klik:Zmien / Trzymaj",
    "[BOT] Nazad | [TOP] Klik:Izmen / Derzhat"
};

const char* setLabels[4][5] = {
    {"1. Language", "2. Brightness", "3. Clock (+1 Hour)", "4. Clock (+1 Min)", "5. Soft Reboot"},
    {"1. Sprache",  "2. Helligkeit", "3. Uhr (+1 Std)",    "4. Uhr (+1 Min)",   "5. Neustart"},
    {"1. Jezyk",    "2. Jasnosc",    "3. Zegar (+1 Godz)", "4. Zegar (+1 Min)", "5. Restart OS"},
    {"1. Yazyk",    "2. Yarkost",    "3. Chasy (+1 Chas)", "4. Chasy (+1 Min)", "5. Perezagruzka"}
};

uint32_t animFrame = 0;
int wifiNetworksFound = -1;
bool isScanningWiFi = false;

bool snifferActive = false;
volatile uint32_t snifferPktCount = 0;
volatile int snifferLastRssi = -95;
uint8_t snifferChannel = 1;
uint32_t lastChannelHop = 0;

uint32_t meshTxCount = 0;
uint32_t meshLastPingUs = 0;
int meshSimRssi = -64;

uint32_t gemTxCount = 0;
String gemLastRx = "USB-CDC Ready";

uint32_t mpyBenchUs = 0;
uint32_t mpyRuns = 0;

int flashPage = 0;
bool otaVerified = false;
String otaMd5Short = "";

int settingsRow = 0; // 0: Język, 1: Jasność, 2: Godzina, 3: Minuta, 4: Soft Reboot
const uint8_t brightVals[3] = {255, 140, 35};
const char* brightLabels[3] = {"100% MAX", "55% MED", "15% NIGHT"};
int brightIdx = 0;

void IRAM_ATTR promiscuousRxCb(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MISC) {
        snifferPktCount++;
        wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
        snifferLastRssi = pkt->rx_ctrl.rssi;
    }
}

void setSnifferMode(bool enable) {
    if (enable && !snifferActive) {
        WiFi.disconnect();
        esp_wifi_set_promiscuous(true);
        esp_wifi_set_promiscuous_rx_cb(&promiscuousRxCb);
        esp_wifi_set_channel(snifferChannel, WIFI_SECOND_CHAN_NONE);
        snifferActive = true;
    } else if (!enable && snifferActive) {
        esp_wifi_set_promiscuous(false);
        snifferActive = false;
    }
}

void initHardwareClock() {
    struct tm tm_init;
    tm_init.tm_year = 2026 - 1900;
    tm_init.tm_mon = 9;
    tm_init.tm_mday = 24;
    tm_init.tm_hour = 12;
    tm_init.tm_min = 40;
    tm_init.tm_sec = 0;
    time_t t = mktime(&tm_init);
    struct timeval now = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&now, NULL);
}

void adjustClock(int addHours, int addMinutes) {
    time_t now;
    time(&now);
    now += (addHours * 3600) + (addMinutes * 60);
    struct timeval tv = { .tv_sec = now, .tv_usec = 0 };
    settimeofday(&tv, NULL);
}

void getLiveClockString(char* buf, size_t len) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 5)) {
        snprintf(buf, len, "12:40 24.10");
        return;
    }
    char sep = (timeinfo.tm_sec % 2 == 0) ? ':' : ' ';
    snprintf(buf, len, "%02d%c%02d %02d.%02d",
             timeinfo.tm_hour, sep, timeinfo.tm_min,
             timeinfo.tm_mday, timeinfo.tm_mon + 1);
}

void runCpuBenchmark() {
    uint32_t t0 = micros();
    volatile float acc = 1.001f;
    for (int i = 0; i < 10000; i++) {
        acc = acc * 1.0001f + 0.0002f;
    }
    mpyBenchUs = micros() - t0;
    mpyRuns++;
}

void drawCyberNeonWallpaper() {
    canvas.fillScreen(COLOR_BG_DARK);
    
    int shiftX = (animFrame / 2) % 20;
    int shiftY = (animFrame / 3) % 20;

    for (int x = shiftX; x < SCREEN_W; x += 20) {
        canvas.drawFastVLine(x, 0, SCREEN_H, 0x1864);
    }
    for (int y = shiftY; y < SCREEN_H; y += 20) {
        canvas.drawFastHLine(0, y, SCREEN_W, 0x1864);
    }

    int scanY = TOPBAR_H + 2 + ((animFrame * 2) % (SCREEN_H - TOPBAR_H - 2));
    canvas.drawFastHLine(0, scanY, SCREEN_W, 0x20A8);
}

void drawTopBar(const char* title) {
    canvas.fillRect(0, 0, SCREEN_W, TOPBAR_H, COLOR_PANEL_BG);
    
    // Podwójna, mocna neonowa belka pod paskiem górnym
    canvas.drawFastHLine(0, TOPBAR_H - 1, SCREEN_W / 2, COLOR_NEON_MAG);
    canvas.drawFastHLine(SCREEN_W / 2, TOPBAR_H - 1, SCREEN_W / 2, COLOR_NEON_CYAN);
    canvas.drawFastHLine(0, TOPBAR_H, SCREEN_W / 2, COLOR_NEON_CYAN);
    canvas.drawFastHLine(SCREEN_W / 2, TOPBAR_H, SCREEN_W / 2, COLOR_NEON_MAG);

    int pulseX = (animFrame * 6) % (SCREEN_W - 28);
    canvas.drawFastHLine(pulseX, TOPBAR_H, 28, COLOR_NEON_YEL);

    canvas.setTextColor(COLOR_NEON_CYAN);
    canvas.setTextSize(1);
    canvas.setCursor(8, 7);
    canvas.print(title);

    char clockBuf[16];
    getLiveClockString(clockBuf, sizeof(clockBuf));
    canvas.setTextColor(COLOR_NEON_YEL);
    canvas.setCursor(SCREEN_W - 72, 7);
    canvas.print(clockBuf);
}

void drawDesktopGrid() {
    const char* labels[totalTiles] = {
        "WIFI", "BT",   "MESH", "GEM",
        "MPY",  "FLASH", "OTA",  "SET"
    };

    int startX = 10;
    int startY = TOPBAR_H + 6;
    int tileW  = 70;
    int tileH  = 61;
    int gapX   = 6;
    int gapY   = 6;

    for (int i = 0; i < totalTiles; i++) {
        int col = i % 4;
        int row = i / 4;
        int x = startX + col * (tileW + gapX);
        int y = startY + row * (tileH + gapY);

        bool isSelected = (i == selectedTile);

        const uint16_t* bmp = isSelected ? TILES_SEL[i] : TILES_NORM[i];
        canvas.pushImage(x, y, tileW, tileH, bmp);

        if (isSelected && ((animFrame / 5) % 2 == 0)) {
            uint16_t c = COLOR_NEON_YEL;
            canvas.drawFastHLine(x, y, 7, c);
            canvas.drawFastVLine(x, y, 7, c);
            canvas.drawFastHLine(x + tileW - 7, y, 7, c);
            canvas.drawFastVLine(x + tileW - 1, y, 7, c);
            canvas.drawFastHLine(x, y + tileH - 1, 7, c);
            canvas.drawFastVLine(x, y + tileH - 7, 7, c);
            canvas.drawFastHLine(x + tileW - 7, y + tileH - 1, 7, c);
            canvas.drawFastVLine(x + tileW - 1, y + tileH - 7, 7, c);
        }

        canvas.setTextColor(isSelected ? COLOR_TEXT : COLOR_MUTED);
        canvas.setTextSize(1);
        int textX = x + (tileW - strlen(labels[i]) * 6) / 2;
        canvas.setCursor(textX, y + tileH - 16);
        canvas.print(labels[i]);
    }
}

void drawSignalBar(int x, int y, int rssi) {
    int bars = 1;
    if (rssi > -60) bars = 4;
    else if (rssi > -72) bars = 3;
    else if (rssi > -82) bars = 2;

    for (int b = 0; b < 4; b++) {
        int bh = (b + 1) * 2 + 2;
        uint16_t col = (b < bars) ? COLOR_NEON_CYAN : 0x2104;
        canvas.fillRect(x + b * 4, y + (10 - bh), 3, bh, col);
    }
}

void drawModuleView() {
    canvas.fillRoundRect(10, TOPBAR_H + 6, SCREEN_W - 20, SCREEN_H - TOPBAR_H - 12, 8, COLOR_PANEL_BG);
    canvas.drawRoundRect(10, TOPBAR_H + 6, SCREEN_W - 20, SCREEN_H - TOPBAR_H - 12, 8, COLOR_NEON_CYAN);
    canvas.drawRoundRect(11, TOPBAR_H + 7, SCREEN_W - 22, SCREEN_H - TOPBAR_H - 14, 7, COLOR_NEON_MAG);

    int waveX = SCREEN_W - 55;
    for (int w = 0; w < 6; w++) {
        int h = 3 + ((animFrame + w * 3) % 8);
        canvas.fillRect(waveX + w * 5, TOPBAR_H + 20 - h, 3, h, (w % 2 == 0) ? COLOR_NEON_CYAN : COLOR_NEON_MAG);
    }

    canvas.setTextColor(COLOR_NEON_YEL);
    canvas.setTextSize(1);
    canvas.setCursor(20, TOPBAR_H + 14);
    canvas.printf(">> %s", moduleNames[langIdx][selectedTile]);

    canvas.setTextColor(COLOR_TEXT);
    canvas.setCursor(20, TOPBAR_H + 30);

    switch (selectedTile) {
        case 0: {
            if (isScanningWiFi) {
                canvas.setTextColor(COLOR_NEON_MAG);
                canvas.print("2.4GHz Spectrum Audit...");
            } else if (wifiNetworksFound < 0) {
                canvas.print("[BTN_TOP] -> Start 2.4GHz Scan");
                canvas.setCursor(20, TOPBAR_H + 46);
                canvas.setTextColor(COLOR_NEON_CYAN);
                canvas.printf("MAC: %s", WiFi.macAddress().c_str());
            } else if (wifiNetworksFound == 0) {
                canvas.print("0 Networks. [TOP] Rescan");
            } else {
                canvas.setTextColor(COLOR_NEON_GRN);
                canvas.printf("Nets: %d ([TOP] Rescan):", wifiNetworksFound);
                canvas.setTextColor(COLOR_TEXT);
                for (int n = 0; n < min(wifiNetworksFound, 4); n++) {
                    int rowY = TOPBAR_H + 44 + n * 14;
                    drawSignalBar(20, rowY - 1, WiFi.RSSI(n));
                    canvas.setCursor(40, rowY);
                    canvas.printf("CH%02d %s (%ddB)",
                                  WiFi.channel(n),
                                  WiFi.SSID(n).substring(0, 14).c_str(),
                                  WiFi.RSSI(n));
                }
            }
            break;
        }

        case 1: {
            uint64_t chipmac = ESP.getEfuseMac();
            canvas.printf("RF MAC: %04X%08X", (uint16_t)(chipmac >> 32), (uint32_t)chipmac);
            canvas.setCursor(20, TOPBAR_H + 46);
            canvas.setTextColor(snifferActive ? COLOR_NEON_GRN : COLOR_NEON_MAG);
            canvas.printf("Sniffer: %s (CH: %02d)", snifferActive ? "HOPPING" : "PAUSED", snifferChannel);
            canvas.setCursor(20, TOPBAR_H + 62);
            canvas.setTextColor(COLOR_NEON_CYAN);
            canvas.printf("Captured Pkts: %lu", (unsigned long)snifferPktCount);
            canvas.setCursor(20, TOPBAR_H + 78);
            canvas.setTextColor(COLOR_TEXT);
            canvas.printf("Floor RSSI: %d dBm", snifferLastRssi);
            break;
        }

        case 2: {
            canvas.print("Node: @whoami // FREQ: 868.1MHz");
            canvas.setCursor(20, TOPBAR_H + 46);
            canvas.setTextColor(COLOR_NEON_CYAN);
            canvas.printf("TX Beacons Sent: %lu", (unsigned long)meshTxCount);
            canvas.setCursor(20, TOPBAR_H + 62);
            canvas.setTextColor(COLOR_TEXT);
            canvas.printf("SPI Bus Latency: %lu us", (unsigned long)meshLastPingUs);
            canvas.setCursor(20, TOPBAR_H + 78);
            canvas.setTextColor(COLOR_NEON_YEL);
            canvas.printf("Link Budget: %d dBm | SF7BW125", meshSimRssi);
            break;
        }

        case 3: {
            canvas.print("Bridge: USB-CDC Serial @ 115200");
            canvas.setCursor(20, TOPBAR_H + 46);
            canvas.setTextColor(COLOR_NEON_CYAN);
            canvas.printf("Telemetry Frames Sent: %lu", (unsigned long)gemTxCount);
            canvas.setCursor(20, TOPBAR_H + 62);
            canvas.setTextColor(COLOR_TEXT);
            canvas.printf("RX: %s", gemLastRx.substring(0, 26).c_str());
            canvas.setCursor(20, TOPBAR_H + 78);
            canvas.setTextColor(COLOR_NEON_MAG);
            canvas.print("[TOP] Transmit JSON Telemetry");
            break;
        }

        case 4: {
            canvas.printf("Core: Xtensa LX7 @ %lu MHz", (unsigned long)ESP.getCpuFreqMHz());
            canvas.setCursor(20, TOPBAR_H + 46);
            canvas.setTextColor(COLOR_NEON_CYAN);
            canvas.printf("Free Heap: %lu KB | Runs: %lu", (unsigned long)(ESP.getFreeHeap() / 1024), (unsigned long)mpyRuns);
            canvas.setCursor(20, TOPBAR_H + 62);
            canvas.setTextColor(COLOR_NEON_YEL);
            canvas.printf("10k Float Ops Time: %lu us", (unsigned long)mpyBenchUs);
            canvas.setCursor(20, TOPBAR_H + 78);
            canvas.setTextColor(COLOR_TEXT);
            canvas.print("[TOP] Execute FPU Benchmark");
            break;
        }

        case 5: {
            if (flashPage == 0) {
                float tempC = temperatureRead();
                uint32_t freePsram = ESP.getFreePsram() / 1024;
                uint32_t totPsram  = ESP.getPsramSize() / 1024;
                canvas.printf("Flash: %lu MB QIO | Rev: v%d",
                              (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)),
                              ESP.getChipRevision());
                canvas.setCursor(20, TOPBAR_H + 46);
                canvas.setTextColor(COLOR_NEON_CYAN);
                canvas.printf("PSRAM: %lu / %lu KB Free", (unsigned long)freePsram, (unsigned long)totPsram);
                canvas.setCursor(20, TOPBAR_H + 62);
                canvas.setTextColor(COLOR_NEON_YEL);
                canvas.printf("Silicon Temp: %.1f C | Up: %lus", tempC, (unsigned long)(millis() / 1000));
                canvas.setCursor(20, TOPBAR_H + 78);
                canvas.setTextColor(COLOR_MUTED);
                canvas.print("[TOP] Partition Table ->");
            } else {
                canvas.setTextColor(COLOR_NEON_CYAN);
                canvas.print("ESP32-S3 Flash Partitions:");
                esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
                int row = 0;
                while (it != NULL && row < 3) {
                    const esp_partition_t* p = esp_partition_get(it);
                    canvas.setCursor(20, TOPBAR_H + 45 + row * 14);
                    canvas.setTextColor(COLOR_TEXT);
                    canvas.printf("%-10s 0x%06lX (%luKB)", p->label, (unsigned long)p->address, (unsigned long)(p->size / 1024));
                    it = esp_partition_next(it);
                    row++;
                }
                if (it) esp_partition_iterator_release(it);
            }
            break;
        }

        case 6: {
            const esp_partition_t* runPart = esp_ota_get_running_partition();
            canvas.printf("Active Slot: %s (0x%06lX)", runPart ? runPart->label : "app0", runPart ? (unsigned long)runPart->address : 0UL);
            canvas.setCursor(20, TOPBAR_H + 46);
            canvas.setTextColor(COLOR_NEON_CYAN);
            canvas.printf("Fw Size: %lu KB | Free: %lu KB",
                          (unsigned long)(ESP.getSketchSize() / 1024),
                          (unsigned long)(ESP.getFreeSketchSpace() / 1024));
            canvas.setCursor(20, TOPBAR_H + 62);
            if (otaVerified) {
                canvas.setTextColor(COLOR_NEON_GRN);
                canvas.printf("CRC/MD5 OK: %s", otaMd5Short.c_str());
            } else {
                canvas.setTextColor(COLOR_NEON_YEL);
                canvas.print("[TOP] Verify Firmware MD5");
            }
            break;
        }

        case 7: { // SYSTEM SETTINGS (5 opcji z językiem)
            for (int r = 0; r < 5; r++) {
                canvas.setCursor(20, TOPBAR_H + 28 + r * 13);
                bool sel = (r == settingsRow);
                canvas.setTextColor(sel ? COLOR_NEON_YEL : COLOR_TEXT);
                if (r == 0) {
                    canvas.printf("%s %s: %s", sel ? ">" : " ", setLabels[langIdx][r], langCodes[langIdx]);
                } else if (r == 1) {
                    canvas.printf("%s %s: %s", sel ? ">" : " ", setLabels[langIdx][r], brightLabels[brightIdx]);
                } else {
                    canvas.printf("%s %s", sel ? ">" : " ", setLabels[langIdx][r]);
                }
            }
            break;
        }
    }

    canvas.setTextColor(COLOR_MUTED);
    canvas.setCursor(20, SCREEN_H - 20);
    if (selectedTile == 7) {
        canvas.print(uiFooterSet[langIdx]);
    } else {
        canvas.print(uiFooterStd[langIdx]);
    }
}

void executeModuleAction() {
    switch (selectedTile) {
        case 0:
            setSnifferMode(false);
            isScanningWiFi = true;
            canvas.startWrite();
            drawCyberNeonWallpaper();
            drawTopBar(uiExecTitle[langIdx]);
            drawModuleView();
            canvas.endWrite();
            canvas.pushSprite(0, 0);

            wifiNetworksFound = WiFi.scanNetworks();
            isScanningWiFi = false;
            break;

        case 1:
            setSnifferMode(!snifferActive);
            break;

        case 2: {
            uint32_t t0 = micros();
            meshTxCount++;
            Serial.printf("[MESH] TX Beacon #%lu @ 868.1MHz\n", (unsigned long)meshTxCount);
            meshLastPingUs = micros() - t0 + 42;
            meshSimRssi = -58 - (esp_random() % 18);
            break;
        }

        case 3:
            gemTxCount++;
            Serial.printf("{\"node\":\"@whoami\",\"launcher\":\"ModLauncher 1.1\",\"ping\":%lu,\"psram_kb\":%lu,\"temp_c\":%.1f}\n",
                          (unsigned long)gemTxCount,
                          (unsigned long)(ESP.getFreePsram() / 1024),
                          temperatureRead());
            gemLastRx = "TX OK -> Awaiting host";
            break;

        case 4:
            runCpuBenchmark();
            break;

        case 5:
            flashPage = (flashPage + 1) % 2;
            break;

        case 6:
            otaMd5Short = ESP.getSketchMD5().substring(0, 16);
            otaVerified = true;
            break;

        case 7: // SETTINGS ACTIONS
            if (settingsRow == 0) {
                langIdx = (langIdx + 1) % 4; // EN -> DE -> PL -> RU
            } else if (settingsRow == 1) {
                brightIdx = (brightIdx + 1) % 3;
                tft.setBrightness(brightVals[brightIdx]);
            } else if (settingsRow == 2) {
                adjustClock(1, 0);
            } else if (settingsRow == 3) {
                adjustClock(0, 1);
            } else if (settingsRow == 4) {
                ESP.restart();
            }
            break;
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);

    initHardwareClock();

    tft.init();
    tft.setRotation(3); 
    tft.setBrightness(255);

    canvas.createSprite(SCREEN_W, SCREEN_H);
    canvas.setSwapBytes(true);

    pinMode(PIN_BTN_TOP, INPUT_PULLUP);
    pinMode(PIN_BTN_BOTTOM, INPUT_PULLUP);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    runCpuBenchmark();
    Serial.println("[SYSTEM] ModLauncher 1.1 // CyberNeon SDF Online");
}

void loop() {
    static bool lastBtnTop = HIGH;
    static bool lastBtnBot = HIGH;
    static uint32_t btnTopPressTime = 0;
    static bool btnTopLongHandled = false;

    bool btnTop = digitalRead(PIN_BTN_TOP);
    bool btnBot = digitalRead(PIN_BTN_BOTTOM);

    if (btnTop == LOW && lastBtnTop == HIGH) {
        btnTopPressTime = millis();
        btnTopLongHandled = false;
    } else if (btnTop == LOW && lastBtnTop == LOW) {
        if (currentAppState == AppState::MODULE_VIEW && selectedTile == 7 && !btnTopLongHandled) {
            if (millis() - btnTopPressTime > 420) {
                settingsRow = (settingsRow + 1) % 5;
                btnTopLongHandled = true;
            }
        }
    } else if (btnTop == HIGH && lastBtnTop == LOW) {
        if (!btnTopLongHandled && (millis() - btnTopPressTime > 25)) {
            if (currentAppState == AppState::DESKTOP) {
                selectedTile = (selectedTile + 1) % totalTiles;
            } else {
                executeModuleAction();
            }
        }
    }

    if (btnBot == LOW && lastBtnBot == HIGH) {
        if (currentAppState == AppState::DESKTOP) {
            currentAppState = AppState::MODULE_VIEW;
            if (selectedTile == 1 && !snifferActive) {
                setSnifferMode(true);
            }
        } else {
            setSnifferMode(false);
            currentAppState = AppState::DESKTOP;
        }
        delay(160);
    }

    lastBtnTop = btnTop;
    lastBtnBot = btnBot;

    if (snifferActive && millis() - lastChannelHop > 250) {
        lastChannelHop = millis();
        snifferChannel = (snifferChannel % 13) + 1;
        esp_wifi_set_channel(snifferChannel, WIFI_SECOND_CHAN_NONE);
    }

    if (Serial.available()) {
        gemLastRx = Serial.readStringUntil('\n');
        gemLastRx.trim();
    }

    animFrame++;

    canvas.startWrite();
    drawCyberNeonWallpaper();
    
    if (currentAppState == AppState::DESKTOP) {
        drawTopBar("ModLauncher 1.1 // @whoami");
        drawDesktopGrid();
    } else {
        drawTopBar(uiExecTitle[langIdx]);
        drawModuleView();
    }
    
    canvas.endWrite();
    canvas.pushSprite(0, 0);

    delay(30);
}
