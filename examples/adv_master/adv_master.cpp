#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <M5Cardputer.h>
#include <math.h>

// -----------------------------------------------------------------------------
// M5Stack Cardputer ADV - Hydra RF Master
// ESP-NOW transmitter + local CLi/status panel
// -----------------------------------------------------------------------------

#define HYDRA_SCK       40
#define HYDRA_MISO      39
#define HYDRA_MOSI      14
#define CC1101_CS       13
#define CC1101_GDO0     5
#define NRF24_CSN       6
#define NRF24_CE        4
#define SDCARD_CS       12

#define FREQ_433_92_MHZ 433920000UL
#define FREQ_868_35_MHZ 868350000UL
#define RF_SWEEP_POINTS 64U
#define ESP_NOW_PERIOD_MS 33U

enum ActiveModule : uint8_t {
    MODULE_IDLE = 0,
    MODULE_CC1101 = 1,
    MODULE_NRF24 = 2
};

enum OperationMode : uint8_t {
    MODE_STANDBY = 0,
    MODE_SPECTRUM = 1,
    MODE_SNIFF = 2
};

#pragma pack(push, 1)
struct TelemetryPacket {
    uint8_t  magic;
    uint8_t  activeModule;
    uint8_t  operationMode;
    uint32_t frequencyHz;
    int16_t  rssiDbm;
    uint8_t  spectrumBins[64];
};
#pragma pack(pop)

TelemetryPacket txPacket;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

uint32_t lastCycleMs = 0;
uint32_t lastRenderMs = 0;

ActiveModule activeModule = MODULE_CC1101;
OperationMode operationMode = MODE_SPECTRUM;
uint32_t currentFrequencyHz = FREQ_433_92_MHZ;
char lastKey = 0;

const char* moduleName(ActiveModule module) {
    switch (module) {
        case MODULE_CC1101: return "CC1101";
        case MODULE_NRF24:  return "nRF24";
        default: return "IDLE";
    }
}

const char* modeName(OperationMode mode) {
    switch (mode) {
        case MODE_STANDBY: return "STANDBY";
        case MODE_SPECTRUM: return "SPECTRUM";
        case MODE_SNIFF: return "SNIFF";
        default: return "STANDBY";
    }
}

void initHydraSPI() {
    pinMode(CC1101_CS, OUTPUT); digitalWrite(CC1101_CS, HIGH);
    pinMode(NRF24_CSN, OUTPUT); digitalWrite(NRF24_CSN, HIGH);
    pinMode(SDCARD_CS, OUTPUT); digitalWrite(SDCARD_CS, HIGH);
    pinMode(NRF24_CE, OUTPUT); digitalWrite(NRF24_CE, LOW);
    pinMode(CC1101_GDO0, INPUT);
    SPI.begin(HYDRA_SCK, HYDRA_MISO, HYDRA_MOSI);
    SPI.setDataMode(SPI_MODE0);
    SPI.setBitOrder(MSBFIRST);
    SPI.setClockDivider(SPI_CLOCK_DIV4);
}

void initEspNowLink() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(50);

    if (esp_now_init() == ESP_OK) {
        memcpy(peerInfo.peer_addr, broadcastAddress, 6);
        peerInfo.channel = 1;
        peerInfo.encrypt = false;
        esp_err_t addPeerResult = esp_now_add_peer(&peerInfo);
        if (addPeerResult != ESP_OK) {
            Serial.printf("ESP-NOW add_peer failed: %d\n", addPeerResult);
        }
    } else {
        Serial.println("ESP-NOW init failed");
    }
}

void renderStatus() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(TFT_GREEN);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(0, 0);

    M5Cardputer.Display.printf("Hydra RF Monitor\n");
    M5Cardputer.Display.printf("Module: %s\n", moduleName(activeModule));
    M5Cardputer.Display.printf("Mode: %s\n", modeName(operationMode));
    M5Cardputer.Display.printf("Freq: %.2f MHz\n", (float)currentFrequencyHz / 1000000.0f);
    M5Cardputer.Display.printf("RSSI: %d dBm\n", txPacket.rssiDbm);
    M5Cardputer.Display.printf("Key: %c\n", (lastKey != 0) ? lastKey : ' ');
    M5Cardputer.Display.printf("1=CC1101 2=nRF24\n");
    M5Cardputer.Display.printf("F=Freq M=Mode\n");
}

void handleKeyboard() {
    if (!M5Cardputer.Keyboard.isKeyPressed()) {
        return;
    }

    char key = M5Cardputer.Keyboard.getKey();
    if (key == 0) {
        return;
    }

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

    txPacket.activeModule = activeModule;
    txPacket.operationMode = operationMode;
    txPacket.frequencyHz = currentFrequencyHz;
}

void performSpectrumSweep() {
    memset(txPacket.spectrumBins, 0, sizeof(txPacket.spectrumBins));

    digitalWrite(CC1101_CS, HIGH);
    digitalWrite(NRF24_CSN, HIGH);
    digitalWrite(NRF24_CE, LOW);

    if (activeModule == MODULE_CC1101) {
        SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
        digitalWrite(CC1101_CS, LOW);

        for (uint8_t i = 0; i < RF_SWEEP_POINTS; ++i) {
            uint8_t raw = SPI.transfer(0x00);
            int16_t sample = (int16_t)raw - 128;

            // Placeholder real-world driver would configure CC1101 registers and read RSSI.
            // This keeps the TDM / ESP-NOW pattern stable while preserving the UART-free architecture.
            float phase = (float)i * 0.52f + (float)currentFrequencyHz * 1.0e-7f;
            int16_t offset = (int16_t)(sin(phase) * 30.0f);
            sample = constrain(sample + offset, 0, 255);

            txPacket.spectrumBins[i] = (uint8_t)sample;
            delayMicroseconds(80);
        }

        digitalWrite(CC1101_CS, HIGH);
        SPI.endTransaction();
    } else if (activeModule == MODULE_NRF24) {
        SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
        digitalWrite(NRF24_CSN, LOW);

        for (uint8_t i = 0; i < RF_SWEEP_POINTS; ++i) {
            uint8_t raw = SPI.transfer(0x00);
            int16_t sample = (int16_t)raw - 128;

            float phase = (float)i * 0.41f + (float)currentFrequencyHz * 1.3e-7f;
            int16_t offset = (int16_t)(cos(phase) * 28.0f);
            sample = constrain(sample + offset, 0, 255);

            txPacket.spectrumBins[i] = (uint8_t)sample;
            delayMicroseconds(80);
        }

        digitalWrite(NRF24_CSN, HIGH);
        SPI.endTransaction();
    } else {
        for (uint8_t i = 0; i < RF_SWEEP_POINTS; ++i) {
            txPacket.spectrumBins[i] = 0;
        }
    }

    uint32_t sum = 0;
    for (uint8_t i = 0; i < RF_SWEEP_POINTS; ++i) {
        sum += txPacket.spectrumBins[i];
    }

    int32_t avg = (int32_t)(sum / RF_SWEEP_POINTS);
    txPacket.rssiDbm = (int16_t)(-90 - (avg / 2));

    digitalWrite(CC1101_CS, HIGH);
    digitalWrite(NRF24_CSN, HIGH);
    digitalWrite(NRF24_CE, LOW);
}

void sendTelemetry() {
    txPacket.magic = 0xA5;
    txPacket.activeModule = activeModule;
    txPacket.operationMode = operationMode;
    txPacket.frequencyHz = currentFrequencyHz;

    esp_err_t err = esp_now_send(broadcastAddress, (uint8_t*)&txPacket, sizeof(TelemetryPacket));
    if (err != ESP_OK) {
        Serial.printf("ESP_NOW send failed: %d\n", err);
    }
}

void setup() {
    Serial.begin(115200);
    M5Cardputer.begin();
    M5Cardputer.Display.setRotation(1);

    initHydraSPI();
    initEspNowLink();

    memset(&txPacket, 0, sizeof(txPacket));
    txPacket.magic = 0xA5;
    txPacket.activeModule = activeModule;
    txPacket.operationMode = operationMode;
    txPacket.frequencyHz = currentFrequencyHz;
    txPacket.rssiDbm = -93;

    renderStatus();
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
        renderStatus();
    }
}
