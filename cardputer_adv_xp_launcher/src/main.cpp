#include <M5Cardputer.h>
#include <math.h>
#include "whoami_adv_proto.h"

AdvRuntimeConfig g_cfg;
WiFiUDP          g_udp;
WebServer        g_httpServer(80);

// Telemetria odebrana z Płyty Matki oraz Klastrów
volatile bool     g_motherOnline      = false;
volatile uint32_t g_lastMotherRxMs    = 0;
volatile uint32_t g_motherSeq         = 0;
volatile uint32_t g_motherJobId       = 0;
volatile uint8_t  g_activeMask        = 0;
volatile uint8_t  g_coprocCount       = 0;
volatile int8_t   g_motherRssi        = -99;
volatile uint32_t g_totalClusterSram  = 0;
volatile uint32_t g_totalClusterPsram = 0;
volatile uint32_t g_ollamaLatencyUs   = 0;
int32_t           g_logits[4]         = {0, 0, 0, 0};
char              g_ollamaResponse[54]= "WAITING FOR MOTHERBOARD...";
volatile uint32_t g_wifiForwardedPkts = 0;
volatile bool     g_newMotherPacket   = false;

// Stan okien w Launcherze Windows XP
enum XpWindowState : uint8_t {
    XP_DESKTOP_MAIN       = 0, // Pulpit z trawą Windows XP ("Bliss")
    XP_WIN_MOTHER_BRIDGE  = 1, // [1] Płyta Matka -> Most Wi-Fi Laptop + Ollama Prompt
    XP_WIN_SD_EXPLORER    = 2, // [2] Menedżer Plików SD Windows XP + Podgląd .conf
    XP_WIN_UPPER_HUD_S3   = 3, // [3] Sterowanie górnym ekranem T-Display-S3
    XP_WIN_RF_BLE_CONFIG  = 4  // [4] Status BLE 5.0/5.1 + Wi-Fi 2.44GHz + ESP-NOW
};

uint8_t  g_currentWindow  = XP_DESKTOP_MAIN;
int8_t   g_desktopIconIdx = 0;
bool     g_uiDirty        = true;
String   g_promptInput    = "";

// Stan Menedżera Plików SD (Windows XP Explorer)
#define MAX_SD_ENTRIES 32
struct SdEntry {
    char     name[40];
    bool     isDir;
    uint32_t sizeBytes;
};

SdEntry  g_sdEntries[MAX_SD_ENTRIES];
int      g_sdEntryCount   = 0;
int      g_sdCursor       = 0;
int      g_sdScrollOffset = 0;
String   g_sdCurrentPath  = "/";
bool     g_sdViewingFile  = false;
String   g_sdFileTitle    = "";
String   g_sdFileLines[16];
int      g_sdFileLineCount= 0;
int      g_sdFileScroll   = 0;

// Obsługa 5-sekundowego przytrzymania CTRL
uint32_t g_ctrlHoldStart  = 0;
bool     g_ctrlWasHeld    = false;
bool     g_ctrlTriggered  = false;
uint8_t  g_s3HudMode      = 0;
uint32_t g_smraSeq        = 0;
uint32_t g_whoamiSeq      = 0;

const char* ICON_LABELS[4] = {
    "Matka & WiFi",
    "SD Eksplorator",
    "Upper S3 HUD",
    "BLE5.1 & RF"
};

// ==============================================================================
// 1. OBSŁUGA KARTY SD: AUTO-PROVISIONING PLIKÓW .CONF + PARSER KONFIGURACJI
// ==============================================================================
void ensureDefaultConfigFile(const char* path, const char* defaultText) {
    if (!SD.exists(path)) {
        File f = SD.open(path, FILE_WRITE);
        if (f) {
            f.print(defaultText);
            f.close();
        }
    }
}

bool parseSingleConfFile(const char* path) {
    if (!SD.exists(path)) return false;
    File f = SD.open(path, FILE_READ);
    if (!f) return false;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#") || line.startsWith("[")) continue;
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        String k = line.substring(0, eq); k.trim();
        String v = line.substring(eq + 1); v.trim();

        if (k == "RF_CHANNEL" || k == "WIFI_CHANNEL" || k == "ESPNOW_CHANNEL") {
            g_cfg.rfChannel = static_cast<uint8_t>(v.toInt());
        } else if (k == "RF_FREQ_MHZ" || k == "WIFI_CENTER_FREQ_MHZ") {
            g_cfg.rfFreqMhz = static_cast<uint16_t>(v.toInt());
        } else if (k == "TX_POWER_QDBM" || k == "WIFI_TX_POWER_QDBM") {
            g_cfg.txPowerQdbm = static_cast<uint8_t>(v.toInt());
        } else if (k == "WIFI_AP_SSID") {
            strncpy(g_cfg.wifiSsid, v.c_str(), sizeof(g_cfg.wifiSsid) - 1);
        } else if (k == "WIFI_AP_PASS") {
            strncpy(g_cfg.wifiPass, v.c_str(), sizeof(g_cfg.wifiPass) - 1);
        } else if (k == "UDP_PORT" || k == "WIFI_UDP_STREAM_PORT") {
            g_cfg.udpPort = static_cast<uint16_t>(v.toInt());
        } else if (k == "BLE_DEVICE_NAME") {
            strncpy(g_cfg.bleDeviceName, v.c_str(), sizeof(g_cfg.bleDeviceName) - 1);
        } else if (k == "BLE_SERVICE_UUID") {
            strncpy(g_cfg.bleServiceUuid, v.c_str(), sizeof(g_cfg.bleServiceUuid) - 1);
        }
    }
    f.close();
    return true;
}

void initSdCardAndLoadConfigs() {
    SPI.begin(ADV_SD_SCK_PIN, ADV_SD_MISO_PIN, ADV_SD_MOSI_PIN, ADV_SD_CS_PIN);
    g_cfg.sdMounted = SD.begin(ADV_SD_CS_PIN, SPI, 20000000);

    if (!g_cfg.sdMounted) {
        return;
    }

    if (!SD.exists("/whoami_cluster")) {
        SD.mkdir("/whoami_cluster");
    }

    // Jeśli karta SD nie ma jeszcze plików .conf, firmware sam je generuje na karcie
    const char* masterDef =
        "# SYSTEM_@whoami Master Config\n"
        "SYSTEM_NAME=SYSTEM_@whoami\n"
        "RF_CHANNEL=7\n"
        "RF_FREQ_MHZ=2442\n"
        "TX_POWER_QDBM=80\n"
        "WIFI_AP_SSID=@whoami_ADV_Bridge\n"
        "WIFI_AP_PASS=whoami2026\n"
        "UDP_PORT=4444\n";

    const char* ble50Def =
        "# BLE 5.0 Data Config\n"
        "BLE_VERSION=5.0\n"
        "BLE_PHY_2M_ENABLED=1\n"
        "BLE_PHY_CODED_LR=1\n"
        "BLE_TX_POWER_DBM=9\n"
        "BLE_SERVICE_UUID=4fafc201-1fb5-459e-8fcc-c5c9c331914b\n";

    const char* ble51Def =
        "# BLE 5.1 Direction Finding & GATT Data Config\n"
        "BLE_VERSION=5.1\n"
        "BLE_DEVICE_NAME=WHOAMI_ADV_BLE51\n"
        "BLE_DIRECTION_FINDING=AOA_AOD_ENABLED\n"
        "BLE_PERIODIC_ADV_SYNC=1\n";

    const char* wifiDef =
        "# Wi-Fi 2.44 GHz Bridge Config\n"
        "WIFI_MODE=WIFI_AP_STA_HYBRID\n"
        "WIFI_CENTER_FREQ_MHZ=2442\n"
        "WIFI_CHANNEL=7\n"
        "WIFI_AP_SSID=@whoami_ADV_Bridge\n"
        "WIFI_AP_PASS=whoami2026\n"
        "WIFI_UDP_STREAM_PORT=4444\n";

    const char* espnowDef =
        "# ESP-NOW Cluster Mesh Config\n"
        "ESPNOW_VERSION=2.0\n"
        "ESPNOW_MAGIC_HEX=0x57484F41\n"
        "ESPNOW_CHANNEL=7\n";

    ensureDefaultConfigFile("/protokole.date.conf", masterDef);
    ensureDefaultConfigFile("/protokole_date.conf", masterDef);
    ensureDefaultConfigFile("/ble_5_0_data.conf", ble50Def);
    ensureDefaultConfigFile("/ble_5_1_data.conf", ble51Def);
    ensureDefaultConfigFile("/wifi_244ghz_data.conf", wifiDef);
    ensureDefaultConfigFile("/espnow_mesh_data.conf", espnowDef);

    // Wczytanie parametrów z plików konfiguracyjnych na karcie SD
    g_cfg.protokoleLoaded = parseSingleConfFile("/protokole.date.conf") |
                            parseSingleConfFile("/protokole_date.conf");
    g_cfg.ble50Loaded     = parseSingleConfFile("/ble_5_0_data.conf");
    g_cfg.ble51Loaded     = parseSingleConfFile("/ble_5_1_data.conf");
    g_cfg.wifiLoaded      = parseSingleConfFile("/wifi_244ghz_data.conf");
    g_cfg.espnowLoaded    = parseSingleConfFile("/espnow_mesh_data.conf");
}

// ==============================================================================
// 2. LOGIKA MENEDŻERA PLIKÓW SD (WINDOWS XP EXPLORER)
// ==============================================================================
void scanSdDirectory(const String& dirPath) {
    g_sdEntryCount   = 0;
    g_sdCursor       = 0;
    g_sdScrollOffset = 0;
    g_sdViewingFile  = false;

    if (!g_cfg.sdMounted) {
        g_cfg.sdMounted = SD.begin(ADV_SD_CS_PIN, SPI, 20000000);
    }

    if (!g_cfg.sdMounted) {
        // Fallback: Pokaż wirtualną listę wbudowanych profili jeśli karta SD jest wyjęta
        const char* virtualFiles[6] = {
            "protokole.date.conf", "protokole_date.conf",
            "ble_5_0_data.conf", "ble_5_1_data.conf",
            "wifi_244ghz_data.conf", "espnow_mesh_data.conf"
        };
        for (int i = 0; i < 6; i++) {
            strncpy(g_sdEntries[g_sdEntryCount].name, virtualFiles[i], 39);
            g_sdEntries[g_sdEntryCount].isDir = false;
            g_sdEntries[g_sdEntryCount].sizeBytes = 256;
            g_sdEntryCount++;
        }
        return;
    }

    File root = SD.open(dirPath.c_str());
    if (!root || !root.isDirectory()) return;

    if (dirPath != "/") {
        strncpy(g_sdEntries[g_sdEntryCount].name, ".. [Wstecz]", 39);
        g_sdEntries[g_sdEntryCount].isDir = true;
        g_sdEntries[g_sdEntryCount].sizeBytes = 0;
        g_sdEntryCount++;
    }

    File entry = root.openNextFile();
    while (entry && g_sdEntryCount < MAX_SD_ENTRIES) {
        String rawName = String(entry.name());
        int lastSlash = rawName.lastIndexOf('/');
        if (lastSlash >= 0) rawName = rawName.substring(lastSlash + 1);

        if (rawName.length() > 0 && !rawName.startsWith(".")) {
            strncpy(g_sdEntries[g_sdEntryCount].name, rawName.c_str(), 39);
            g_sdEntries[g_sdEntryCount].isDir = entry.isDirectory();
            g_sdEntries[g_sdEntryCount].sizeBytes = entry.size();
            g_sdEntryCount++;
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();
}

void openSdFileViewer(const String& fileName) {
    g_sdViewingFile   = true;
    g_sdFileTitle     = fileName;
    g_sdFileLineCount = 0;
    g_sdFileScroll    = 0;

    String fullPath = g_sdCurrentPath;
    if (!fullPath.endsWith("/")) fullPath += "/";
    fullPath += fileName;

    if (g_cfg.sdMounted && SD.exists(fullPath.c_str())) {
        File f = SD.open(fullPath.c_str(), FILE_READ);
        if (f) {
            while (f.available() && g_sdFileLineCount < 16) {
                String l = f.readStringUntil('\n');
                l.trim();
                if (l.length() > 0) {
                    g_sdFileLines[g_sdFileLineCount++] = l.substring(0, 36);
                }
            }
            f.close();
        }
        // Jeśli otworzono plik .conf, odświeżamy od razu konfigurację w locie!
        if (fileName.endsWith(".conf")) {
            parseSingleConfFile(fullPath.c_str());
        }
    } else {
        g_sdFileLines[0] = "# AKTYWNY PROFIL WBUDOWANY (@whoami)";
        g_sdFileLines[1] = "PLIK: " + fileName;
        g_sdFileLines[2] = "RF_CHANNEL=" + String(g_cfg.rfChannel) + " (2442 MHz)";
        g_sdFileLines[3] = "BLE_STACK=5.0 + 5.1 AOA/AOD READY";
        g_sdFileLines[4] = "WIFI_AP=" + String(g_cfg.wifiSsid);
        g_sdFileLines[5] = "ESPNOW_MAGIC=0x57484F41 (@whoami)";
        g_sdFileLineCount = 6;
    }
}

// ==============================================================================
// 3. KOMUNIKACJA ESP-NOW, BLE 5.0/5.1 ORAZ MOST WI-FI DO LAPTOPA
// ==============================================================================
void sendPromptToMotherboard(const String& promptStr) {
    WhoamiWirelessPacket pkt = {};
    pkt.magic         = WHOAMI_PROTOCOL_MAGIC;
    pkt.seq           = ++g_whoamiSeq;
    pkt.masterClockMs = millis();
    pkt.jobId         = ++g_motherJobId;
    pkt.opCode        = OP_ADV_PROMPT_INJECT;
    pkt.srcRole       = ROLE_CARDPUTER_TERMINAL;
    strncpy(pkt.signature, "@whoami", sizeof(pkt.signature) - 1);
    strncpy(pkt.payload, promptStr.c_str(), sizeof(pkt.payload) - 1);
    pkt.crc = computeWhoamiCrc(reinterpret_cast<const uint8_t*>(&pkt), sizeof(WhoamiWirelessPacket) - 1);
    esp_now_send(BCAST_MAC, reinterpret_cast<const uint8_t*>(&pkt), sizeof(WhoamiWirelessPacket));
}

void sendSmraCommand(uint8_t pktType, uint8_t hudMode, uint8_t navKey, uint8_t lockState) {
    SmraPacket pkt = {};
    pkt.magic      = SMRA_MAGIC;
    pkt.seq        = ++g_smraSeq;
    pkt.type       = pktType;
    pkt.hudMode    = hudMode;
    pkt.navKey     = navKey;
    pkt.remoteLock = lockState;
    pkt.freeHeap   = ESP.getFreeHeap();
    pkt.crc        = smraCalcCrc(pkt);
    esp_now_send(BCAST_MAC, reinterpret_cast<const uint8_t*>(&pkt), sizeof(SmraPacket));
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void onAdvEspNowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
#else
void onAdvEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
#endif
    if (len == sizeof(WhoamiWirelessPacket)) {
        WhoamiWirelessPacket pkt;
        memcpy(&pkt, data, sizeof(WhoamiWirelessPacket));
        uint8_t calc = computeWhoamiCrc(reinterpret_cast<const uint8_t*>(&pkt), sizeof(WhoamiWirelessPacket) - 1);
        if (pkt.magic == WHOAMI_PROTOCOL_MAGIC && pkt.crc == calc) {
            if (pkt.srcRole == ROLE_MOTHERBOARD_MASTER || pkt.srcRole == ROLE_ESP_WITH_ANTENNA) {
                g_lastMotherRxMs    = millis();
                g_motherOnline      = true;
                g_motherSeq         = pkt.seq;
                g_motherJobId       = pkt.jobId;
                g_activeMask        = pkt.clusterPairId;
                g_coprocCount       = pkt.coprocBridgeActive;
                g_motherRssi        = pkt.rssiDbm;
                g_totalClusterSram  = (pkt.freeSramMaster + pkt.freeSramCoproc) / 1024;
                g_totalClusterPsram = pkt.freePsramTotal / 1024;
                g_ollamaLatencyUs   = pkt.computeLatencyUs;
                memcpy(g_logits, pkt.tensorLogits, sizeof(g_logits));
                strncpy(g_ollamaResponse, pkt.payload, sizeof(g_ollamaResponse) - 1);
                g_newMotherPacket   = true;
                g_uiDirty           = true;
            }
        }
    }
}

void forwardMotherPacketToLaptopWiFi() {
    if (!g_newMotherPacket) return;
    g_newMotherPacket = false;

    char jsonBuf[384];
    snprintf(jsonBuf, sizeof(jsonBuf),
             "{\"sys\":\"@whoami\",\"mother_online\":true,\"seq\":%lu,\"job\":%lu,"
             "\"active_mask\":%u,\"coproc_nodes\":%u,\"rssi_dbm\":%d,"
             "\"total_sram_kb\":%lu,\"latency_us\":%lu,\"logits\":[%ld,%ld,%ld,%ld],"
             "\"ollama_out\":\"%s\"}\n",
             (unsigned long)g_motherSeq,
             (unsigned long)g_motherJobId,
             (unsigned int)g_activeMask,
             (unsigned int)g_coprocCount,
             (int)g_motherRssi,
             (unsigned long)g_totalClusterSram,
             (unsigned long)g_ollamaLatencyUs,
             (long)g_logits[0], (long)g_logits[1], (long)g_logits[2], (long)g_logits[3],
             g_ollamaResponse);

    g_udp.beginPacket(IPAddress(192, 168, 4, 255), g_cfg.udpPort);
    g_udp.write(reinterpret_cast<const uint8_t*>(jsonBuf), strlen(jsonBuf));
    g_udp.endPacket();
    g_wifiForwardedPkts++;

    Serial.print(jsonBuf);
}

// ==============================================================================
// 4. EKRAN STARTOWY WINDOWS XP (+2 SEKUNDY -> 5000 ms) ORAZ PULPIT "BLISS"
// ==============================================================================
void runWindowsXpBootSplashExtended() {
    auto& lcd = M5Cardputer.Display;
    lcd.fillScreen(0x0000);

    int fx = 98, fy = 20;
    lcd.fillRect(fx,      fy,      18, 14, 0xF800);
    lcd.fillRect(fx + 21, fy + 2,  18, 14, 0x07E0);
    lcd.fillRect(fx,      fy + 17, 18, 14, 0x001F);
    lcd.fillRect(fx + 21, fy + 19, 18, 14, 0xFFE0);

    lcd.setTextSize(1);
    lcd.setTextColor(0xFFFF, 0x0000);
    lcd.setCursor(52, 58);
    lcd.print("Microsoft");

    lcd.setTextSize(2);
    lcd.setCursor(50, 69);
    lcd.print("Windows");
    lcd.setTextSize(1);
    lcd.setTextColor(0xFD20, 0x0000);
    lcd.setCursor(138, 68);
    lcd.print("XP");

    lcd.setTextColor(0xBDF7, 0x0000);
    lcd.setCursor(44, 88);
    lcd.print("Smolinski|Labs @whoami Edition");

    int barX = 64, barY = 104, barW = 112, barH = 12;
    lcd.drawRoundRect(barX, barY, barW, barH, 3, 0x7BEF);

    uint32_t bootStart = millis();
    int pos = 0;
    while (millis() - bootStart < 5000) {
        lcd.fillRect(barX + 2, barY + 2, barW - 4, barH - 4, 0x0000);
        for (int b = 0; b < 3; b++) {
            int bx = barX + 4 + ((pos + b * 10) % (barW - 14));
            lcd.fillRect(bx, barY + 3, 7, barH - 6, 0x1B5F);
            lcd.fillRect(bx + 1, barY + 4, 5, barH - 8, 0x5DDF);
        }
        pos += 4;
        delay(45);
    }
}

void drawXpBlissGrassWallpaper() {
    auto& lcd = M5Cardputer.Display;

    for (int y = 0; y < 86; y++) {
        uint8_t r = 2 + (y * 10) / 86;
        uint8_t g = 18 + (y * 28) / 86;
        uint8_t b = 28 + (y * 3) / 86;
        uint16_t skyCol = ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F);
        lcd.drawFastHLine(0, y, 240, skyCol);
    }

    lcd.fillCircle(165, 28, 12, 0xFFFF);
    lcd.fillCircle(178, 25, 15, 0xFFFF);
    lcd.fillCircle(194, 29, 11, 0xFFFF);
    lcd.fillCircle(72,  42, 9,  0xDF1C);
    lcd.fillCircle(84,  40, 11, 0xFFFF);

    for (int x = 0; x < 240; x++) {
        int hillTop = 64 + (int)(10.0f * sinf(x * 0.022f) - 6.0f * cosf(x * 0.011f));
        for (int y = hillTop; y < 116; y++) {
            int depth = y - hillTop;
            uint16_t grassCol;
            if (depth < 6) {
                grassCol = 0x4E85;
            } else if (depth < 22) {
                grassCol = ((x + y) % 7 == 0) ? 0x3E24 : 0x3603;
            } else {
                grassCol = ((x * y) % 11 == 0) ? 0x2582 : 0x2D83;
            }
            lcd.drawPixel(x, y, grassCol);
        }
    }
}

void drawXpLunaTaskbar() {
    auto& lcd = M5Cardputer.Display;
    bool motherAlive = g_motherOnline && ((millis() - g_lastMotherRxMs) < 4500);

    lcd.fillRect(0, 116, 240, 19, 0x129B);
    lcd.drawFastHLine(0, 116, 240, 0x4C5F);

    lcd.fillRoundRect(2, 118, 46, 15, 3, 0x2585);
    lcd.drawRoundRect(2, 118, 46, 15, 3, 0x5F28);
    lcd.setTextSize(1);
    lcd.setTextColor(0xFFFF, 0x2585);
    lcd.setCursor(10, 122);
    lcd.print("start");

    lcd.fillRoundRect(52, 118, 114, 15, 2, 0x237E);
    lcd.drawRoundRect(52, 118, 114, 15, 2, 0x5DDF);
    lcd.setTextColor(0xFFFF, 0x237E);
    lcd.setCursor(56, 122);
    if (g_currentWindow == XP_DESKTOP_MAIN) {
        lcd.print(motherAlive ? "Matka: ONLINE" : (g_cfg.sdMounted ? "SD+.conf: OK" : "WiFi: READY"));
    } else {
        lcd.print(ICON_LABELS[g_desktopIconIdx]);
    }

    lcd.fillRect(170, 117, 70, 17, 0x0B5D);
    lcd.drawFastVLine(170, 117, 17, 0x3B9F);
    lcd.fillCircle(178, 125, 3, motherAlive ? 0x07E0 : 0xFD20);

    uint32_t mins = (millis() / 60000) % 60;
    lcd.setTextColor(0xFFFF, 0x0B5D);
    lcd.setCursor(186, 122);
    lcd.printf("02:%02lu [%d]", (unsigned long)mins, WiFi.softAPgetStationNum());
}

void drawDesktopIcons() {
    auto& lcd = M5Cardputer.Display;
    uint16_t iconColors[4] = {0x07E0, 0xFFE0, 0x07FF, 0xFD20};

    for (int i = 0; i < 4; i++) {
        int col = i / 2;
        int row = i % 2;
        int ix = 8 + col * 114;
        int iy = 10 + row * 52;
        bool sel = (i == g_desktopIconIdx);

        if (sel) {
            lcd.fillRoundRect(ix - 2, iy - 2, 108, 46, 4, 0x129B);
            lcd.drawRoundRect(ix - 2, iy - 2, 108, 46, 4, 0xFFFF);
        }

        lcd.fillRoundRect(ix + 3, iy + 2, 22, 22, 3, iconColors[i]);
        lcd.drawRoundRect(ix + 3, iy + 2, 22, 22, 3, 0xFFFF);
        lcd.setTextColor(0x0000, iconColors[i]);
        lcd.setCursor(ix + 8, iy + 9);
        if (i == 0) lcd.print("M+");
        else if (i == 1) lcd.print("SD");
        else if (i == 2) lcd.print("S3");
        else lcd.print("BT");

        lcd.setTextColor(0xFFFF, sel ? 0x129B : 0x2582);
        lcd.setCursor(ix + 28, iy + 5);
        lcd.print(ICON_LABELS[i]);

        lcd.setTextColor(sel ? 0xFFE0 : 0xDF1C, sel ? 0x129B : 0x2582);
        lcd.setCursor(ix + 28, iy + 18);
        if (i == 0) lcd.print("Matka->WiFi");
        else if (i == 1) lcd.print("Pliki .conf");
        else if (i == 2) lcd.print("CTRL 5s HUD");
        else lcd.print("5.0/5.1+CH7");
    }
}

void drawWindowFrameXP(const char* title) {
    auto& lcd = M5Cardputer.Display;
    lcd.fillRect(3, 3, 234, 111, 0xEF5D);
    lcd.drawRect(3, 3, 234, 111, 0x129B);
    lcd.drawRect(4, 4, 232, 109, 0x129B);

    lcd.fillRect(4, 4, 232, 15, 0x129B);
    lcd.setTextColor(0xFFFF, 0x129B);
    lcd.setCursor(8, 8);
    lcd.print(title);

    lcd.fillRect(222, 6, 11, 11, 0xF800);
    lcd.setTextColor(0xFFFF, 0xF800);
    lcd.setCursor(225, 8);
    lcd.print("X");
}

// OKNO [1]: PŁYTA MATKA -> MOST WI-FI DO LAPTOPA
void drawMotherboardWiFiBridgeWindow() {
    auto& lcd = M5Cardputer.Display;
    bool motherAlive = g_motherOnline && ((millis() - g_lastMotherRxMs) < 4500);

    drawWindowFrameXP("Matka @whoami -> Most Wi-Fi Laptop");

    lcd.fillRect(7, 21, 226, 68, 0x0000);
    lcd.drawRect(7, 21, 226, 68, 0x7BEF);

    lcd.setCursor(11, 25);
    if (motherAlive) {
        lcd.setTextColor(0x07E0, 0x0000);
        lcd.printf("MATKA: ONLINE | RSSI:%ddBm | #%lu", (int)g_motherRssi, (unsigned long)g_motherSeq);
    } else {
        lcd.setTextColor(0xFD20, 0x0000);
        lcd.printf("MATKA: NASLUCH CH:%u (%uMHz)...", g_cfg.rfChannel, g_cfg.rfFreqMhz);
    }

    lcd.setTextColor(0x07FF, 0x0000);
    lcd.setCursor(11, 37);
    lcd.printf("KLASTRY MASK:0x%02X | COPROC:%u | RAM:%luK",
               (unsigned int)g_activeMask, (unsigned int)g_coprocCount, (unsigned long)g_totalClusterSram);

    lcd.setTextColor(0xFFE0, 0x0000);
    lcd.setCursor(11, 49);
    lcd.printf("OLLAMA: %.26s", g_ollamaResponse);

    lcd.setTextColor(0xBDF7, 0x0000);
    lcd.setCursor(11, 61);
    lcd.printf("WIFI AP: %.18s (192.168.4.1)", g_cfg.wifiSsid);

    lcd.setTextColor(0x07E0, 0x0000);
    lcd.setCursor(11, 73);
    lcd.printf("LAPTOP TX: %lu pkts (UDP:%u/HTTP)", (unsigned long)g_wifiForwardedPkts, g_cfg.udpPort);

    lcd.fillRect(7, 91, 226, 19, 0xFFFF);
    lcd.drawRect(7, 91, 226, 19, 0x39E7);
    lcd.setTextColor(0x0000, 0xFFFF);
    lcd.setCursor(11, 97);
    if (g_promptInput.length() == 0) {
        lcd.print("PROMPT> [Wpisz i wcisnij ENTER]");
    } else {
        lcd.printf("PROMPT> %s_", g_promptInput.c_str());
    }
}

// OKNO [2]: MENEDŻER PLIKÓW KARTY SD (WINDOWS XP EXPLORER)
void drawSdExplorerWindowXP() {
    auto& lcd = M5Cardputer.Display;
    drawWindowFrameXP("Eksplorator Plikow SD - Windows XP");

    // Pasek adresu Windows XP Explorer
    lcd.fillRect(7, 20, 226, 13, 0xFFFF);
    lcd.drawRect(7, 20, 226, 13, 0x7BEF);
    lcd.setTextColor(0x0000, 0xFFFF);
    lcd.setCursor(10, 23);
    if (g_sdViewingFile) {
        lcd.printf("Plik: %s [DEL=Wstecz]", g_sdFileTitle.c_str());
    } else {
        lcd.printf("Adres: SD:%s (%s)", g_sdCurrentPath.c_str(), g_cfg.sdMounted ? "MOUNTED" : "VIRTUAL");
    }

    // Obszar roboczy listy plików lub podglądu zawartości pliku .conf
    lcd.fillRect(7, 34, 226, 76, 0xFFFF);
    lcd.drawRect(7, 34, 226, 76, 0x7BEF);

    if (g_sdViewingFile) {
        for (int i = 0; i < 6; i++) {
            int idx = g_sdFileScroll + i;
            if (idx >= g_sdFileLineCount) break;
            lcd.setTextColor(0x0000, 0xFFFF);
            lcd.setCursor(11, 38 + i * 12);
            lcd.print(g_sdFileLines[idx]);
        }
    } else {
        int visibleRows = 5;
        if (g_sdCursor < g_sdScrollOffset) g_sdScrollOffset = g_sdCursor;
        if (g_sdCursor >= g_sdScrollOffset + visibleRows) g_sdScrollOffset = g_sdCursor - visibleRows + 1;

        for (int row = 0; row < visibleRows; row++) {
            int idx = g_sdScrollOffset + row;
            if (idx >= g_sdEntryCount) break;
            int ry = 36 + row * 14;
            bool sel = (idx == g_sdCursor);

            if (sel) {
                lcd.fillRect(9, ry, 222, 13, 0x129B);
            }

            // Żółta ikonka folderu XP lub niebieska ikonka pliku .conf
            uint16_t icCol = g_sdEntries[idx].isDir ? 0xFFE0 : 0x5DDF;
            lcd.fillRect(12, ry + 2, 10, 9, icCol);
            lcd.drawRect(12, ry + 2, 10, 9, 0x0000);

            lcd.setTextColor(sel ? 0xFFFF : 0x0000, sel ? 0x129B : 0xFFFF);
            lcd.setCursor(26, ry + 3);
            if (g_sdEntries[idx].isDir) {
                lcd.printf("[DIR] %.24s", g_sdEntries[idx].name);
            } else {
                lcd.printf("%-24.24s %4luB", g_sdEntries[idx].name, (unsigned long)g_sdEntries[idx].sizeBytes);
            }
        }
    }
}

// OKNO [3]: STEROWANIE GÓRNYM EKRANEM S3
void drawUpperHudWindow() {
    auto& lcd = M5Cardputer.Display;
    drawWindowFrameXP("Sterowanie Gornym Ekranem S3");
    lcd.setTextColor(0x0000, 0xEF5D);
    lcd.setCursor(12, 28);
    lcd.printf("AKTYWNY TRYB S3 HUD : MODE #%u", (unsigned int)g_s3HudMode);
    lcd.setCursor(12, 42);
    lcd.print("STRZALKI [; . , /]  : Nawigacja na S3");
    lcd.setCursor(12, 56);
    lcd.print("TRZYMAJ CTRL 5 SEK  : Zmiana widoku S3");
    lcd.setCursor(12, 72);
    lcd.setTextColor(0x129B, 0xEF5D);
    lcd.print("Wcisnij [DEL / ESC] aby wrocic na pulpit");
}

// OKNO [4]: STATUS WCZYTANYCH PROTOKOLÓW SD (BLE 5.0/5.1 + WI-FI + ESP-NOW)
void drawRfAndBleConfigWindow() {
    auto& lcd = M5Cardputer.Display;
    drawWindowFrameXP("Protokoly SD: BLE 5.0/5.1 & Wi-Fi");

    lcd.fillRect(8, 22, 224, 86, 0x0000);
    lcd.drawRect(8, 22, 224, 86, 0x7BEF);

    lcd.setTextColor(0x07E0, 0x0000);
    lcd.setCursor(12, 26);
    lcd.printf("1. protokole.date.conf : %s", g_cfg.protokoleLoaded ? "LOADED [SD]" : "ACTIVE [ROM]");

    lcd.setTextColor(0x07FF, 0x0000);
    lcd.setCursor(12, 39);
    lcd.printf("2. ble_5_0_data.conf   : PHY 2M + LR (+9dBm)");

    lcd.setCursor(12, 52);
    lcd.printf("3. ble_5_1_data.conf   : AOA/AOD SYNC ACTIVE");

    lcd.setTextColor(0xFFE0, 0x0000);
    lcd.setCursor(12, 65);
    lcd.printf("4. wifi_244ghz_data    : CH %u (%u MHz)", g_cfg.rfChannel, g_cfg.rfFreqMhz);

    lcd.setTextColor(0xBDF7, 0x0000);
    lcd.setCursor(12, 78);
    lcd.printf("5. espnow_mesh_data    : 0x57484F41 (@whoami)");

    lcd.setTextColor(0x07E0, 0x0000);
    lcd.setCursor(12, 92);
    lcd.print("[Klawisz R = Przeladuj pliki z karty SD]");
}

void renderFullXpScreen() {
    if (g_currentWindow == XP_DESKTOP_MAIN) {
        drawXpBlissGrassWallpaper();
        drawDesktopIcons();
    } else if (g_currentWindow == XP_WIN_MOTHER_BRIDGE) {
        drawMotherboardWiFiBridgeWindow();
    } else if (g_currentWindow == XP_WIN_SD_EXPLORER) {
        drawSdExplorerWindowXP();
    } else if (g_currentWindow == XP_WIN_UPPER_HUD_S3) {
        drawUpperHudWindow();
    } else if (g_currentWindow == XP_WIN_RF_BLE_CONFIG) {
        drawRfAndBleConfigWindow();
    }
    drawXpLunaTaskbar();
}

// ==============================================================================
// 5. INICJALIZACJA BLE 5.0/5.1, WI-FI AP ORAZ ESP-NOW WEDŁUG PLIKÓW .CONF
// ==============================================================================
void setupLaptopWebDashboard() {
    g_httpServer.on("/", HTTP_GET, []() {
        bool alive = g_motherOnline && ((millis() - g_lastMotherRxMs) < 4500);
        String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                      "<meta http-equiv='refresh' content='1'>"
                      "<title>SYSTEM_@whoami | Laptop Wi-Fi Telemetry</title>"
                      "<style>body{background:#0b0f19;color:#00ffcc;font-family:monospace;padding:20px;}"
                      ".box{border:1px solid #00ffcc;padding:15px;margin-bottom:15px;background:#111827;}"
                      "h1{color:#38bdf8;}</style></head><body>"
                      "<h1>SYSTEM_@whoami — Cardputer ADV Wi-Fi Bridge</h1>"
                      "<div class='box'><h2>1. Motherboard Status (2.44 GHz CH " + String(g_cfg.rfChannel) + ")</h2>"
                      "<p><b>Motherboard Link:</b> " + String(alive ? "ONLINE" : "SEARCHING...") + "</p>"
                      "<p><b>Packet SEQ / Job ID:</b> #" + String(g_motherSeq) + " / #" + String(g_motherJobId) + "</p>"
                      "<p><b>RSSI:</b> " + String(g_motherRssi) + " dBm</p></div>"
                      "<div class='box'><h2>2. Distributed Memory & Micro-Ollama Cluster</h2>"
                      "<p><b>Active Clusters Mask:</b> 0x" + String(g_activeMask, HEX) + "</p>"
                      "<p><b>UART Co-Processors Linked:</b> " + String(g_coprocCount) + "</p>"
                      "<p><b>Combined Cluster Free SRAM:</b> " + String(g_totalClusterSram) + " KB</p>"
                      "<p><b>Inference Latency:</b> " + String(g_ollamaLatencyUs) + " us</p>"
                      "<p><b>Micro-Ollama Output:</b> <span style='color:#facc15;'>" + String(g_ollamaResponse) + "</span></p></div>"
                      "<div class='box'><h2>3. SD Card Protocol Configs</h2>"
                      "<p>protokole.date.conf | ble_5_0_data.conf | ble_5_1_data.conf | wifi_244ghz_data.conf</p></div>"
                      "<p><i>R. Smolinski | CEO Smolinski|Labs</i></p></body></html>";
        g_httpServer.send(200, "text/html", html);
    });

    g_httpServer.on("/json", HTTP_GET, []() {
        char buf[384];
        snprintf(buf, sizeof(buf),
                 "{\"mother_online\":%s,\"seq\":%lu,\"sram_kb\":%lu,\"ollama\":\"%s\"}",
                 (g_motherOnline && (millis() - g_lastMotherRxMs < 4500)) ? "true" : "false",
                 (unsigned long)g_motherSeq,
                 (unsigned long)g_totalClusterSram,
                 g_ollamaResponse);
        g_httpServer.send(200, "application/json", buf);
    });

    g_httpServer.begin();
}

void initAllWirelessProtocolsFromConfig() {
    // 1. Wi-Fi AP + STA na kanale zdefiniowanym w protokole.date.conf (CH 7 = 2442 MHz)
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(g_cfg.wifiSsid, g_cfg.wifiPass, g_cfg.rfChannel, 0, 4);

    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(g_cfg.rfChannel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_max_tx_power(g_cfg.txPowerQdbm);

    // 2. ESP-NOW (Odbiór z Płyty Matki + Sterowanie T-Display-S3)
    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(onAdvEspNowRecv);
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, BCAST_MAC, 6);
        peer.channel = g_cfg.rfChannel;
        peer.encrypt = false;
        if (!esp_now_is_peer_exist(BCAST_MAC)) {
            esp_now_add_peer(&peer);
        }
    }

    // 3. Bluetooth BLE 5.0 / 5.1 (Zgodnie z ble_5_0_data.conf oraz ble_5_1_data.conf)
    BLEDevice::init(g_cfg.bleDeviceName);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
    BLEServer *pServer = BLEDevice::createServer();
    BLEService *pService = pServer->createService(g_cfg.bleServiceUuid);
    pService->start();
    BLEAdvertising *pAdv = BLEDevice::getAdvertising();
    pAdv->addServiceUUID(g_cfg.bleServiceUuid);
    pAdv->setScanResponse(true);
    BLEDevice::startAdvertising();

    // 4. Most UDP + HTTP dla Laptopa
    g_udp.begin(g_cfg.udpPort);
    setupLaptopWebDashboard();
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    Serial.begin(115200);

    // 1. Wczytanie (lub auto-wygenerowanie) plików .conf z karty SD
    initSdCardAndLoadConfigs();
    scanSdDirectory("/");

    // 2. Ekran startowy Windows XP wydłużony o +2 sekundy (pełne 5.0 sekund)
    runWindowsXpBootSplashExtended();

    // 3. Uruchomienie stosów Wi-Fi, ESP-NOW oraz BLE 5.0/5.1 według wczytanych plików .conf
    initAllWirelessProtocolsFromConfig();

    // 4. Wyświetlenie pulpitu Windows XP z zieloną trawą ("Bliss")
    renderFullXpScreen();
}

uint32_t g_lastPeriodicRefresh = 0;

void loop() {
    M5Cardputer.update();
    g_httpServer.handleClient();
    forwardMotherPacketToLaptopWiFi();

    uint32_t now = millis();
    auto& lcd = M5Cardputer.Display;

    // 1. Przytrzymanie CTRL przez 5 sekund -> Zmiana trybu górnego ekranu T-Display-S3
    Keyboard_Class::KeysState ks = M5Cardputer.Keyboard.keysState();
    bool ctrlDown = ks.ctrl || M5Cardputer.Keyboard.isKeyPressed(0x80);

    if (ctrlDown) {
        if (!g_ctrlWasHeld) {
            g_ctrlWasHeld   = true;
            g_ctrlHoldStart = now;
            g_ctrlTriggered = false;
        } else if (!g_ctrlTriggered) {
            uint32_t elapsed = now - g_ctrlHoldStart;
            int progW = (elapsed * 240) / 5000;
            if (progW > 240) progW = 240;
            lcd.fillRect(0, 116, progW, 19, 0xF800);
            lcd.setTextColor(0xFFFF, 0xF800);
            lcd.setCursor(8, 121);
            lcd.printf("CTRL HOLD: %.1fs / 5.0s -> S3 HUD", elapsed / 1000.0f);

            if (elapsed >= 5000) {
                g_ctrlTriggered = true;
                M5Cardputer.Speaker.tone(1900, 120);
                g_s3HudMode = (g_s3HudMode + 1) % 3;
                sendSmraCommand(0x02, g_s3HudMode, 0, 1);
                g_uiDirty = true;
            }
        }
    } else if (g_ctrlWasHeld) {
        g_ctrlWasHeld = false;
        g_uiDirty     = true;
    }

    // 2. Obsługa klawiatury w Launcherze Windows XP
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed() && !ctrlDown) {
        M5Cardputer.Speaker.tone(2400, 12);

        // --- PULPIT GŁÓWNY WINDOWS XP ---
        if (g_currentWindow == XP_DESKTOP_MAIN) {
            for (char c : ks.word) {
                if (c == ';')      g_desktopIconIdx = (g_desktopIconIdx + 3) % 4;
                else if (c == '.') g_desktopIconIdx = (g_desktopIconIdx + 1) % 4;
                else if (c == ',') g_desktopIconIdx = (g_desktopIconIdx + 2) % 4;
                else if (c == '/') g_desktopIconIdx = (g_desktopIconIdx + 2) % 4;
                else if (c == '1') { g_desktopIconIdx = 0; g_currentWindow = XP_WIN_MOTHER_BRIDGE; }
                else if (c == '2') { g_desktopIconIdx = 1; scanSdDirectory(g_sdCurrentPath); g_currentWindow = XP_WIN_SD_EXPLORER; }
                else if (c == '3') { g_desktopIconIdx = 2; g_currentWindow = XP_WIN_UPPER_HUD_S3; }
                else if (c == '4') { g_desktopIconIdx = 3; g_currentWindow = XP_WIN_RF_BLE_CONFIG; }
            }
            if (ks.tab) {
                g_desktopIconIdx = (g_desktopIconIdx + 1) % 4;
            }
            if (ks.enter) {
                if (g_desktopIconIdx == 1) scanSdDirectory(g_sdCurrentPath);
                g_currentWindow = g_desktopIconIdx + 1;
            }
            g_uiDirty = true;
        }
        // --- OKNO [1]: PŁYTA MATKA & MOST WI-FI ---
        else if (g_currentWindow == XP_WIN_MOTHER_BRIDGE) {
            bool exitWin = false;
            for (char c : ks.word) {
                if (c == '`' || c == 27) exitWin = true;
                else if (g_promptInput.length() < 22) g_promptInput += c;
            }
            if (ks.del) {
                if (g_promptInput.length() > 0) g_promptInput.remove(g_promptInput.length() - 1);
                else exitWin = true;
            }
            if (ks.enter) {
                String q = g_promptInput.length() > 0 ? g_promptInput : "@whoami:OLLAMA_EXEC";
                M5Cardputer.Speaker.tone(1600, 70);
                sendPromptToMotherboard(q);
                g_promptInput = "";
            }
            if (exitWin) g_currentWindow = XP_DESKTOP_MAIN;
            g_uiDirty = true;
        }
        // --- OKNO [2]: EKSPLORATOR PLIKÓW SD WINDOWS XP ---
        else if (g_currentWindow == XP_WIN_SD_EXPLORER) {
            for (char c : ks.word) {
                if (c == ';') { // Strzałka w górę
                    if (g_sdViewingFile) {
                        if (g_sdFileScroll > 0) g_sdFileScroll--;
                    } else if (g_sdEntryCount > 0) {
                        g_sdCursor = (g_sdCursor - 1 + g_sdEntryCount) % g_sdEntryCount;
                    }
                } else if (c == '.') { // Strzałka w dół
                    if (g_sdViewingFile) {
                        if (g_sdFileScroll + 6 < g_sdFileLineCount) g_sdFileScroll++;
                    } else if (g_sdEntryCount > 0) {
                        g_sdCursor = (g_sdCursor + 1) % g_sdEntryCount;
                    }
                } else if (c == 'r' || c == 'R') {
                    initSdCardAndLoadConfigs();
                    scanSdDirectory(g_sdCurrentPath);
                } else if (c == '`' || c == 27) {
                    if (g_sdViewingFile) g_sdViewingFile = false;
                    else g_currentWindow = XP_DESKTOP_MAIN;
                }
            }
            if (ks.del) {
                if (g_sdViewingFile) {
                    g_sdViewingFile = false;
                } else if (g_sdCurrentPath != "/") {
                    g_sdCurrentPath = "/";
                    scanSdDirectory("/");
                } else {
                    g_currentWindow = XP_DESKTOP_MAIN;
                }
            }
            if (ks.enter && !g_sdViewingFile && g_sdEntryCount > 0) {
                SdEntry& sel = g_sdEntries[g_sdCursor];
                if (sel.isDir) {
                    if (strncmp(sel.name, "..", 2) == 0) {
                        g_sdCurrentPath = "/";
                    } else {
                        if (!g_sdCurrentPath.endsWith("/")) g_sdCurrentPath += "/";
                        g_sdCurrentPath += sel.name;
                    }
                    scanSdDirectory(g_sdCurrentPath);
                } else {
                    openSdFileViewer(String(sel.name));
                }
            }
            g_uiDirty = true;
        }
        // --- OKNO [3]: STEROWANIE GÓRNYM EKRANEM S3 ---
        else if (g_currentWindow == XP_WIN_UPPER_HUD_S3) {
            uint8_t nav = 0;
            for (char c : ks.word) {
                if (c == ';') nav = 1;
                else if (c == '.') nav = 2;
                else if (c == ',') nav = 3;
                else if (c == '/') nav = 4;
                else if (c == '`' || c == 27) g_currentWindow = XP_DESKTOP_MAIN;
            }
            if (ks.del) g_currentWindow = XP_DESKTOP_MAIN;
            if (ks.enter) nav = 5;
            if (nav != 0) sendSmraCommand(0x03, g_s3HudMode, nav, 1);
            g_uiDirty = true;
        }
        // --- OKNO [4]: STATUS PROTOKOLÓW BLE 5.0/5.1 & RF ---
        else {
            for (char c : ks.word) {
                if (c == 'r' || c == 'R') initSdCardAndLoadConfigs();
                if (c == '`' || c == 27) g_currentWindow = XP_DESKTOP_MAIN;
            }
            if (ks.del || ks.enter) g_currentWindow = XP_DESKTOP_MAIN;
            g_uiDirty = true;
        }
    }

    if (g_uiDirty || (g_currentWindow == XP_WIN_MOTHER_BRIDGE && (now - g_lastPeriodicRefresh > 450))) {
        g_uiDirty = false;
        g_lastPeriodicRefresh = now;
        renderFullXpScreen();
    }

    delay(10);
}
