#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_now.h>
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

TelemetryPacket txPacket;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

uint32_t lastCycleMs  = 0;
uint32_t lastRenderMs = 0;

ActiveModule  activeModule       = MODULE_CC1101;
OperationMode operationMode      = MODE_SPECTRUM;
uint32_t      currentFrequencyHz = FREQ_433_92_MHZ;
char          lastKey            = '-';

uint8_t calculateXOR(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
    }
    return crc;
}

const char* moduleName(ActiveModule module) {
    switch (module) {
        case MODULE_CC1101: return "CC1101 ";
        case MODULE_NRF24:  return "nRF24  ";
        default:            return "IDLE   ";
    }
}

const char* modeName(OperationMode mode) {
    switch (mode) {
        case MODE_STANDBY:  return "STANDBY ";
        case MODE_SPECTRUM: return "SPECTRUM";
        case MODE_SNIFF:    return "SNIFF   ";
        default:            return "STANDBY ";
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
    delay(50);

    if (esp_now_init() == ESP_OK) {
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, broadcastAddress, 6);
        peerInfo.channel = 1;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
    }
}

void renderStatus(bool clearBg = false) {
    if (clearBg) {
        M5Cardputer.Display.fillScreen(TFT_BLACK);
    }
    M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(4, 4);

    M5Cardputer.Display.printf("SMOLINSKI|LABS HYDRA MASTER\n\n");
    M5Cardputer.Display.printf("Module : %s\n", moduleName(activeModule));
    M5Cardputer.Display.printf("Mode   : %s\n", modeName(operationMode));
    M5Cardputer.Display.printf("Freq   : %6.2f MHz  \n", (float)currentFrequencyHz / 1000000.0f);
    M5Cardputer.Display.printf("Peak   : %4d dBm    \n", txPacket.rssiDbm);
    M5Cardputer.Display.printf("LastKey: [%c]        \n\n", lastKey);
    M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5Cardputer.Display.printf("[1]CC1101 [2]nRF [F]Freq [M]Mode");
}

void handleKeyboard() {
    M5Cardputer.update();

    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return;
    }

    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    for (char key : status.word) {
        lastKey = key;
        switch (key) {
            case '1':
                activeModule = MODULE_CC1101;
                break;
            case '2':
                activeModule = MODULE_NRF24;
                break;
            case 'F':
            case 'f':
                currentFrequencyHz = (currentFrequencyHz == FREQ_433_92_MHZ) ? FREQ_868_35_MHZ : FREQ_433_92_MHZ;
                break;
            case 'M':
            case 'm':
                operationMode = static_cast<OperationMode>((static_cast<uint8_t>(operationMode) + 1U) % 3U);
                break;
            default:
                break;
        }
    }

    txPacket.activeModule  = activeModule;
    txPacket.operationMode = operationMode;
    txPacket.frequencyHz   = currentFrequencyHz;
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
            int16_t rssiDbm = (rawRssi >= 128)
                              ? (((int16_t)rawRssi - 256) / 2 - 74)
                              : ((int16_t)rawRssi / 2 - 74);

            if (rssiDbm > peakRssiDbm) {
                peakRssiDbm = rssiDbm;
            }

            int32_t mapped = (rssiDbm + 110) * 255 / 80;
            txPacket.spectrumBins[i] = (uint8_t)constrain(mapped, 0, 255);
        }

        cc1101Strobe(0x36);
        hydraSPI.endTransaction();

    } else if (activeModule == MODULE_NRF24) {
        hydraSPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

        for (uint8_t i = 0; i < RF_SWEEP_POINTS; ++i) {
            uint8_t channel = i * 2;
            nrf24WriteReg(0x05, channel);

            digitalWrite(NRF24_CE, HIGH);
            delayMicroseconds(130);
            digitalWrite(NRF24_CE, LOW);

            uint8_t rpd = nrf24ReadReg(0x09) & 0x01;

            uint16_t currentVal = txPacket.spectrumBins[i];
            if (rpd) {
                currentVal = (currentVal + 180 > 255) ? 255 : (currentVal + 180);
            } else {
                currentVal = (currentVal > 20) ? (currentVal - 20) : 8;
            }
            txPacket.spectrumBins[i] = (uint8_t)currentVal;

            int16_t estDbm = -95 + ((int16_t)currentVal * 60 / 255);
            if (estDbm > peakRssiDbm) {
                peakRssiDbm = estDbm;
            }
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

void setup() {
    Serial.begin(115200);
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);

    initHydraSPI();
    initEspNowLink();

    memset(&txPacket, 0, sizeof(txPacket));
    txPacket.magic         = 0xA5;
    txPacket.activeModule  = activeModule;
    txPacket.operationMode = operationMode;
    txPacket.frequencyHz   = currentFrequencyHz;
    txPacket.rssiDbm       = -93;

    renderStatus(true);
}

void loop() {
    handleKeyboard();

    if ((millis() - lastCycleMs) >= ESP_NOW_PERIOD_MS) {
        lastCycleMs = millis();
        performSpectrumSweep();
        sendTelemetry();
    }

    if ((millis() - lastRenderMs) >= 100U) {
        lastRenderMs = millis();
        renderStatus(false);
    }
}
