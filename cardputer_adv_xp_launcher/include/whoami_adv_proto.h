#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEAdvertising.h>
#include <esp_idf_version.h>

// Piny sprzętowe czytnika kart microSD w M5Stack Cardputer / Cardputer ADV
#define ADV_SD_SCK_PIN          40
#define ADV_SD_MISO_PIN         39
#define ADV_SD_MOSI_PIN         14
#define ADV_SD_CS_PIN           12

#define WHOAMI_PROTOCOL_MAGIC   0x57484F41UL  // "WHOA" -> @whoami
#define SMRA_MAGIC              0x534D5241UL  // "SMRA" -> T-Display-S3 HUD

enum WhoamiRole : uint8_t {
    ROLE_MOTHERBOARD_MASTER = 0x01,
    ROLE_ESP_WITH_ANTENNA   = 0x02,
    ROLE_ESP_NO_ANTENNA     = 0x03,
    ROLE_CARDPUTER_TERMINAL = 0x04
};

enum WhoamiOpCode : uint8_t {
    OP_MASTER_BEACON        = 0x10,
    OP_MEM_ALLOC_DISPATCH   = 0x20,
    OP_UART_COPROC_REQ      = 0x30,
    OP_UART_COPROC_RES      = 0x31,
    OP_CLUSTER_STATE_REPORT = 0x40,
    OP_MOTHER_AGGREGATE     = 0x50,
    OP_ADV_PROMPT_INJECT    = 0x60
};

#pragma pack(push, 1)
struct WhoamiWirelessPacket {
    uint32_t magic;
    uint32_t seq;
    uint32_t masterClockMs;
    uint32_t jobId;
    uint8_t  opCode;
    uint8_t  srcRole;
    uint8_t  clusterPairId;
    uint8_t  coprocBridgeActive;
    uint8_t  hwHasAntenna;
    int8_t   rssiDbm;
    uint32_t freeSramMaster;
    uint32_t freeSramCoproc;
    uint32_t freePsramTotal;
    uint32_t computeLatencyUs;
    int32_t  tensorLogits[4];
    char     signature[10];
    char     payload[54];
    uint8_t  crc;
};

struct SmraPacket {
    uint32_t magic;
    uint32_t seq;
    uint8_t  type;
    uint8_t  hudMode;
    uint8_t  navKey;
    uint8_t  remoteLock;
    uint32_t freeHeap;
    uint8_t  crc;
};
#pragma pack(pop)

// Struktura przechowująca aktywną konfigurację wczytaną z plików .conf na karcie SD
struct AdvRuntimeConfig {
    bool     sdMounted        = false;
    bool     protokoleLoaded  = false;
    bool     ble50Loaded      = false;
    bool     ble51Loaded      = false;
    bool     wifiLoaded       = false;
    bool     espnowLoaded     = false;
    uint8_t  rfChannel        = 7;
    uint16_t rfFreqMhz        = 2442;
    uint8_t  txPowerQdbm      = 80;
    uint16_t udpPort          = 4444;
    char     wifiSsid[32]     = "@whoami_ADV_Bridge";
    char     wifiPass[32]     = "whoami2026";
    char     bleDeviceName[32]= "WHOAMI_ADV_BLE51";
    char     bleServiceUuid[40]= "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
};

static const uint8_t BCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

inline uint8_t computeWhoamiCrc(const uint8_t* ptr, size_t len) {
    uint8_t crc = 0x5A;
    for (size_t i = 0; i < len; i++) {
        crc ^= ptr[i];
        crc = (crc << 1) | (crc >> 7);
    }
    return crc;
}

inline uint8_t smraCalcCrc(const SmraPacket& pkt) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&pkt);
    uint8_t crc = 0x5A;
    for (size_t i = 0; i < sizeof(SmraPacket) - 1; i++) crc ^= p[i];
    return crc;
}
