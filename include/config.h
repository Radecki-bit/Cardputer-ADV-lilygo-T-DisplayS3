#pragma once
#include <Arduino.h>
#include <SD.h>

#define SYSTEM_AUTHOR          "@whoami"
#define SYSTEM_PROJECT         "Smolinski|Labs Launcher"
#define FIRMWARE_VERSION       "v1.1.0-PRODUCTION"

// T-Display-S3 SD Card SPI Pins
#define SD_CS_PIN              10
#define SD_MOSI_PIN            11
#define SD_MISO_PIN            13
#define SD_SCK_PIN             12

// T-Display-S3 Hardware & Reserved 2.4GHz Antenna Switch + Grove Pins
#define PIN_POWER_ON           15
#define PIN_BTN_TOP            0
#define PIN_BTN_BOTTOM         14
#define PIN_LCD_BL             38
#define PIN_ANTENNA_SW         16
#define PIN_TDISPLAY_GROVE_RX  44
#define PIN_TDISPLAY_GROVE_TX  43

// Cardputer ADV Hydra SPI & Reserved 2.4GHz ADV Antenna Switch + Grove Pins
#define HYDRA_SCK              40
#define HYDRA_MISO             39
#define HYDRA_MOSI             14
#define CC1101_CS              13
#define CC1101_GDO0            5
#define NRF24_CSN              6
#define NRF24_CE               4
#define SDCARD_CS              12
#define PIN_ADV_ANT_24GHZ      2
#define PIN_ADV_GROVE_SDA      2
#define PIN_ADV_GROVE_SCL      1

// 2.4GHz Antenna Switch States
#define ANT_24GHZ_INTERNAL     0
#define ANT_24GHZ_EXTERNAL     1

// RF Constants
#define FREQ_433_92_MHZ        433920000UL
#define FREQ_868_35_MHZ        868350000UL
#define RF_SWEEP_POINTS        64U
#define ESP_NOW_PERIOD_MS      33U
#define XP_BOOT_TIME_MS        8000UL
#define PROTOCOL_BIN_SIZE      4096U

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

// 64-byte Binary Protocol Header (Offset 0x0000)
struct EspNowProtocolHeader {
    uint32_t magicHeader;       // 0x534D5241 ("SMRA")
    uint16_t structVersion;     // 0x0101
    uint8_t  targetMcuType;     // 1 = ESP32-S3 (T-Display), 2 = ESP32-U (Stealth Node)
    uint8_t  wifiChannel;       // 1..13
    uint8_t  antMode24GHz;      // 1 = EXTERNAL, 0 = INTERNAL
    uint8_t  antSwitchGpio;     // GPIO 16 (S3) or 0xFF (Hardwired U.FL on ESP32-U)
    uint8_t  txPowerQuarterDbm; // 80 = 20.0 dBm
    uint8_t  groveMode;         // 1 = UART_BRIDGE, 2 = I2C_BRIDGE
    uint8_t  groveRxPin;        // GPIO 44 (S3) or GPIO 3 (ESP32-U)
    uint8_t  groveTxPin;        // GPIO 43 (S3) or GPIO 1 (ESP32-U)
    uint8_t  peerMac[6];        // Peer MAC address
    uint32_t uartBaudRate;      // 115200
    uint32_t hopIntervalMs;     // Channel hop dwell time
    uint16_t hopTableOffset;    // 0x0040
    uint16_t cc1101LutOffset;   // 0x0080
    uint16_t nrf24LutOffset;    // 0x0180
    uint16_t groveLutOffset;    // 0x0280
    char     profileName[24];   // Null-terminated profile identifier
    uint32_t headerCrc32;       // Header integrity checksum
};
#pragma pack(pop)

inline uint8_t calculateXOR(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) crc ^= data[i];
    return crc;
}

inline uint32_t calculateCRC32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
        }
    }
    return ~crc;
}

inline bool loadEspNowProtocolBin(const char* path, EspNowProtocolHeader& outHdr) {
    if (!SD.exists(path)) return false;
    File f = SD.open(path, FILE_READ);
    if (!f || f.size() < sizeof(EspNowProtocolHeader)) {
        if (f) f.close();
        return false;
    }
    f.read((uint8_t*)&outHdr, sizeof(EspNowProtocolHeader));
    f.close();
    return (outHdr.magicHeader == 0x534D5241);
}

inline String readCfgValue(const char* path, const char* key, const char* defaultVal) {
    if (!SD.exists(path)) return String(defaultVal);
    File f = SD.open(path, FILE_READ);
    if (!f) return String(defaultVal);
    String targetPrefix = String(key) + "=";
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.startsWith("#") || line.length() == 0) continue;
        if (line.startsWith(targetPrefix)) {
            f.close();
            return line.substring(targetPrefix.length());
        }
    }
    f.close();
    return String(defaultVal);
}

inline void writeStructuredProtocolBin(const char* path, uint8_t mcuType, uint8_t antGpio, uint8_t rxPin, uint8_t txPin, const char* label) {
    if (SD.exists(path)) return;
    File f = SD.open(path, FILE_WRITE);
    if (!f) return;

    uint8_t sector[512];
    memset(sector, 0, sizeof(sector));

    EspNowProtocolHeader* hdr = (EspNowProtocolHeader*)sector;
    hdr->magicHeader       = 0x534D5241;
    hdr->structVersion     = 0x0101;
    hdr->targetMcuType     = mcuType;
    hdr->wifiChannel       = 1;
    hdr->antMode24GHz      = ANT_24GHZ_EXTERNAL;
    hdr->antSwitchGpio     = antGpio;
    hdr->txPowerQuarterDbm = 80;
    hdr->groveMode         = 1;
    hdr->groveRxPin        = rxPin;
    hdr->groveTxPin        = txPin;
    memset(hdr->peerMac, 0xFF, 6);
    hdr->uartBaudRate      = 115200;
    hdr->hopIntervalMs     = 33;
    hdr->hopTableOffset    = 0x0040;
    hdr->cc1101LutOffset   = 0x0080;
    hdr->nrf24LutOffset    = 0x0180;
    hdr->groveLutOffset    = 0x0280;
    strncpy(hdr->profileName, label, 23);
    hdr->headerCrc32       = calculateCRC32((const uint8_t*)hdr, sizeof(EspNowProtocolHeader) - 4);

    // Populate Channel Hopping Sequence at 0x0040
    const uint8_t hopChannels[16] = {1, 6, 11, 2, 7, 12, 3, 8, 13, 4, 9, 5, 10, 1, 6, 11};
    memcpy(&sector[0x40], hopChannels, 16);

    // Populate CC1101 SmartRF Register Preset LUT at 0x0080
    const uint8_t cc1101Preset[32] = {
        0x02, 0x06, 0x08, 0x05, 0x0B, 0x06, 0x10, 0xF5,
        0x11, 0x83, 0x12, 0x30, 0x15, 0x15, 0x18, 0x18,
        0x19, 0x16, 0x1B, 0x07, 0x21, 0x56, 0x22, 0x11,
        0x23, 0xE9, 0x24, 0x2A, 0x25, 0x00, 0x26, 0x1F
    };
    memcpy(&sector[0x80], cc1101Preset, 32);

    f.write(sector, sizeof(sector));
    for (int i = 1; i < 8; i++) {
        memset(sector, 0xA5 ^ i, sizeof(sector));
        f.write(sector, sizeof(sector));
    }
    f.close();
}

inline void provisionDefaultSDConfigs() {
    if (!SD.exists("/config"))    SD.mkdir("/config");
    if (!SD.exists("/protocols")) SD.mkdir("/protocols");
    if (!SD.exists("/firmware"))  SD.mkdir("/firmware");

    if (!SD.exists("/config/adv_master.cfg")) {
        File f = SD.open("/config/adv_master.cfg", FILE_WRITE);
        if (f) {
            f.println("# Smolinski|Labs - Cardputer ADV Native Configuration");
            f.println("device_name=Cardputer_ADV_Master");
            f.println("active_module=1");
            f.println("operation_mode=1");
            f.println("default_freq_hz=433920000");
            f.println("ant_24ghz_mode=EXT");
            f.println("ant_switch_pin=2");
            f.println("espnow_channel=1");
            f.println("grove_sda_pin=2");
            f.println("grove_scl_pin=1");
            f.println("grove_link_enable=1");
            f.println("protocol_s3_bin=/protocols/espnow_tdisplay_s3.bin");
            f.println("protocol_esp32u_bin=/protocols/espnow_esp32_u.bin");
            f.println("bruce_bin_path=/firmware/bruce.bin");
            f.close();
        }
    }

    if (!SD.exists("/config/tdisplay_s3.cfg")) {
        File f = SD.open("/config/tdisplay_s3.cfg", FILE_WRITE);
        if (f) {
            f.println("# Smolinski|Labs - LilyGO T-Display-S3 Native Configuration");
            f.println("device_name=TDisplay_S3_HUD");
            f.println("cluster_mode=1");
            f.println("language_id=3");
            f.println("ant_24ghz_mode=EXT");
            f.println("ant_switch_pin=16");
            f.println("grove_rx_pin=44");
            f.println("grove_tx_pin=43");
            f.println("espnow_channel=1");
            f.println("protocol_bin=/protocols/espnow_tdisplay_s3.bin");
            f.println("peer_stealth_bin=/protocols/espnow_esp32_u.bin");
            f.println("waterfall_default=0");
            f.println("lcd_brightness=220");
            f.close();
        }
    }

    if (!SD.exists("/protocols/espnow_tdisplay_s3.cfg")) {
        File f = SD.open("/protocols/espnow_tdisplay_s3.cfg", FILE_WRITE);
        if (f) {
            f.println("# ESP-NOW Protocol Config - Target: ESP32-S3 (LilyGO T-Display-S3)");
            f.println("protocol_bin=/protocols/espnow_tdisplay_s3.bin");
            f.println("mcu_target=ESP32-S3");
            f.println("node_role=TOP_HUD_DUAL_ANTENNA");
            f.println("wifi_channel=1");
            f.println("ant_24ghz_route=EXT_SMA");
            f.println("gpio_ant_sw=16");
            f.println("tx_power_dbm=20");
            f.println("peer_mac=FF:FF:FF:FF:FF:FF");
            f.println("grove_bridge=UART_115200");
            f.println("grove_rx_pin=44");
            f.println("grove_tx_pin=43");
            f.close();
        }
    }

    if (!SD.exists("/protocols/espnow_esp32_u.cfg")) {
        File f = SD.open("/protocols/espnow_esp32_u.cfg", FILE_WRITE);
        if (f) {
            f.println("# ESP-NOW Protocol Config - Target: ESP32-U (Hidden Stealth Node)");
            f.println("protocol_bin=/protocols/espnow_esp32_u.bin");
            f.println("mcu_target=ESP32-U");
            f.println("node_role=HIDDEN_STEALTH_TRANSCEIVER");
            f.println("wifi_channel=1");
            f.println("ant_24ghz_route=UFL_EXTERNAL_HIGH_GAIN");
            f.println("tx_power_dbm=20");
            f.println("peer_mac=FF:FF:FF:FF:FF:FF");
            f.println("grove_bridge=UART_115200");
            f.println("grove_tx_pin=1");
            f.println("grove_rx_pin=3");
            f.close();
        }
    }

    writeStructuredProtocolBin("/protocols/espnow_tdisplay_s3.bin", 1, 16, 44, 43, "SMRA_TDISPLAY_S3_V1");
    writeStructuredProtocolBin("/protocols/espnow_esp32_u.bin",     2, 255, 3,  1, "SMRA_ESP32_U_STEALTH");
}
