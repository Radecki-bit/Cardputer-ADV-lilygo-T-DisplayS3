#pragma once
#include <Arduino.h>

#define SYSTEM_AUTHOR          "@whoami"
#define SYSTEM_PROJECT         "Smolinski|Labs Launcher"
#define FIRMWARE_VERSION       "v1.0.8-PROTOTYPE"

// T-Display-S3 SD Card SPI Pins
#define SD_CS_PIN              10
#define SD_MOSI_PIN            11
#define SD_MISO_PIN            13
#define SD_SCK_PIN             12

// T-Display-S3 Hardware & Reserved 2.4GHz Antenna Switch
#define PIN_POWER_ON           15
#define PIN_BTN_TOP            0
#define PIN_BTN_BOTTOM         14
#define PIN_LCD_BL             38
#define PIN_ANTENNA_SW         16

// Cardputer ADV Hydra SPI & Reserved 2.4GHz ADV Antenna Switch
#define HYDRA_SCK              40
#define HYDRA_MISO             39
#define HYDRA_MOSI             14
#define CC1101_CS              13
#define CC1101_GDO0            5
#define NRF24_CSN              6
#define NRF24_CE               4
#define SDCARD_CS              12
#define PIN_ADV_ANT_24GHZ      2

// 2.4GHz Antenna Switch States
#define ANT_24GHZ_INTERNAL     0
#define ANT_24GHZ_EXTERNAL     1

// RF Constants
#define FREQ_433_92_MHZ        433920000UL
#define FREQ_868_35_MHZ        868350000UL
#define RF_SWEEP_POINTS        64U
#define ESP_NOW_PERIOD_MS      33U
#define XP_BOOT_TIME_MS        8000UL

#pragma pack(push, 1)
struct TelemetryPacket {
    uint8_t  magic;
    uint8_t  activeModule;
    uint8_t  operationMode;
    uint32_t frequencyHz;
    int16_t  rssiDbm;
    uint8_t  spectrumBins[64];
    uint8_t  ant24GState;
    uint8_t  sdStatus;
    uint8_t  checksum;
};
#pragma pack(pop)

inline uint8_t calculateXOR(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) crc ^= data[i];
    return crc;
}
