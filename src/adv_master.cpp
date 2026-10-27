#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <M5Cardputer.h>

#define HYDRA_SCK         40
#define HYDRA_MISO        39
#define HYDRA_MOSI        14
#define CC1101_CS         13
#define CC1101_GDO0       5
#define NRF24_CSN         6
#define NRF24_CE          4
#define SDCARD_CS         12

#define FREQ_433_92_MHZ   433920000UL
#define FREQ_868_35_MHZ   868350000UL
#define RF_SWEEP_POINTS   64U
#define ESP_NOW_PERIOD_MS 33U
#define XP_BOOT_TIME_MS   8000UL

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

#pragma pack(push, 1)
struct TelemetryPacket {
    uint8_t  magic;
    uint8_t  activeModule;
    uint8_t  operationMode;
    uint32_t frequencyHz;
    int16_t  rssiDbm;
    uint8_t  spectrumBins[64];
    uint8_t  checksum;
};
#pragma pack(pop)

SPIClass hydraSPI(HSPI);
M5Canvas desktopCanvas(&M5Cardputer.Display);

TelemetryPacket txPacket;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

uint32_t lastCycleMs  = 0;
uint32_t lastRenderMs = 0;

ActiveModule  activeModule       = MODULE_CC1101;
OperationMode operationMode      = MODE_SPECTRUM;
uint32_t      currentFrequencyHz = FREQ_433_92_MHZ;

bool startMenuOpen    = false;
bool rfWindowMin      = false;
int8_t startMenuIndex = 0;
const int8_t MENU_ITEMS_COUNT = 5;

uint8_t calculateXOR(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) crc ^= data[i];
    return crc;
}

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

void initHydraSPI() {
    pinMode(CC1101_CS, OUTPUT);
    pinMode(NRF24_CSN, OUTPUT);
    pinMode(SDCARD_CS, OUTPUT);
    pinMode(NRF24_CE,  OUTPUT);
    pinMode(CC1101_GDO0, INPUT);

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
}

void initEspNowLink() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(20);

    if (esp_now_init() == ESP_OK) {
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, broadcastAddress, 6);
        peerInfo.channel = 1;
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

    if (SD.begin(SDCARD_CS, hydraSPI, 16000000)) {
        const char* paths[] = {"/bruce.bin", "/firmware/bruce.bin", "/Bruce.bin"};
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
        desktopCanvas.printf("%lus/8s", elapsed + 1);

        desktopCanvas.pushSprite(0, 0);
        delay(30);
    }
    playWindowsXPStartupSound();
}

void renderWindowsXPDesktop() {
    desktopCanvas.fillRect(0, 0, 240, 78, 0x2D7F);
    desktopCanvas.fillRect(0, 78, 240, 39, 0x45E5);
    desktopCanvas.fillCircle(65, 95, 38, 0x5686);

    desktopCanvas.fillRoundRect(8, 8, 20, 18, 3, 0x001F);
    desktopCanvas.drawRoundRect(8, 8, 20, 18, 3, TFT_WHITE);
    desktopCanvas.setTextColor(TFT_WHITE);
    desktopCanvas.setTextSize(1);
    desktopCanvas.setCursor(12, 13);
    desktopCanvas.printf("RF");
    desktopCanvas.setCursor(4, 28);
    desktopCanvas.printf("Hydra");

    desktopCanvas.fillRoundRect(8, 44, 20, 18, 3, 0x780F);
    desktopCanvas.drawRoundRect(8, 44, 20, 18, 3, 0xFFE0);
    desktopCanvas.setTextColor(0xFFE0);
    desktopCanvas.setCursor(12, 49);
    desktopCanvas.printf("BR");
    desktopCanvas.setTextColor(TFT_WHITE);
    desktopCanvas.setCursor(4, 64);
    desktopCanvas.printf("Bruce");

    if (!rfWindowMin) {
        const int winX = 42, winY = 6, winW = 192, winH = 104;
        desktopCanvas.fillRoundRect(winX, winY, winW, winH, 4, 0x02B9);
        desktopCanvas.fillRect(winX + 2, winY + 16, winW - 4, winH - 18, 0xEF5B);

        desktopCanvas.setTextColor(TFT_WHITE);
        desktopCanvas.setCursor(winX + 6, winY + 4);
        desktopCanvas.printf("Hydra RF - @whoami");
        desktopCanvas.fillRoundRect(winX + winW - 15, winY + 2, 12, 12, 2, 0xE182);

        desktopCanvas.setTextColor(TFT_BLACK);
        desktopCanvas.setCursor(winX + 6, winY + 20);
        desktopCanvas.printf("Chip: %s", moduleName(activeModule));

        desktopCanvas.setCursor(winX + 6, winY + 31);
        desktopCanvas.printf("Mode: %s | %lu.%02luMHz", modeName(operationMode), currentFrequencyHz / 1000000UL, (currentFrequencyHz % 1000000UL) / 10000UL);

        desktopCanvas.setCursor(winX + 6, winY + 42);
        desktopCanvas.printf("Peak: %d dBm | Link: ESP-NOW", txPacket.rssiDbm);

        const int scopeX = winX + 6, scopeY = winY + 53, scopeW = 180, scopeH = 44;
        desktopCanvas.fillRect(scopeX, scopeY, scopeW, scopeH, TFT_BLACK);
        desktopCanvas.drawRect(scopeX, scopeY, scopeW, scopeH, 0x8410);

        for (int i = 0; i < 58; i++) {
            int h = (txPacket.spectrumBins[i] * (scopeH - 4)) / 255;
            if (h > 0) {
                uint16_t barCol = (txPacket.spectrumBins[i] > 160) ? 0xFFE0 : 0x07E0;
                desktopCanvas.fillRect(scopeX + 3 + (i * 3), scopeY + scopeH - 2 - h, 2, h, barCol);
            }
        }
    }

    if (startMenuOpen) {
        const int smX = 0, smY = 20, smW = 148, smH = 97;
        desktopCanvas.fillRoundRect(smX, smY, smW, smH, 4, 0x1B5E);
        desktopCanvas.fillRect(smX + 2, smY + 18, 92, smH - 34, TFT_WHITE);
        desktopCanvas.fillRect(smX + 94, smY + 18, 52, smH - 34, 0xD69A);

        desktopCanvas.fillRoundRect(smX + 4, smY + 2, 14, 14, 2, 0xFD20);
        desktopCanvas.setTextColor(TFT_WHITE);
        desktopCanvas.setCursor(smX + 22, smY + 5);
        desktopCanvas.printf("@whoami | CEO");

        const char* menuLabels[MENU_ITEMS_COUNT] = {
            "1: CC1101 SubGHz",
            "2: nRF24 2.4GHz",
            "3: Toggle 433/868",
            "4: Mode Spec/Sniff",
            "5: Switch to Bruce"
        };

        for (int i = 0; i < MENU_ITEMS_COUNT; i++) {
            int itemY = smY + 21 + (i * 12);
            if (i == startMenuIndex) {
                desktopCanvas.fillRect(smX + 3, itemY - 1, 90, 11, 0x2B5E);
                desktopCanvas.setTextColor(TFT_WHITE);
            } else {
                desktopCanvas.setTextColor((i == 4) ? 0xC800 : TFT_BLACK);
            }
            desktopCanvas.setCursor(smX + 5, itemY);
            desktopCanvas.printf("%.15s", menuLabels[i]);
        }

        desktopCanvas.fillRect(smX + 2, smY + smH - 15, smW - 4, 13, 0x1B5E);
        desktopCanvas.setCursor(smX + 77, smY + smH - 12);
        desktopCanvas.printf("[B] Run Bruce");
    }

    const int tbY = 117;
    desktopCanvas.fillRect(0, tbY, 240, 18, 0x129B);
    desktopCanvas.drawFastHLine(0, tbY, 240, 0x4C7F);

    desktopCanvas.fillRoundRect(0, tbY + 1, 46, 17, 4, startMenuOpen ? 0x1B82 : 0x35C4);
    desktopCanvas.setTextColor(TFT_WHITE);
    desktopCanvas.setCursor(10, tbY + 5);
    desktopCanvas.printf("start");

    desktopCanvas.fillRoundRect(50, tbY + 2, 112, 14, 2, 0x24BE);
    desktopCanvas.setCursor(55, tbY + 5);
    desktopCanvas.printf("[M]Menu [1/2]RF [B]Br");

    desktopCanvas.fillRect(166, tbY + 1, 74, 17, 0x0C5E);
    uint32_t upSec = millis() / 1000;
    desktopCanvas.setCursor(170, tbY + 5);
    desktopCanvas.printf("%ddBm %02lu:%02lu", txPacket.rssiDbm, (upSec / 60) % 60, upSec % 60);

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
    txPacket.checksum      = calculateXOR((const uint8_t*)&txPacket, sizeof(TelemetryPacket) - 1);
    esp_now_send(broadcastAddress, (const uint8_t*)&txPacket, sizeof(TelemetryPacket));
}

void executeMenuAction(int8_t idx) {
    switch (idx) {
        case 0: activeModule = MODULE_CC1101; break;
        case 1: activeModule = MODULE_NRF24;  break;
        case 2: currentFrequencyHz = (currentFrequencyHz == FREQ_433_92_MHZ) ? FREQ_868_35_MHZ : FREQ_433_92_MHZ; break;
        case 3: operationMode = static_cast<OperationMode>((static_cast<uint8_t>(operationMode) + 1U) % 3U); break;
        case 4: bootIntoBruce(); break;
    }
    startMenuOpen = false;
}

void handleKeyboard() {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;

    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    if (status.enter && startMenuOpen) {
        executeMenuAction(startMenuIndex);
        return;
    }

    for (char key : status.word) {
        switch (key) {
            case 'm': case 'M': startMenuOpen = !startMenuOpen; break;
            case ';': if (startMenuOpen) startMenuIndex = (startMenuIndex - 1 + MENU_ITEMS_COUNT) % MENU_ITEMS_COUNT; break;
            case '.': if (startMenuOpen) startMenuIndex = (startMenuIndex + 1) % MENU_ITEMS_COUNT; break;
            case '1': activeModule = MODULE_CC1101; break;
            case '2': activeModule = MODULE_NRF24; break;
            case 'f': case 'F': currentFrequencyHz = (currentFrequencyHz == FREQ_433_92_MHZ) ? FREQ_868_35_MHZ : FREQ_433_92_MHZ; break;
            case 'w': case 'W': rfWindowMin = !rfWindowMin; break;
            case 'b': case 'B': bootIntoBruce(); break;
        }
    }
    txPacket.activeModule  = activeModule;
    txPacket.operationMode = operationMode;
    txPacket.frequencyHz   = currentFrequencyHz;
}

void setup() {
    Serial.begin(115200);
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);

    desktopCanvas.setColorDepth(16);
    desktopCanvas.createSprite(240, 135);

    initHydraSPI();
    initEspNowLink();

    memset(&txPacket, 0, sizeof(txPacket));
    txPacket.magic         = 0xA5;
    txPacket.activeModule  = activeModule;
    txPacket.operationMode = operationMode;
    txPacket.frequencyHz   = currentFrequencyHz;
    txPacket.rssiDbm       = -93;

    runWindowsXPBootScreen(XP_BOOT_TIME_MS);
}

void loop() {
    handleKeyboard();
    if ((millis() - lastCycleMs) >= ESP_NOW_PERIOD_MS) {
        lastCycleMs = millis();
        performSpectrumSweep();
        sendTelemetry();
    }
    if ((millis() - lastRenderMs) >= 66U) {
        lastRenderMs = millis();
        renderWindowsXPDesktop();
    }
}
