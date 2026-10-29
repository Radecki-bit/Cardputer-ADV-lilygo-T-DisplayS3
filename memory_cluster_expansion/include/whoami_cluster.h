#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEAdvertising.h>
#include <Preferences.h>
#include <esp_idf_version.h>

#define WHOAMI_PROTOCOL_MAGIC   0x57484F41UL  // "WHOA" -> @whoami
#define WHOAMI_UART_BRIDGE_SYNC 0x55415254UL  // "UART" -> Inter-board DMA/UART link
#define WHOAMI_RF_CHANNEL       7             // 2442 MHz (2.44 GHz Center Frequency)

#ifndef WHOAMI_EXT_ANTENNA
#define WHOAMI_EXT_ANTENNA      1
#endif

#ifndef WHOAMI_NODE_ROLE
#define WHOAMI_NODE_ROLE        2
#endif

#ifndef WHOAMI_TX_POWER_QDBM
#define WHOAMI_TX_POWER_QDBM    80            // 80 = +20.0 dBm (SMA), 70 = +17.5 dBm (PCB)
#endif

#define PIN_STATUS_LED          2
#define PIN_EXT_ANT_SWITCH      4
#define PIN_BOOT_BUTTON         0
#define PIN_UART_BRIDGE_RX      16
#define PIN_UART_BRIDGE_TX      17

enum WhoamiRole : uint8_t {
    ROLE_MOTHERBOARD_MASTER = 0x01,
    ROLE_ESP_WITH_ANTENNA   = 0x02,
    ROLE_ESP_NO_ANTENNA     = 0x03,
    ROLE_CARDPUTER_TERMINAL = 0x04
};

enum WhoamiLedMode : uint8_t {
    LED_MODE_BOOT_5HZ       = 0x00,
    LED_MODE_PAIRING_1HZ    = 0x01,
    LED_MODE_ONLINE_SOLID   = 0x02,
    LED_MODE_TX_PULSE       = 0x03,
    LED_MODE_ERROR_DOUBLE   = 0x04
};

enum WhoamiOpCode : uint8_t {
    OP_MASTER_BEACON        = 0x10,
    OP_MEM_ALLOC_DISPATCH   = 0x20,
    OP_UART_COPROC_REQ      = 0x30,
    OP_UART_COPROC_RES      = 0x31,
    OP_CLUSTER_STATE_REPORT = 0x40,
    OP_CLUSTER_ERROR_TRAP   = 0xE0
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

struct WhoamiUartBridgePacket {
    uint32_t magic;
    uint32_t jobId;
    uint8_t  opCode;
    uint32_t freeSramBytes;
    uint32_t freePsramBytes;
    uint32_t computeTimeUs;
    int32_t  shardVector[2];
    char     memoryPage[32];
    uint8_t  crc;
};
#pragma pack(pop)

static const uint8_t WHOAMI_BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

inline uint8_t computeWhoamiCrc(const uint8_t* ptr, size_t len) {
    uint8_t crc = 0x5A;
    for (size_t i = 0; i < len; i++) {
        crc ^= ptr[i];
        crc = (crc << 1) | (crc >> 7);
    }
    return crc;
}
