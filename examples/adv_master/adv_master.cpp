#include "../include/config.h"
#include <SPI.h>
#include <SD.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <M5Cardputer.h>

enum ActiveModule : uint8_t {
    MODULE_IDLE   = 0,
    MODULE_CC1101 = 1,
    MODULE_NRF24  = 2
};

enum OperationMode : uint8_t {
    MODE_STANDBY  = 0,
    MODE_SPECTRUM = 1,
    MODE_SNIFF    = 2
};

SPIClass hydraSPI(HSPI);
M5Canvas desktopCanvas(&M5Cardputer.Display);

EspNowProtocolHeader advProtoS3;
EspNowProtocolHeader advProtoEsp32U;
TelemetryPacket txPacket;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

uint32_t lastCycleMs  = 0;
uint32_t lastRenderMs = 0;

ActiveModule  activeModule       = MODULE_CC1101;
OperationMode operationMode      = MODE_SPECTRUM;
uint32_t      currentFrequencyHz = FREQ_433_92_MHZ;
uint8_t       advAnt24GState     = ANT_24GHZ_OFF; // Domyślnie WYŁĄCZONA (Safe 0V)
uint8_t       espNowChannel      = 1;
bool          sdCardMounted      = false;
bool          protocolsVerified  = false;
uint32_t      sdCardSizeMB       = 0;

bool startMenuOpen    = false;
bool rfWindowMin      = false;
int8_t startMenuIndex = 0;
const int8_t MENU_ITEMS_COUNT = 6;

// Kursor myszki Windows XP (sterowany Fn + strzałki lub bezpośrednio ; . , /)
int16_t cursorX = 222;
int16_t cursorY = 12;

const char* moduleName(ActiveModule m) {
    switch (m) {
        case MODULE_CC1101: return "CC1101 Sub-GHz";
        case MODULE_NRF24:  return "nRF24 2.4GHz";
        default:            return "IDLE";
    }
}

const char* modeName(OperationMode m) {
    switch (m) {
        case MODE_STANDBY:  return "STANDBY";
        case MODE_SPECTRUM: return "SPECTRUM";
        case MODE_SNIFF:    return "SNIFFER";
        default:            return "STANDBY";
    }
}

void isolateHydraBus() {
    digitalWrite(CC1101_CS, HIGH);
    digitalWrite(NRF24_CSN, HIGH);
    digitalWrite(SDCARD_CS, HIGH);
    digitalWrite(NRF24_CE,  LOW);
}

void setAdv24GHzAntennaSwitch(uint8_t state) {
    advAnt24GState = (state == ANT_24GHZ_ON) ? ANT_24GHZ_ON : ANT_24GHZ_OFF;
    digitalWrite(PIN_ADV_ANT_24GHZ, (advAnt24GState == ANT_24GHZ_ON) ? HIGH : LOW);
}

void cc1101WriteReg(uint8_t reg, uint8_t val) {
    digitalWrite(CC1101_CS, LOW);
    hydraSPI.transfer(reg);
    hydraSPI.transfer(val);
    digitalWrite(CC1101_CS, HIGH);
}

void cc1101Strobe(uint8_t cmd) {
    digitalWrite(CC1101_CS, LOW);
    hydraSPI.transfer(cmd);
    digitalWrite(CC1101_CS, HIGH);
}

uint8_t cc1101ReadStatusReg(uint8_t reg) {
    digitalWrite(CC1101_CS, LOW);
    hydraSPI.transfer(reg | 0xC0);
    uint8_t val = hydraSPI.transfer(0x00);
    digitalWrite(CC1101_CS, HIGH);
    return val;
}

void cc1101SetFrequency(uint32_t freqHz) {
    uint32_t freqWord = (uint32_t)(((uint64_t)freqHz << 16) / 26000000ULL);
    cc1101WriteReg(0x0D, (freqWord >> 16) & 0xFF);
    cc1101WriteReg(0x0E, (freqWord >> 8)  & 0xFF);
    cc1101WriteReg(0x0F,  freqWord        & 0xFF);
}

void nrf24WriteReg(uint8_t reg, uint8_t val) {
    digitalWrite(NRF24_CSN, LOW);
    hydraSPI.transfer(0x20 | (reg & 0x1F));
    hydraSPI.transfer(val);
    digitalWrite(NRF24_CSN, HIGH);
}

uint8_t nrf24ReadReg(uint8_t reg) {
    digitalWrite(NRF24_CSN, LOW);
    hydraSPI.transfer(reg & 0x1F);
    uint8_t val = hydraSPI.transfer(0xFF);
    digitalWrite(NRF24_CSN, HIGH);
    return val;
}

void initHydraSPIAndSD() {
    pinMode(CC1101_CS, OUTPUT);
    pinMode(NRF24_CSN, OUTPUT);
    pinMode(SDCARD_CS, OUTPUT);
    pinMode(NRF24_CE,  OUTPUT);
    pinMode(CC1101_GDO0, INPUT);
    pinMode(PIN_ADV_ANT_24GHZ, OUTPUT);

    // Domyślnie gniazdo 2.4GHz WYŁĄCZONE (0V)
    setAdv24GHzAntennaSwitch(ANT_24GHZ_OFF);
    isolateHydraBus();
    hydraSPI.begin(HYDRA_SCK, HYDRA_MISO, HYDRA_MOSI, -1);

    hydraSPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    cc1101Strobe(0x30);
    delayMicroseconds(200);
    cc1101WriteReg(0x1B, 0x07);
    hydraSPI.endTransaction();

    hydraSPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    nrf24WriteReg(0x00, 0x03);
    nrf24WriteReg(0x01, 0x00);
    nrf24WriteReg(0x06, 0x0F);
    hydraSPI.endTransaction();

    isolateHydraBus();
    sdCardMounted = SD.begin(SDCARD_CS, hydraSPI, 16000000);
    if (sdCardMounted) {
        sdCardSizeMB = (uint32_t)(SD.cardSize() / (1024ULL * 1024ULL));
        provisionDefaultSDConfigs();

        bool okS3  = loadEspNowProtocolBin("/protocols/espnow_tdisplay_s3.bin", advProtoS3);
        bool ok32U = loadEspNowProtocolBin("/protocols/espnow_esp32_u.bin", advProtoEsp32U);
        protocolsVerified = (okS3 && ok32U);

        int modCfg = readCfgValue("/config/adv_master.cfg", "active_module", "1").toInt();
        activeModule = static_cast<ActiveModule>(constrain(modCfg, 0, 2));

        int modeCfg = readCfgValue("/config/adv_master.cfg", "operation_mode", "1").toInt();
        operationMode = static_cast<OperationMode>(constrain(modeCfg, 0, 2));

        uint32_t freqCfg = (uint32_t)readCfgValue("/config/adv_master.cfg", "default_freq_hz", "433920000").toInt();
        if (freqCfg >= 300000000UL && freqCfg <= 928000000UL) currentFrequencyHz = freqCfg;

        String antCfg = readCfgValue("/config/adv_master.cfg", "ant_24ghz_mode", "OFF");
        setAdv24GHzAntennaSwitch(antCfg == "ON" ? ANT_24GHZ_ON : ANT_24GHZ_OFF);

        int chCfg = readCfgValue("/config/adv_master.cfg", "espnow_channel", "1").toInt();
        espNowChannel = (uint8_t)constrain(chCfg, 1, 13);
    }
    isolateHydraBus();
}

void saveTelemetryLogToSD() {
    isolateHydraBus();
    if (!sdCardMounted) {
        sdCardMounted = SD.begin(SDCARD_CS, hydraSPI, 16000000);
    }
    if (sdCardMounted) {
        File logFile = SD.open("/hydra_rf.log", FILE_APPEND);
        if (logFile) {
            logFile.printf("[%lu ms] MOD:%s FREQ:%lu RSSI:%d dBm ANT24G:%s\n",
                           millis(), moduleName(activeModule), currentFrequencyHz,
                           txPacket.rssiDbm,
                           (advAnt24GState == ANT_24GHZ_ON) ? "ON" : "OFF");
            logFile.close();
        }
    }
    isolateHydraBus();
}

void initEspNowLink() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(20);

    if (esp_now_init() == ESP_OK) {
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, broadcastAddress, 6);
        peerInfo.channel = espNowChannel;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
    }
}

void bootIntoBruce() {
    desktopCanvas.fillSprite(0x0015);
    desktopCanvas.fillRect(0, 0, 240, 18, 0x0010);
    desktopCanvas.setTextColor(TFT_WHITE);
    desktopCanvas.setCursor(12, 5);
    desktopCanvas.printf("Windows XP - System Shutdown");

    desktopCanvas.setCursor(20, 45);
    desktopCanvas.printf("Saving Hydra RF settings...");
    desktopCanvas.setCursor(20, 62);
    desktopCanvas.setTextColor(0xFFE0);
    desktopCanvas.printf("Switching to BRUCE FIRMWARE...");
    desktopCanvas.pushSprite(0, 0);

    M5Cardputer.Speaker.tone(784, 150);
    delay(160);
    M5Cardputer.Speaker.tone(523, 250);
    delay(300);

    isolateHydraBus();

    if (sdCardMounted || SD.begin(SDCARD_CS, hydraSPI, 16000000)) {
        String customBruce = readCfgValue("/config/adv_master.cfg", "bruce_bin_path", "/firmware/bruce.bin");
        const char* paths[] = {customBruce.c_str(), "/bruce.bin", "/firmware/bruce.bin", "/Bruce.bin"};
        for (const char* path : paths) {
            if (SD.exists(path)) {
                File bin = SD.open(path, FILE_READ);
                if (bin && bin.size() > 0x10000) {
                    size_t fileSize = bin.size();
                    bin.seek(0x10000);
                    fileSize -= 0x10000;

                    desktopCanvas.setCursor(20, 82);
                    desktopCanvas.setTextColor(0x07E0);
                    desktopCanvas.printf("Flashing %s from SD...", path);
                    desktopCanvas.pushSprite(0, 0);

                    if (Update.begin(fileSize, U_FLASH)) {
                        uint8_t buf[2048];
                        size_t written = 0;
                        while (bin.available() && written < fileSize) {
                            size_t toRead = min((size_t)sizeof(buf), fileSize - written);
                            size_t r = bin.read(buf, toRead);
                            if (r == 0) break;
                            Update.write(buf, r);
                            written += r;
                        }
                        if (Update.end(true)) {
                            bin.close();
                            ESP.restart();
                        }
                    }
                }
                if (bin) bin.close();
            }
        }
    }

    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* nextPart = esp_ota_get_next_update_partition(running);
    if (nextPart != nullptr) esp_ota_set_boot_partition(nextPart);
    delay(400);
    ESP.restart();
}

void playWindowsXPStartupSound() {
    M5Cardputer.Speaker.setVolume(160);
    M5Cardputer.Speaker.tone(622, 220); delay(230);
    M5Cardputer.Speaker.tone(466, 180); delay(190);
    M5Cardputer.Speaker.tone(830, 320); delay(330);
    M5Cardputer.Speaker.tone(622, 280); delay(290);
    M5Cardputer.Speaker.tone(932, 450);
}

void runWindowsXPBootScreen(uint32_t durationMs) {
    uint32_t startMs = millis();
    int barPos = -30;

    while (millis() - startMs < durationMs) {
        desktopCanvas.fillSprite(TFT_BLACK);
        desktopCanvas.fillRoundRect(92, 16, 22, 18, 2, 0xF100);
        desktopCanvas.fillRoundRect(117, 19, 22, 18, 2, 0x3666);
        desktopCanvas.fillRoundRect(90, 37, 22, 18, 2, 0x03DF);
        desktopCanvas.fillRoundRect(115, 40, 22, 18, 2, 0xFD20);

        desktopCanvas.setTextSize(1);
        desktopCanvas.setTextColor(0xCE79);
        desktopCanvas.setCursor(48, 63);
        desktopCanvas.printf("Microsoft");

        desktopCanvas.setTextSize(2);
        desktopCanvas.setTextColor(TFT_WHITE);
        desktopCanvas.setCursor(46, 73);
        desktopCanvas.printf("Windows");

        desktopCanvas.setTextSize(1);
        desktopCanvas.setTextColor(0xFD20);
        desktopCanvas.setCursor(134, 71);
        desktopCanvas.printf("xp");

        desktopCanvas.setTextColor(0x7BEF);
        desktopCanvas.setCursor(52, 92);
        desktopCanvas.printf("Smolinski|Labs Edition");

        const int trackX = 65, trackY = 108, trackW = 110, trackH = 11;
        desktopCanvas.drawRoundRect(trackX, trackY, trackW, trackH, 3, 0x7BEF);

        for (int b = 0; b < 3; b++) {
            int bx = trackX + 2 + barPos + (b * 9);
            if (bx >= trackX + 2 && bx + 7 <= trackX + trackW - 2) {
                desktopCanvas.fillRect(bx, trackY + 2, 7, trackH - 4, 0x24BE);
                desktopCanvas.fillRect(bx, trackY + 2, 7, 2, 0x6D7F);
            }
        }

        barPos += 4;
        if (barPos > trackW) barPos = -28;

        desktopCanvas.setTextColor(0x4A49);
        desktopCanvas.setCursor(6, 125);
        desktopCanvas.printf("Copyright (C) @whoami");

        uint32_t elapsed = (millis() - startMs) / 1000;
        desktopCanvas.setCursor(205, 125);
        desktopCanvas.printf("%lus/6s", elapsed + 1);

        desktopCanvas.pushSprite(0, 0);
        delay(30);
    }
    playWindowsXPStartupSound();
}

// Tapeta Windows XP "Bliss" (gradient nieba, białe chmury, zielone wzgórza)
void drawBlissWallpaper() {
    for (int y = 0; y < 82; y++) {
        uint16_t skyCol = desktopCanvas.color565(35 + y, 115 + (y * 3) / 2, 235);
        desktopCanvas.drawFastHLine(0, y, 240, skyCol);
    }
    // Chmury na horyzoncie
    desktopCanvas.fillCircle(36, 48, 12, 0xF7BE);
    desktopCanvas.fillCircle(50, 45, 15, TFT_WHITE);
    desktopCanvas.fillCircle(66, 50, 11, 0xEF5D);
    desktopCanvas.fillCircle(172, 34, 11, TFT_WHITE);
    desktopCanvas.fillCircle(186, 32, 14, 0xF7BE);
    desktopCanvas.fillCircle(200, 36, 10, 0xEF5D);

    // Zielone wzgórza Bliss (dalszy i bliższy plan)
    desktopCanvas.fillRect(0, 82, 240, 35, 0x35E4);
    desktopCanvas.fillCircle(190, 108, 65, 0x4665);
    desktopCanvas.fillCircle(72, 116, 78, 0x56E6);
    desktopCanvas.fillCircle(25, 118, 52, 0x3E04);
}

// Ikonka fioletowego rekina Bruce'a na pulpicie (jedyna ikona na pulpicie)
void drawBrucePurpleSharkIcon(int x, int y) {
    bool hover = (cursorX >= x && cursorX <= x + 30 && cursorY >= y && cursorY <= y + 36);
    if (hover) {
        desktopCanvas.fillRoundRect(x - 2, y - 2, 32, 38, 3, 0x1B5E);
        desktopCanvas.drawRoundRect(x - 2, y - 2, 32, 38, 3, 0x6D7F);
    }

    // Tło kafelka ikony
    desktopCanvas.fillRoundRect(x, y, 28, 24, 4, 0x2008);
    desktopCanvas.drawRoundRect(x, y, 28, 24, 4, 0xB9df);

    // Sylwetka fioletowego rekina (płetwa grzbietowa, ogon, korpus, białe zęby i oko)
    uint16_t sharkPurple = 0x98DF; // Jasny fiolet Bruce'a
    uint16_t sharkDark   = 0x6014;
    desktopCanvas.fillTriangle(x + 11, y + 3, x + 9, y + 11, x + 17, y + 11, sharkPurple); // Płetwa grzbietowa
    desktopCanvas.fillTriangle(x + 3, y + 8, x + 3, y + 18, x + 9, y + 13, sharkDark);    // Płetwa ogonowa
    desktopCanvas.fillEllipse(x + 15, y + 13, 9, 5, sharkPurple);                         // Korpus rekina
    desktopCanvas.fillTriangle(x + 19, y + 9, x + 26, y + 13, x + 19, y + 16, sharkPurple);// Pysk
    // Białe podbrzusze i zęby
    desktopCanvas.fillRect(x + 13, y + 15, 10, 2, TFT_WHITE);
    // Żółte/czerwone oko rekina
    desktopCanvas.fillCircle(x + 21, y + 11, 1, 0xFFE0);

    desktopCanvas.setTextSize(1);
    desktopCanvas.setTextColor(TFT_WHITE);
    desktopCanvas.setCursor(x, y + 26);
    desktopCanvas.printf("Bruce");
}

// Kursor myszki Windows XP (biała strzałka z czarnym obrysem)
void drawMouseCursor(int x, int y) {
    desktopCanvas.fillTriangle(x, y, x, y + 10, x + 7, y + 7, TFT_BLACK);
    desktopCanvas.fillTriangle(x + 1, y + 2, x + 1, y + 8, x + 5, y + 6, TFT_WHITE);
    desktopCanvas.drawLine(x + 3, y + 7, x + 6, y + 11, TFT_BLACK);
    desktopCanvas.drawPixel(x + 4, y + 8, TFT_WHITE);
}

void renderWindowsXPDesktop() {
    drawBlissWallpaper();

    // Tylko ikona fioletowego rekina Bruce'a na pulpicie (Hydra usunięta z pulpitu)
    drawBrucePurpleSharkIcon(6, 8);

    // Okno Hydra RF (prawdziwy styl Windows XP Luna 3D)
    if (!rfWindowMin) {
        const int winX = 40, winY = 4, winW = 196, winH = 109;
        // Cień i zewnętrzna ramka Luna Blue
        desktopCanvas.fillRoundRect(winX, winY, winW, winH, 4, 0x0193);
        desktopCanvas.fillRoundRect(winX + 1, winY + 1, winW - 2, 16, 3, 0x02B9);
        desktopCanvas.drawFastHLine(winX + 3, winY + 2, winW - 6, 0x54FF); // Luna 3D bevel
        desktopCanvas.fillRect(winX + 2, winY + 16, winW - 4, winH - 18, 0xF79E);
        desktopCanvas.drawRect(winX + 2, winY + 16, winW - 4, winH - 18, 0x9CD3);

        // Mini-ikona i tytuł na belce okna
        desktopCanvas.fillRect(winX + 5, winY + 4, 8, 8, 0x07E0);
        desktopCanvas.setTextColor(TFT_WHITE);
        desktopCanvas.setCursor(winX + 16, winY + 4);
        desktopCanvas.printf("Hydra RF - @whoami");

        // Przycisk minimalizacji [_] oraz czerwony przycisk zamknięcia [X]
        int minBtnX = winX + winW - 29, btnY = winY + 2;
        int closeBtnX = winX + winW - 15;
        desktopCanvas.fillRoundRect(minBtnX, btnY, 12, 12, 2, 0x24BE);
        desktopCanvas.drawRoundRect(minBtnX, btnY, 12, 12, 2, TFT_WHITE);
        desktopCanvas.drawFastHLine(minBtnX + 3, btnY + 9, 6, TFT_WHITE);

        bool hoverClose = (cursorX >= closeBtnX && cursorX <= closeBtnX + 12 && cursorY >= btnY && cursorY <= btnY + 12);
        desktopCanvas.fillRoundRect(closeBtnX, btnY, 12, 12, 2, hoverClose ? 0xF800 : 0xE182);
        desktopCanvas.drawRoundRect(closeBtnX, btnY, 12, 12, 2, TFT_WHITE);
        desktopCanvas.drawLine(closeBtnX + 3, btnY + 3, closeBtnX + 8, btnY + 8, TFT_WHITE);
        desktopCanvas.drawLine(closeBtnX + 8, btnY + 3, closeBtnX + 3, btnY + 8, TFT_WHITE);

        // Pasek informacji wewnątrz okna
        desktopCanvas.setTextColor(TFT_BLACK);
        desktopCanvas.setCursor(winX + 6, winY + 20);
        desktopCanvas.printf("Chip: %s", moduleName(activeModule));

        desktopCanvas.setCursor(winX + 6, winY + 31);
        desktopCanvas.printf("Mode: %s | %lu.%02luMHz", modeName(operationMode), currentFrequencyHz / 1000000UL, (currentFrequencyHz % 1000000UL) / 10000UL);

        desktopCanvas.setCursor(winX + 6, winY + 42);
        desktopCanvas.printf("RSSI:%ddBm SD:%s 2.4G:%s",
                             txPacket.rssiDbm,
                             sdCardMounted ? "OK" : "--",
                             (advAnt24GState == ANT_24GHZ_ON) ? "ON" : "OFF");

        // Oscyloskop widma RF z siatką 3D
        const int scopeX = winX + 6, scopeY = winY + 53, scopeW = 184, scopeH = 50;
        desktopCanvas.fillRect(scopeX, scopeY, scopeW, scopeH, TFT_BLACK);
        desktopCanvas.drawRect(scopeX, scopeY, scopeW, scopeH, 0x4A49);
        for (int gy = scopeY + 12; gy < scopeY + scopeH; gy += 12) {
            desktopCanvas.drawFastHLine(scopeX + 1, gy, scopeW - 2, 0x1082);
        }

        for (int i = 0; i < 58; i++) {
            int h = (txPacket.spectrumBins[i] * (scopeH - 4)) / 255;
            if (h > 0) {
                uint16_t barCol = (txPacket.spectrumBins[i] > 160) ? 0xFFE0 : 0x07E0;
                desktopCanvas.fillRect(scopeX + 4 + (i * 3), scopeY + scopeH - 2 - h, 2, h, barCol);
            }
        }
    }

    // Menu Start Windows XP Luna (6 wybieralnych opcji)
    if (startMenuOpen) {
        const int smX = 0, smY = 12, smW = 158, smH = 105;
        desktopCanvas.fillRoundRect(smX, smY, smW, smH, 4, 0x02B9);
        desktopCanvas.drawRoundRect(smX, smY, smW, smH, 4, 0x6D7F);
        desktopCanvas.fillRect(smX + 2, smY + 18, 104, smH - 33, TFT_WHITE);
        desktopCanvas.fillRect(smX + 106, smY + 18, 50, smH - 33, 0xD69A);

        // Belka użytkownika @whoami | CEO
        desktopCanvas.fillRoundRect(smX + 4, smY + 2, 13, 13, 2, 0xFD20);
        desktopCanvas.drawRoundRect(smX + 4, smY + 2, 13, 13, 2, TFT_WHITE);
        desktopCanvas.setTextColor(TFT_WHITE);
        desktopCanvas.setCursor(smX + 21, smY + 5);
        desktopCanvas.printf("@whoami | CEO");

        const char* menuLabels[MENU_ITEMS_COUNT] = {
            "1: CC1101 SubGHz",
            "2: nRF24 2.4GHz",
            "3: Band 433/868",
            "4: Mode Spec/Sniff",
            (advAnt24GState == ANT_24GHZ_ON) ? "5: 2.4G Ant [ON]" : "5: 2.4G Ant [OFF]",
            "6: Run Bruce OS"
        };

        for (int i = 0; i < MENU_ITEMS_COUNT; i++) {
            int itemY = smY + 20 + (i * 11);
            bool sel = (i == startMenuIndex);
            if (sel) {
                desktopCanvas.fillRect(smX + 3, itemY - 1, 102, 10, 0x2B5E);
                desktopCanvas.setTextColor(TFT_WHITE);
            } else {
                desktopCanvas.setTextColor((i == 5) ? 0x901A : TFT_BLACK);
            }
            desktopCanvas.setCursor(smX + 5, itemY);
            desktopCanvas.printf("%.17s", menuLabels[i]);
        }

        // Prawa kolumna informacyjna XP
        desktopCanvas.setTextColor(0x0193);
        desktopCanvas.setCursor(smX + 109, smY + 24);
        desktopCanvas.printf("SD Card");
        desktopCanvas.setTextColor(TFT_BLACK);
        desktopCanvas.setCursor(smX + 109, smY + 35);
        desktopCanvas.printf(sdCardMounted ? "MOUNTED" : "NONE");
        desktopCanvas.setCursor(smX + 109, smY + 52);
        desktopCanvas.printf("2.4G Pin");
        desktopCanvas.setTextColor((advAnt24GState == ANT_24GHZ_ON) ? 0x03E0 : 0xC800);
        desktopCanvas.setCursor(smX + 109, smY + 63);
        desktopCanvas.printf((advAnt24GState == ANT_24GHZ_ON) ? "ON (3V3)" : "OFF (0V)");

        // Dolna belka wyłączania w menu Start
        desktopCanvas.fillRect(smX + 2, smY + smH - 14, smW - 4, 12, 0x1B5E);
        desktopCanvas.fillRoundRect(smX + 68, smY + smH - 13, 9, 9, 1, 0xE182);
        desktopCanvas.setTextColor(TFT_WHITE);
        desktopCanvas.setCursor(smX + 80, smY + smH - 11);
        desktopCanvas.printf("Switch to Bruce");
    }

    // Pasek zadań Windows XP (Czysty, bez tekstów skrótów)
    const int tbY = 117;
    desktopCanvas.fillRect(0, tbY, 240, 18, 0x129B);
    desktopCanvas.drawFastHLine(0, tbY, 240, 0x4C7F);

    // Zielony przycisk [start] z 4-kolorową flagą Windows
    desktopCanvas.fillRoundRect(0, tbY + 1, 48, 17, 4, startMenuOpen ? 0x1B82 : 0x35C4);
    desktopCanvas.fillRect(4, tbY + 5, 3, 3, 0xF100);
    desktopCanvas.fillRect(8, tbY + 5, 3, 3, 0x3666);
    desktopCanvas.fillRect(4, tbY + 9, 3, 3, 0x03DF);
    desktopCanvas.fillRect(8, tbY + 9, 3, 3, 0xFD20);
    desktopCanvas.setTextColor(TFT_WHITE);
    desktopCanvas.setCursor(14, tbY + 5);
    desktopCanvas.printf("start");

    // Karta aktywnego programu na pasku zadań (jak w prawdziwym Windows XP)
    desktopCanvas.fillRoundRect(52, tbY + 2, 102, 14, 2, rfWindowMin ? 0x1A3C : 0x0953);
    desktopCanvas.drawRoundRect(52, tbY + 2, 102, 14, 2, 0x4C7F);
    desktopCanvas.fillRect(56, tbY + 5, 6, 6, 0x07E0);
    desktopCanvas.setCursor(65, tbY + 5);
    desktopCanvas.printf("Hydra RF Scope");

    // Zasobnik systemowy (System Tray) po prawej stronie
    desktopCanvas.fillRect(158, tbY + 1, 82, 17, 0x0C5E);
    desktopCanvas.drawFastVLine(158, tbY + 1, 17, 0x4C7F);
    uint32_t upSec = millis() / 1000;
    desktopCanvas.setTextColor((advAnt24GState == ANT_24GHZ_ON) ? 0x07E0 : 0xFFE0);
    desktopCanvas.setCursor(162, tbY + 5);
    desktopCanvas.printf("2.4:%s", (advAnt24GState == ANT_24GHZ_ON) ? "ON " : "OFF");
    desktopCanvas.setTextColor(TFT_WHITE);
    desktopCanvas.setCursor(206, tbY + 5);
    desktopCanvas.printf("%02lu:%02lu", (upSec / 60) % 60, upSec % 60);

    // Renderowanie kursora myszki na samej górze warstwy
    drawMouseCursor(cursorX, cursorY);

    desktopCanvas.pushSprite(0, 0);
}

void performSpectrumSweep() {
    isolateHydraBus();
    if (operationMode == MODE_STANDBY || activeModule == MODULE_IDLE) {
        memset(txPacket.spectrumBins, 0, sizeof(txPacket.spectrumBins));
        txPacket.rssiDbm = -110;
        return;
    }

    int16_t peakRssiDbm = -120;
    if (activeModule == MODULE_CC1101) {
        hydraSPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
        uint32_t startFreq = currentFrequencyHz - (32UL * 25000UL);

        for (uint8_t i = 0; i < RF_SWEEP_POINTS; ++i) {
            cc1101Strobe(0x36);
            cc1101SetFrequency(startFreq + ((uint32_t)i * 25000UL));
            cc1101Strobe(0x34);
            delayMicroseconds(90);

            uint8_t rawRssi = cc1101ReadStatusReg(0x34);
            int16_t rssiDbm = (rawRssi >= 128) ? (((int16_t)rawRssi - 256) / 2 - 74) : ((int16_t)rawRssi / 2 - 74);
            if (rssiDbm > peakRssiDbm) peakRssiDbm = rssiDbm;

            int32_t mapped = (rssiDbm + 110) * 255 / 80;
            txPacket.spectrumBins[i] = (uint8_t)constrain(mapped, 0, 255);
        }
        cc1101Strobe(0x36);
        hydraSPI.endTransaction();
    } else if (activeModule == MODULE_NRF24) {
        hydraSPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
        for (uint8_t i = 0; i < RF_SWEEP_POINTS; ++i) {
            nrf24WriteReg(0x05, i * 2);
            digitalWrite(NRF24_CE, HIGH);
            delayMicroseconds(130);
            digitalWrite(NRF24_CE, LOW);

            uint8_t rpd = nrf24ReadReg(0x09) & 0x01;
            uint16_t cur = txPacket.spectrumBins[i];
            if (rpd) cur = (cur + 180 > 255) ? 255 : cur + 180;
            else cur = (cur > 20) ? cur - 20 : 8;
            txPacket.spectrumBins[i] = (uint8_t)cur;

            int16_t estDbm = -95 + ((int16_t)cur * 60 / 255);
            if (estDbm > peakRssiDbm) peakRssiDbm = estDbm;
        }
        hydraSPI.endTransaction();
    }
    txPacket.rssiDbm = peakRssiDbm;
    isolateHydraBus();
}

void sendTelemetry() {
    txPacket.magic         = 0xA5;
    txPacket.activeModule  = activeModule;
    txPacket.operationMode = operationMode;
    txPacket.frequencyHz   = currentFrequencyHz;
    txPacket.ant24GState   = advAnt24GState;
    txPacket.sdStatus      = sdCardMounted ? 1 : 0;
    txPacket.checksum      = calculateXOR((const uint8_t*)&txPacket, sizeof(TelemetryPacket) - 1);
    esp_now_send(broadcastAddress, (const uint8_t*)&txPacket, sizeof(TelemetryPacket));
}

void executeMenuAction(int8_t idx) {
    switch (idx) {
        case 0: activeModule = MODULE_CC1101; rfWindowMin = false; break;
        case 1: activeModule = MODULE_NRF24;  rfWindowMin = false; break;
        case 2: currentFrequencyHz = (currentFrequencyHz == FREQ_433_92_MHZ) ? FREQ_868_35_MHZ : FREQ_433_92_MHZ; rfWindowMin = false; break;
        case 3: operationMode = static_cast<OperationMode>((static_cast<uint8_t>(operationMode) + 1U) % 3U); rfWindowMin = false; break;
        case 4: setAdv24GHzAntennaSwitch(advAnt24GState == ANT_24GHZ_ON ? ANT_24GHZ_OFF : ANT_24GHZ_ON); break;
        case 5: bootIntoBruce(); break;
    }
    startMenuOpen = false;
}

void handleCursorEnterClick() {
    // 1. Kliknięcie w przycisk [start] (lewy dolny róg)
    if (cursorX >= 0 && cursorX <= 48 && cursorY >= 117) {
        startMenuOpen = !startMenuOpen;
        return;
    }
    // 2. Kliknięcie w kartę [Hydra RF Scope] na pasku zadań
    if (cursorX >= 52 && cursorX <= 154 && cursorY >= 117) {
        rfWindowMin = !rfWindowMin;
        startMenuOpen = false;
        return;
    }
    // 3. Kliknięcie w opcję otwartego menu Start
    if (startMenuOpen && cursorX >= 2 && cursorX <= 106 && cursorY >= 32 && cursorY <= 98) {
        int clickedIdx = (cursorY - 32) / 11;
        if (clickedIdx >= 0 && clickedIdx < MENU_ITEMS_COUNT) {
            executeMenuAction(clickedIdx);
            return;
        }
    }
    // 4. Kliknięcie w czerwony przycisk [X] lub [_] okna Hydra RF
    if (!rfWindowMin && cursorY >= 4 && cursorY <= 20 && cursorX >= 205 && cursorX <= 236) {
        rfWindowMin = true;
        return;
    }
    // 5. Kliknięcie w fioletowego rekina Bruce na pulpicie (lewy górny róg)
    if (cursorX >= 4 && cursorX <= 38 && cursorY >= 6 && cursorY <= 46) {
        bootIntoBruce();
        return;
    }
    // Domyślnie jeśli menu Start jest otwarte -> wykonaj zaznaczoną pozycję
    if (startMenuOpen) {
        executeMenuAction(startMenuIndex);
    }
}

void handleKeyboard() {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;

    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

    for (char key : status.word) {
        // Sterowanie kursorem myszki (Fn + strzałki lub bezpośrednio ; . , /)
        if (key == ';' || key == ':') {
            if (startMenuOpen && !status.fn) {
                startMenuIndex = (startMenuIndex - 1 + MENU_ITEMS_COUNT) % MENU_ITEMS_COUNT;
            } else {
                cursorY = max(2, cursorY - 8);
            }
        } else if (key == '.' || key == '>') {
            if (startMenuOpen && !status.fn) {
                startMenuIndex = (startMenuIndex + 1) % MENU_ITEMS_COUNT;
            } else {
                cursorY = min(131, cursorY + 8);
            }
        } else if (key == ',' || key == '<') {
            cursorX = max(2, cursorX - 10);
        } else if (key == '/' || key == '?') {
            cursorX = min(235, cursorX + 10);
        } else {
            switch (key) {
                case 'm': case 'M': startMenuOpen = !startMenuOpen; break;
                case '1': executeMenuAction(0); break;
                case '2': executeMenuAction(1); break;
                case '3': case 'f': case 'F': executeMenuAction(2); break;
                case '4': executeMenuAction(3); break;
                case '5': case 'a': case 'A': executeMenuAction(4); break;
                case '6': case 'b': case 'B': bootIntoBruce(); break;
                case 's': case 'S': saveTelemetryLogToSD(); break;
                case 'w': case 'W': rfWindowMin = !rfWindowMin; break;
            }
        }
    }

    if (status.enter) {
        handleCursorEnterClick();
    }

    txPacket.activeModule  = activeModule;
    txPacket.operationMode = operationMode;
    txPacket.frequencyHz   = currentFrequencyHz;
    txPacket.ant24GState   = advAnt24GState;
    txPacket.sdStatus      = sdCardMounted ? 1 : 0;
}

void setup() {
    Serial.begin(115200);
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);

    desktopCanvas.setColorDepth(16);
    desktopCanvas.createSprite(240, 135);

    initHydraSPIAndSD();
    initEspNowLink();

    memset(&txPacket, 0, sizeof(txPacket));
    txPacket.magic         = 0xA5;
    txPacket.activeModule  = activeModule;
    txPacket.operationMode = operationMode;
    txPacket.frequencyHz   = currentFrequencyHz;
    txPacket.rssiDbm       = -93;
    txPacket.ant24GState   = advAnt24GState;
    txPacket.sdStatus      = sdCardMounted ? 1 : 0;

    runWindowsXPBootScreen(XP_BOOT_TIME_MS);
}

void loop() {
    handleKeyboard();
    if ((millis() - lastCycleMs) >= ESP_NOW_PERIOD_MS) {
        lastCycleMs = millis();
        performSpectrumSweep();
        sendTelemetry();
    }
    if ((millis() - lastRenderMs) >= 50U) {
        lastRenderMs = millis();
        renderWindowsXPDesktop();
    }
}
