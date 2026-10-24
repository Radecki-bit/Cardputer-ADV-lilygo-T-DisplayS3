#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include "config.h"
#include "cyber_assets.h"

// Paleta CyberNeon & Intro Red
const uint16_t COLOR_BG_DARK     = 0x0822; // #090412
const uint16_t COLOR_PANEL_BG    = 0x1065; 
const uint16_t COLOR_NEON_CYAN   = 0x07FF; 
const uint16_t COLOR_NEON_MAG    = 0xF81F; 
const uint16_t COLOR_NEON_YEL    = 0xFFE0; 
const uint16_t COLOR_INTRO_RED   = 0xF800; // Krwawa czerwień LAUNCHER
const uint16_t COLOR_TEXT        = 0xFFFF; 
const uint16_t COLOR_MUTED       = 0x9CD3; 

LGFX_Sprite canvas(&tft);

enum class AppState {
    DESKTOP,
    MODULE_VIEW
};

AppState currentAppState = AppState::DESKTOP;

const char* allLabels[8] = {
    "WIFI", "BT", "MESH", "GEM",
    "MPY", "FLASH", "OTA", "SET"
};

const char* allModuleNames[8] = {
    "WIFI SCANNER", "BLUETOOTH", "MESH LORA", "GEMINI AI",
    "MicroPython",  "FLASH SYS", "OTA UPDATE", "SETTINGS"
};

int activeModules[16] = {0, 1, 2, 3, 4, 5, 6, 7};
int activeCount = 8;
int selectedSlot = 0; 

const uint8_t brightnessLevels[4] = {255, 110, 25, 0}; 
const char* brightnessNames[4] = {"100% MAX", "45% MED", "10% NIGHT", "0% STEALTH"};
int brightnessIdx = 0;

int wifiNetworksFound = -1;
bool isScanningWiFi = false;
bool ntpSynced = false;
int moduleSubOption = 0;

void initInternalClock() {
    struct tm tm_init;
    tm_init.tm_year = 2026 - 1900;
    tm_init.tm_mon = 9; 
    tm_init.tm_mday = 24;
    tm_init.tm_hour = 9;
    tm_init.tm_min = 12;
    tm_init.tm_sec = 0;
    time_t t = mktime(&tm_init);
    struct timeval now = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&now, NULL);
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

void getFormattedTime(char* buf, size_t len) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 5)) {
        snprintf(buf, len, "--:-- --.--");
        return;
    }
    const char sep = (timeinfo.tm_sec % 2 == 0) ? ':' : ' ';
    snprintf(buf, len, "%02d%c%02d %02d.%02d",
             timeinfo.tm_hour, sep, timeinfo.tm_min,
             timeinfo.tm_mday, timeinfo.tm_mon + 1);
}

void applyStealthHardware() {
    tft.setBrightness(brightnessLevels[brightnessIdx]);
    pinMode(38, OUTPUT);
    digitalWrite(38, LOW);
}

void uninstallModule(int slot) {
    int modId = activeModules[slot];
    if (modId == 7 || activeCount <= 1) return; 
    for (int i = slot; i < activeCount - 1; i++) {
        activeModules[i] = activeModules[i + 1];
    }
    activeCount--;
    if (selectedSlot >= activeCount) {
        selectedSlot = activeCount - 1;
    }
}

void restoreAllModules() {
    for (int i = 0; i < 8; i++) activeModules[i] = i;
    activeCount = 8;
}

// Wierna animacja startowa: LAUNCHER (czerwień) -> Glitch -> created by @whoami -> Dym[cite: 9]
void runBootSplashDemo() {
    // Faza 1: Czerń i pojawienie się napisu LAUNCHER w stylu grunge/czerwieni
    for (int i = 0; i <= 20; i++) {
        canvas.fillScreen(0x0000);
        canvas.setTextSize(3);
        canvas.setTextColor(COLOR_INTRO_RED);
        canvas.setCursor(30, 70);
        canvas.print("LAUNCHER");
        canvas.pushSprite(0, 0);
        delay(35);
    }

    // Faza 2: Efekt zakłóceń / glitch linii na dole
    for (int g = 0; g < 5; g++) {
        canvas.fillScreen(0x0000);
        canvas.setTextSize(3);
        canvas.setTextColor(g % 2 == 0 ? COLOR_INTRO_RED : 0xC000);
        canvas.setCursor(30 + (g % 2 == 0 ? 1 : -1), 70);
        canvas.print("LAUNCHER");
        
        // Paski glitch
        canvas.drawFastHLine(20, 130 + g * 4, SCREEN_W - 40, COLOR_NEON_MAG);
        canvas.drawFastHLine(60, 135 + g * 2, 120, COLOR_NEON_CYAN);
        
        canvas.pushSprite(0, 0);
        delay(60);
    }

    // Faza 3: Pojawienie się podpisu "created by @whoami"
    for (int step = 0; step <= 15; step++) {
        canvas.fillScreen(0x0000);
        canvas.setTextSize(3);
        canvas.setTextColor(COLOR_INTRO_RED);
        canvas.setCursor(30, 70);
        canvas.print("LAUNCHER");

        canvas.setTextSize(1);
        canvas.setTextColor(COLOR_TEXT);
        canvas.setCursor(180, 115);
        canvas.print("created by @whoami");

        canvas.pushSprite(0, 0);
        delay(30);
    }

    // Faza 4: Efekt dymu / chmury przelatującej z lewej strony
    for (int smokeX = -120; smokeX < SCREEN_W + 50; smokeX += 12) {
        canvas.fillScreen(0x0000);
        canvas.setTextSize(3);
        canvas.setTextColor(COLOR_INTRO_RED);
        canvas.setCursor(30, 70);
        canvas.print("LAUNCHER");

        canvas.setTextSize(1);
        canvas.setTextColor(COLOR_TEXT);
        canvas.setCursor(180, 115);
        canvas.print("created by @whoami");

        // Symulacja cząsteczek dymu
        for (int d = 0; d < 6; d++) {
            int bx = smokeX + d * 22;
            int by = 60 + (d % 3) * 20 + (d * 5) % 15;
            canvas.fillCircle(bx, by, 25 - d * 2, 0x4208); // Szary dym
            canvas.fillCircle(bx + 10, by - 5, 18, 0x2104);
        }

        canvas.pushSprite(0, 0);
        delay(20);
    }
    delay(400);
}

void drawCyberNeonWallpaper() {
    canvas.fillScreen(COLOR_BG_DARK);
    for (int x = 0; x < SCREEN_W; x += 20) {
        canvas.drawFastVLine(x, 0, SCREEN_H, 0x1043);
    }
    for (int y = 0; y < SCREEN_H; y += 20) {
        canvas.drawFastHLine(0, y, SCREEN_W, 0x1043);
    }
}

void drawTopBar(const char* title) {
    canvas.fillRect(0, 0, SCREEN_W, TOPBAR_H, COLOR_PANEL_BG);
    canvas.drawFastHLine(0, TOPBAR_H, SCREEN_W / 2, COLOR_NEON_CYAN);
    canvas.drawFastHLine(SCREEN_W / 2, TOPBAR_H, SCREEN_W / 2, COLOR_NEON_MAG);

    canvas.setTextColor(COLOR_NEON_CYAN);
    canvas.setTextSize(1);
    canvas.setCursor(8, 7);
    canvas.print(title);

    canvas.setTextColor(COLOR_MUTED);
    canvas.setCursor(142, 7);
    canvas.printf("[%s]", brightnessIdx == 3 ? "STEALTH" : (ntpSynced ? "NTP OK" : "RTC"));

    char timeBuf[20];
    getFormattedTime(timeBuf, sizeof(timeBuf));
    canvas.setTextColor(COLOR_NEON_YEL);
    canvas.setCursor(SCREEN_W - 84, 7);
    canvas.print(timeBuf);
}

void drawDesktopGrid() {
    int startX = 10;
    int startY = TOPBAR_H + 5;
    int tileW  = 70;
    int tileH  = 61;
    int gapX   = 6;
    int gapY   = 5;

    for (int slot = 0; slot < activeCount && slot < 8; slot++) {
        int modId = activeModules[slot];
        int col = slot % 4;
        int row = slot / 4;
        int x = startX + col * (tileW + gapX);
        int y = startY + row * (tileH + gapY);

        bool isSelected = (slot == selectedSlot);

        const uint16_t* bmp = isSelected ? TILES_SEL[modId] : TILES_NORM[modId];
        canvas.pushImage(x, y, tileW, tileH, bmp);

        if (isSelected) {
            canvas.drawRoundRect(x + 1, y + 1, tileW - 2, tileH - 2, 11, COLOR_NEON_YEL);
            canvas.drawRoundRect(x + 2, y + 2, tileW - 4, tileH - 4, 10, COLOR_TEXT);
        }

        canvas.setTextColor(isSelected ? COLOR_NEON_YEL : COLOR_MUTED);
        canvas.setTextSize(1);
        const char* lbl = allLabels[modId];
        int textX = x + (tileW - strlen(lbl) * 6) / 2;
        canvas.setCursor(textX, y + tileH - 16);
        canvas.print(lbl);
    }

    canvas.fillCircle(SCREEN_W / 2 - 6, SCREEN_H - 3, 2, COLOR_NEON_CYAN);
    canvas.drawCircle(SCREEN_W / 2 + 6, SCREEN_H - 3, 2, COLOR_MUTED);
}

void drawModuleView() {
    int modId = activeModules[selectedSlot];

    canvas.fillRoundRect(10, TOPBAR_H + 6, SCREEN_W - 20, SCREEN_H - TOPBAR_H - 12, 8, COLOR_PANEL_BG);
    canvas.drawRoundRect(10, TOPBAR_H + 6, SCREEN_W - 20, SCREEN_H - TOPBAR_H - 12, 8, COLOR_NEON_CYAN);
    canvas.drawRoundRect(11, TOPBAR_H + 7, SCREEN_W - 22, SCREEN_H - TOPBAR_H - 14, 7, COLOR_NEON_MAG);

    canvas.setTextColor(COLOR_NEON_YEL);
    canvas.setTextSize(1);
    canvas.setCursor(20, TOPBAR_H + 14);
    canvas.printf("%s", allModuleNames[modId]);

    canvas.setTextColor(COLOR_TEXT);
    canvas.setCursor(20, TOPBAR_H + 30);

    switch (modId) {
        case 0: // WIFI
            if (isScanningWiFi) {
                canvas.setTextColor(COLOR_NEON_MAG);
                canvas.print("Scanning 2.4GHz networks...");
            } else if (wifiNetworksFound < 0) {
                canvas.print("Status: Ready to Scan");
                canvas.setCursor(20, TOPBAR_H + 44);
                canvas.print("Top Action: Run 2.4GHz Audit");
            } else {
                canvas.printf("Networks found: %d", wifiNetworksFound);
                for (int n = 0; n < min(wifiNetworksFound, 3); n++) {
                    canvas.setCursor(20, TOPBAR_H + 44 + n * 12);
                    canvas.printf("%d:%s (%ddBm)", n + 1, WiFi.SSID(n).substring(0, 14).c_str(), WiFi.RSSI(n));
                }
            }
            break;

        case 7: // SETTINGS
            canvas.setTextColor(moduleSubOption == 0 ? COLOR_NEON_YEL : COLOR_TEXT);
            canvas.printf("%s 1. LED/Screen: %s", moduleSubOption == 0 ? ">" : " ", brightnessNames[brightnessIdx]);

            canvas.setCursor(20, TOPBAR_H + 44);
            canvas.setTextColor(moduleSubOption == 1 ? COLOR_NEON_YEL : COLOR_TEXT);
            canvas.printf("%s 2. Clock +1h (Manual Sync)", moduleSubOption == 1 ? ">" : " ");

            canvas.setCursor(20, TOPBAR_H + 58);
            canvas.setTextColor(moduleSubOption == 2 ? COLOR_NEON_YEL : COLOR_TEXT);
            canvas.printf("%s 3. Clock +5m (Manual Sync)", moduleSubOption == 2 ? ">" : " ");

            canvas.setCursor(20, TOPBAR_H + 72);
            canvas.setTextColor(moduleSubOption == 3 ? COLOR_NEON_YEL : COLOR_TEXT);
            canvas.printf("%s 4. Restore Tiles (%d/8) & Demo", moduleSubOption == 3 ? ">" : " ", activeCount);
            break;

        default:
            canvas.printf("Module Active: %s", allLabels[modId]);
            canvas.setCursor(20, TOPBAR_H + 46);
            canvas.printf("Free PSRAM: %d KB | 16MB Flash", ESP.getFreePsram() / 1024);
            canvas.setCursor(20, TOPBAR_H + 62);
            canvas.setTextColor(moduleSubOption == 1 ? COLOR_NEON_MAG : COLOR_MUTED);
            canvas.printf("[BTN_TOP] Mode: %s", moduleSubOption == 1 ? "UNINSTALL TILE (Click BOT)" : "Normal Operation");
            break;
    }

    canvas.setTextColor(COLOR_MUTED);
    canvas.setCursor(20, SCREEN_H - 20);
    if (modId == 7) {
        canvas.print("[TOP] Next Option | [BOT] Execute / Hold:Exit");
    } else {
        canvas.print("[TOP] Action/Uninstall | [BOT] Back");
    }
}

void setup() {
    Serial.begin(115200);
    delay(150);

    initInternalClock();

    tft.init();
    tft.setRotation(3);
    tft.setBrightness(255);

    canvas.createSprite(SCREEN_W, SCREEN_H);
    canvas.setSwapBytes(true);

    pinMode(PIN_BTN_TOP, INPUT_PULLUP);
    pinMode(PIN_BTN_BOTTOM, INPUT_PULLUP);

    applyStealthHardware();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Uruchomienie dokładnej animacji startowej LAUNCHER + @whoami z wideo
    runBootSplashDemo();
}

void loop() {
    static bool lastBtnTop = HIGH;
    static bool lastBtnBot = HIGH;
    static unsigned long botPressStart = 0;

    bool btnTop = digitalRead(PIN_BTN_TOP);
    bool btnBot = digitalRead(PIN_BTN_BOTTOM);

    if (brightnessIdx == 3 && ((btnTop == LOW && lastBtnTop == HIGH) || (btnBot == LOW && lastBtnBot == HIGH))) {
        brightnessIdx = 0;
        applyStealthHardware();
        lastBtnTop = btnTop;
        lastBtnBot = btnBot;
        delay(200);
        return;
    }

    if (btnTop == LOW && lastBtnTop == HIGH) {
        if (currentAppState == AppState::DESKTOP) {
            selectedSlot = (selectedSlot + 1) % activeCount;
        } else {
            int modId = activeModules[selectedSlot];
            if (modId == 0) { 
                isScanningWiFi = true;
                canvas.startWrite();
                drawCyberNeonWallpaper();
                drawTopBar("WIFI MODULE");
                drawModuleView();
                canvas.endWrite();
                canvas.pushSprite(0, 0);

                wifiNetworksFound = WiFi.scanNetworks();
                isScanningWiFi = false;
            } else if (modId == 7) { 
                moduleSubOption = (moduleSubOption + 1) % 4;
            } else {
                moduleSubOption = (moduleSubOption + 1) % 2;
            }
        }
        delay(170);
    }

    if (btnBot == LOW && lastBtnBot == HIGH) {
        botPressStart = millis();
    }

    if (btnBot == HIGH && lastBtnBot == LOW) {
        unsigned long pressDuration = millis() - botPressStart;
        if (currentAppState == AppState::DESKTOP) {
            if (pressDuration > 700) {
                brightnessIdx = (brightnessIdx + 1) % 4;
                applyStealthHardware();
            } else {
                currentAppState = AppState::MODULE_VIEW;
                moduleSubOption = 0;
            }
        } else {
            int modId = activeModules[selectedSlot];
            if (modId == 7) {
                if (pressDuration > 600) {
                    currentAppState = AppState::DESKTOP;
                } else {
                    if (moduleSubOption == 0) {
                        brightnessIdx = (brightnessIdx + 1) % 4;
                        applyStealthHardware();
                    } else if (moduleSubOption == 1) {
                        struct timeval tv;
                        gettimeofday(&tv, NULL);
                        tv.tv_sec += 3600; 
                        settimeofday(&tv, NULL);
                    } else if (moduleSubOption == 2) {
                        struct timeval tv;
                        gettimeofday(&tv, NULL);
                        tv.tv_sec += 300; 
                        settimeofday(&tv, NULL);
                    } else if (moduleSubOption == 3) {
                        restoreAllModules();
                        runBootSplashDemo();
                        currentAppState = AppState::DESKTOP;
                    }
                }
            } else {
                if (moduleSubOption == 1) {
                    uninstallModule(selectedSlot);
                }
                currentAppState = AppState::DESKTOP;
            }
        }
        delay(150);
    }

    lastBtnTop = btnTop;
    lastBtnBot = btnBot;

    canvas.startWrite();
    drawCyberNeonWallpaper();

    if (currentAppState == AppState::DESKTOP) {
        drawTopBar("CYBERNEON OS");
        drawDesktopGrid();
    } else {
        drawTopBar("SYSTEM MODULE");
        drawModuleView();
    }

    canvas.endWrite();
    canvas.pushSprite(0, 0);

    delay(30);
}
