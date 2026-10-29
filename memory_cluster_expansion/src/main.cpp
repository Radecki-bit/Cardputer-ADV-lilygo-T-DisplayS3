#include "whoami_cluster.h"
#include "whoami_custom_ext.h"

Preferences          g_prefs;
volatile uint8_t     g_nodeRole           = WHOAMI_NODE_ROLE;
volatile uint8_t     g_clusterId          = 1;
volatile uint32_t    g_bootTimestampMs    = 0;
volatile uint32_t    g_lastMasterRxMs     = 0;
volatile uint32_t    g_lastCoprocRxMs     = 0;
volatile uint32_t    g_txBlinkUntilMs     = 0;
volatile uint32_t    g_errorStateUntilMs  = 0;
volatile bool        g_hasEverLinked      = false;

volatile uint32_t    g_seqCounter         = 0;
volatile uint32_t    g_activeJobId        = 1;
volatile uint32_t    g_partnerFreeSram    = 0;
volatile uint32_t    g_partnerFreePsram   = 0;
volatile uint32_t    g_partnerComputeUs   = 0;
int32_t              g_partnerVectors[2]  = {0, 0};

static constexpr size_t OP_MEM_POOL_SIZE  = 32768;
uint8_t*             g_operationalRamPool = nullptr;
char                 g_activeTaskBuf[54]  = "@whoami:MEM_POOL_ONLINE";
volatile bool        g_immediateDispatch  = false;
uint32_t             g_customExtMetric    = 0;

inline void triggerTxPulse() {
    g_txBlinkUntilMs = millis() + 85;
}

void diagnosticLedTask(void* pvParameters) {
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);

    for (;;) {
        uint32_t now = millis();
        uint32_t uptime = now - g_bootTimestampMs;

        bool linkHealthy = (g_lastMasterRxMs > 0 && (now - g_lastMasterRxMs < 4500)) ||
                           (g_lastCoprocRxMs > 0 && (now - g_lastCoprocRxMs < 4500));

        if (linkHealthy) g_hasEverLinked = true;

        if (uptime < 2500) {
            bool on = ((now / 100) % 2) == 0;
            digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW);
        } else if ((g_hasEverLinked && !linkHealthy) || (now < g_errorStateUntilMs)) {
            uint32_t phase = now % 1000;
            bool on = (phase < 100) || (phase >= 200 && phase < 300);
            digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW);
        } else if (!linkHealthy) {
            bool on = ((now / 500) % 2) == 0;
            digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW);
        } else if (now < g_txBlinkUntilMs) {
            uint32_t rem = g_txBlinkUntilMs - now;
            digitalWrite(PIN_STATUS_LED, (rem > 35) ? LOW : HIGH);
        } else {
            digitalWrite(PIN_STATUS_LED, HIGH);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void executeMemoryTensorShard(const char* inputData, uint8_t shardHalf, int32_t outLogits[2], uint32_t& durationUs) {
    uint32_t tStart = micros();
    const int8_t quantWeights[2][16] = {
        { 14, -7,  9, 19, -12,  6,  8, -5, 17,  9, -8, 13,  4, -9, 15,  7},
        {-11, 15, -8, 12,  16, -6, 13,  9, -7, 14,  6, -4, 18,  8, -6, 11}
    };

    outLogits[0] = 0x57484F00 + shardHalf;
    outLogits[1] = 0x414D4900 + shardHalf;

    if (g_operationalRamPool != nullptr) {
        for (size_t offset = 0; offset < OP_MEM_POOL_SIZE; offset += 64) {
            g_operationalRamPool[offset] ^= static_cast<uint8_t>(inputData[offset % 16] + shardHalf);
        }
    }

    for (int layer = 0; layer < 96; layer++) {
        for (int row = 0; row < 2; row++) {
            int32_t acc = 0;
            for (int col = 0; col < 16; col++) {
                char c = inputData[col % 16];
                int8_t act = static_cast<int8_t>(c ? c : (col + 31)) + ((layer + shardHalf) & 0x0F);
                acc += static_cast<int32_t>(act) * static_cast<int32_t>(quantWeights[(row + shardHalf) % 2][col]);
            }
            outLogits[row] ^= (acc + (layer << 4));
        }
    }

    durationUs = micros() - tStart;
}

void sendUartBridgePacket(uint8_t op, uint32_t jobId, const int32_t vec[2], uint32_t compUs, const char* pageSlice) {
    WhoamiUartBridgePacket pkt = {};
    pkt.magic          = WHOAMI_UART_BRIDGE_SYNC;
    pkt.jobId          = jobId;
    pkt.opCode         = op;
    pkt.freeSramBytes  = ESP.getFreeHeap();
    pkt.freePsramBytes = ESP.getFreePsram();
    pkt.computeTimeUs  = compUs;

    if (vec) {
        pkt.shardVector[0] = vec[0];
        pkt.shardVector[1] = vec[1];
    }
    if (pageSlice) {
        strncpy(pkt.memoryPage, pageSlice, sizeof(pkt.memoryPage) - 1);
    }
    pkt.crc = computeWhoamiCrc(reinterpret_cast<const uint8_t*>(&pkt), sizeof(WhoamiUartBridgePacket) - 1);

    Serial2.write(reinterpret_cast<const uint8_t*>(&pkt), sizeof(WhoamiUartBridgePacket));
    if (g_nodeRole == ROLE_ESP_NO_ANTENNA) {
        Serial.write(reinterpret_cast<const uint8_t*>(&pkt), sizeof(WhoamiUartBridgePacket));
    }
}

void processIncomingUartBridge(HardwareSerial& serialPort) {
    while (serialPort.available() >= static_cast<int>(sizeof(WhoamiUartBridgePacket))) {
        if (serialPort.peek() != 0x54) {
            serialPort.read();
            continue;
        }
        WhoamiUartBridgePacket rxPkt;
        serialPort.readBytes(reinterpret_cast<uint8_t*>(&rxPkt), sizeof(WhoamiUartBridgePacket));
        uint8_t calc = computeWhoamiCrc(reinterpret_cast<const uint8_t*>(&rxPkt), sizeof(WhoamiUartBridgePacket) - 1);
        if (rxPkt.magic != WHOAMI_UART_BRIDGE_SYNC || rxPkt.crc != calc) {
            g_errorStateUntilMs = millis() + 1500;
            continue;
        }

        g_lastCoprocRxMs = millis();

        if (g_nodeRole == ROLE_ESP_NO_ANTENNA && rxPkt.opCode == OP_UART_COPROC_REQ) {
            int32_t localVec[2] = {0, 0};
            uint32_t elapsedUs  = 0;
            executeMemoryTensorShard(rxPkt.memoryPage, 1, localVec, elapsedUs);
            triggerTxPulse();
            sendUartBridgePacket(OP_UART_COPROC_RES, rxPkt.jobId, localVec, elapsedUs, rxPkt.memoryPage);
        } else if (g_nodeRole == ROLE_ESP_WITH_ANTENNA && rxPkt.opCode == OP_UART_COPROC_RES) {
            g_partnerFreeSram   = rxPkt.freeSramBytes;
            g_partnerFreePsram  = rxPkt.freePsramBytes;
            g_partnerComputeUs  = rxPkt.computeTimeUs;
            g_partnerVectors[0] = rxPkt.shardVector[0];
            g_partnerVectors[1] = rxPkt.shardVector[1];
        }
    }
}

void transmitWirelessFrame(WhoamiWirelessPacket& frame) {
    strncpy(frame.signature, "@whoami", sizeof(frame.signature) - 1);
    frame.crc = computeWhoamiCrc(reinterpret_cast<const uint8_t*>(&frame), sizeof(WhoamiWirelessPacket) - 1);
    esp_now_send(WHOAMI_BROADCAST_MAC, reinterpret_cast<const uint8_t*>(&frame), sizeof(WhoamiWirelessPacket));
    triggerTxPulse();
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void onWirelessDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
#else
void onWirelessDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
#endif
    if (len != sizeof(WhoamiWirelessPacket)) return;
    WhoamiWirelessPacket pkt;
    memcpy(&pkt, data, sizeof(WhoamiWirelessPacket));
    uint8_t expected = computeWhoamiCrc(reinterpret_cast<const uint8_t*>(&pkt), sizeof(WhoamiWirelessPacket) - 1);
    if (pkt.magic != WHOAMI_PROTOCOL_MAGIC || pkt.crc != expected) {
        g_errorStateUntilMs = millis() + 1500;
        return;
    }

    uint32_t now = millis();
    g_lastMasterRxMs = now;

    if (g_nodeRole == ROLE_ESP_WITH_ANTENNA) {
        if (pkt.opCode == OP_MEM_ALLOC_DISPATCH) {
            g_activeJobId = pkt.jobId;
            strncpy(g_activeTaskBuf, pkt.payload, sizeof(g_activeTaskBuf) - 1);
            g_immediateDispatch = true;
        } else if (pkt.srcRole == ROLE_ESP_NO_ANTENNA && pkt.opCode == OP_UART_COPROC_RES) {
            g_lastCoprocRxMs    = now;
            g_partnerFreeSram   = pkt.freeSramCoproc;
            g_partnerFreePsram  = pkt.freePsramTotal;
            g_partnerComputeUs  = pkt.computeLatencyUs;
            g_partnerVectors[0] = pkt.tensorLogits[2];
            g_partnerVectors[1] = pkt.tensorLogits[3];
        }
    } else if (g_nodeRole == ROLE_ESP_NO_ANTENNA) {
        if (pkt.srcRole == ROLE_ESP_WITH_ANTENNA && pkt.opCode == OP_UART_COPROC_REQ) {
            g_lastCoprocRxMs = now;
            int32_t vecB[2]  = {0, 0};
            uint32_t compUs  = 0;
            executeMemoryTensorShard(pkt.payload, 1, vecB, compUs);

            WhoamiWirelessPacket res = {};
            res.magic            = WHOAMI_PROTOCOL_MAGIC;
            res.seq              = ++g_seqCounter;
            res.jobId            = pkt.jobId;
            res.opCode           = OP_UART_COPROC_RES;
            res.srcRole          = ROLE_ESP_NO_ANTENNA;
            res.clusterPairId    = g_clusterId;
            res.hwHasAntenna     = 0;
            res.freeSramCoproc   = ESP.getFreeHeap();
            res.freePsramTotal   = ESP.getFreePsram();
            res.computeLatencyUs = compUs;
            res.tensorLogits[2]  = vecB[0];
            res.tensorLogits[3]  = vecB[1];
            transmitWirelessFrame(res);
        }
    }
}

void initializeRfAndBleStack() {
#if WHOAMI_EXT_ANTENNA == 1
    pinMode(PIN_EXT_ANT_SWITCH, OUTPUT);
    digitalWrite(PIN_EXT_ANT_SWITCH, HIGH);
#else
    pinMode(PIN_EXT_ANT_SWITCH, INPUT_PULLDOWN);
#endif

    delay(esp_random() % 80);

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_ps(WIFI_PS_NONE);

#if WHOAMI_EXT_ANTENNA == 1
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
#else
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
#endif

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(WHOAMI_RF_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_max_tx_power(WHOAMI_TX_POWER_QDBM);

    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(onWirelessDataRecv);
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, WHOAMI_BROADCAST_MAC, 6);
        peer.channel = WHOAMI_RF_CHANNEL;
        peer.encrypt = false;
        if (!esp_now_is_peer_exist(WHOAMI_BROADCAST_MAC)) {
            esp_now_add_peer(&peer);
        }
    }

    uint8_t mac[6];
    WiFi.macAddress(mac);
    g_prefs.begin("whoami_cfg", false);
    g_clusterId = g_prefs.getUChar("cid", (mac[5] % 2) + 1);

    char bleId[32];
    snprintf(bleId, sizeof(bleId), "WHOAMI_%s_C%u_%02X",
             WHOAMI_EXT_ANTENNA ? "ANT" : "NOANT", g_clusterId, mac[5]);

    BLEDevice::init(bleId);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, WHOAMI_EXT_ANTENNA ? ESP_PWR_LVL_P9 : ESP_PWR_LVL_P6);
    BLEServer *pServer = BLEDevice::createServer();
    BLEService *pService = pServer->createService("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
    pAdvertising->setScanResponse(true);
    BLEDevice::startAdvertising();
}

void setup() {
    g_bootTimestampMs = millis();
    Serial.begin(115200);
    Serial2.begin(115200, SERIAL_8N1, PIN_UART_BRIDGE_RX, PIN_UART_BRIDGE_TX);
    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

    g_operationalRamPool = static_cast<uint8_t*>(malloc(OP_MEM_POOL_SIZE));
    if (g_operationalRamPool) {
        memset(g_operationalRamPool, 0xAA, OP_MEM_POOL_SIZE);
    }

    initializeRfAndBleStack();
    whoamiCustomSetup();

    xTaskCreatePinnedToCore(diagnosticLedTask, "WHOAMI_LED", 4096, NULL, 3, NULL, 0);

    Serial.println("==================================================================");
    Serial.printf("[SYSTEM_@whoami] OPERATIONAL MEMORY & COMPUTE NODE INITIALIZED\n");
    Serial.printf("[HARDWARE] PROFILE: %s | CLUSTER PAIR ID: #%u\n",
                  WHOAMI_EXT_ANTENNA ? "ESP_WITH_ANTENNA (2.44GHz SMA +20dBm)" : "ESP_WITHOUT_ANTENNA (UART CO-PROCESSOR)",
                  g_clusterId);
    Serial.printf("[MEMORY] LOCAL FREE SRAM: %lu Bytes | FREE PSRAM: %lu Bytes\n",
                  (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getFreePsram());
    Serial.println("==================================================================");
}

uint32_t g_lastCycleMs = 0;

void loop() {
    uint32_t now = millis();

    processIncomingUartBridge(Serial2);
    processIncomingUartBridge(Serial);
    whoamiCustomLoop(g_nodeRole, g_clusterId, g_customExtMetric);

    if (digitalRead(PIN_BOOT_BUTTON) == LOW) {
        delay(45);
        if (digitalRead(PIN_BOOT_BUTTON) == LOW) {
            g_clusterId = (g_clusterId % 4) + 1;
            g_prefs.putUChar("cid", g_clusterId);
            triggerTxPulse();
            Serial.printf("[CONFIG] Cluster Pair ID updated in NVS: #%u\n", g_clusterId);
            while (digitalRead(PIN_BOOT_BUTTON) == LOW) delay(10);
        }
    }

    if (g_nodeRole == ROLE_ESP_WITH_ANTENNA && (now - g_lastCycleMs >= 1000 || g_immediateDispatch)) {
        g_lastCycleMs       = now;
        g_immediateDispatch = false;
        g_activeJobId++;

        sendUartBridgePacket(OP_UART_COPROC_REQ, g_activeJobId, nullptr, 0, g_activeTaskBuf);

        WhoamiWirelessPacket req = {};
        req.magic         = WHOAMI_PROTOCOL_MAGIC;
        req.seq           = ++g_seqCounter;
        req.jobId         = g_activeJobId;
        req.opCode        = OP_UART_COPROC_REQ;
        req.srcRole       = ROLE_ESP_WITH_ANTENNA;
        req.clusterPairId = g_clusterId;
        strncpy(req.payload, g_activeTaskBuf, sizeof(req.payload) - 1);
        transmitWirelessFrame(req);

        int32_t vecA[2]  = {0, 0};
        uint32_t localUs = 0;
        executeMemoryTensorShard(g_activeTaskBuf, 0, vecA, localUs);

        uint32_t waitStart = millis();
        while (millis() - waitStart < 35) {
            processIncomingUartBridge(Serial2);
            processIncomingUartBridge(Serial);
            delay(2);
        }

        bool coprocOnline = (g_lastCoprocRxMs > 0) && ((millis() - g_lastCoprocRxMs) < 4000);
        uint32_t combinedSram  = ESP.getFreeHeap() + (coprocOnline ? g_partnerFreeSram : ESP.getFreeHeap());
        uint32_t combinedPsram = ESP.getFreePsram() + (coprocOnline ? g_partnerFreePsram : 0);

        WhoamiWirelessPacket report = {};
        report.magic              = WHOAMI_PROTOCOL_MAGIC;
        report.seq                = ++g_seqCounter;
        report.masterClockMs      = millis();
        report.jobId              = g_activeJobId;
        report.opCode             = OP_CLUSTER_STATE_REPORT;
        report.srcRole            = ROLE_ESP_WITH_ANTENNA;
        report.clusterPairId      = g_clusterId;
        report.coprocBridgeActive = coprocOnline ? 1 : 0;
        report.hwHasAntenna       = 1;
        report.rssiDbm            = WiFi.RSSI();
        report.freeSramMaster     = ESP.getFreeHeap();
        report.freeSramCoproc     = coprocOnline ? g_partnerFreeSram : ESP.getFreeHeap();
        report.freePsramTotal     = combinedPsram;
        report.computeLatencyUs   = localUs + g_partnerComputeUs;
        report.tensorLogits[0]    = vecA[0] ^ g_customExtMetric;
        report.tensorLogits[1]    = vecA[1];
        report.tensorLogits[2]    = g_partnerVectors[0];
        report.tensorLogits[3]    = g_partnerVectors[1];

        snprintf(report.payload, sizeof(report.payload), "@whoami_C%u[RAM:%luKB|%04X:%04X]",
                 g_clusterId,
                 (unsigned long)(combinedSram / 1024),
                 (unsigned int)(report.tensorLogits[0] & 0xFFFF),
                 (unsigned int)(g_partnerVectors[0] & 0xFFFF));

        transmitWirelessFrame(report);
    }

    if (g_nodeRole == ROLE_ESP_NO_ANTENNA && (now - g_lastCycleMs >= 800)) {
        g_lastCycleMs = now;
        int32_t vecB[2] = {0, 0};
        uint32_t compUs = 0;
        executeMemoryTensorShard("@whoami_MEM_SHARD_B", 1, vecB, compUs);
        vecB[0] ^= g_customExtMetric;
        sendUartBridgePacket(OP_UART_COPROC_RES, g_activeJobId, vecB, compUs, "@whoami_MEM_SHARD_B");
    }

    delay(5);
}
