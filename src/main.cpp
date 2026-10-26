#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <math.h>
#include "config.h"

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

TelemetryPacket rxPacket;
volatile bool newFrameReady = false;
uint32_t lastPacketMs = 0;
uint8_t currentView = 0;
uint8_t backlightLevel = 180;

bool sdReady = false;
bool advAntennaEnabled = false;
AdvScheduleItem advSchedule = {0, 0, false};

static uint8_t peakHold[64] = {0};
static uint8_t waterfall[64][96] = {{0}};

static const uint16_t COLOR_BG = 0x0000;
static const uint16_t COLOR_PANEL = 0x1065;
static const uint16_t COLOR_WHITE = 0xFFFF;
static const uint16_t COLOR_CYAN = 0x07FF;
static const uint16_t COLOR_GREEN = 0x07E0;
static const uint16_t COLOR_YELLOW = 0xFFE0;
static const uint16_t COLOR_RED = 0xF800;
static const uint16_t COLOR_BLUE = 0x001F;
static const uint16_t COLOR_MUTED = 0x8410;

LGFX_Sprite canvas(&tft);

bool initSdCard() {
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

    if (!SD.begin(SD_CS_PIN, SPI)) {
        Serial.println("[SD] INIT FAILED");
        return false;
    }

    if (!SD.exists("/logs")) {
        SD.mkdir("/logs");
    }

    if (!SD.exists("/cfg")) {
        SD.mkdir("/cfg");
    }

    File bootInfo = SD.open("/logs/boot.log", FILE_WRITE);
    if (bootInfo) {
        bootInfo.println("Smolinski|Labs boot");
        bootInfo.close();
    }

    sdReady = true;
    Serial.println("[SD] READY");
    return true;
}

void appendSdLog(const String& msg) {
    if (!sdReady) {
        return;
    }

    File logFile = SD.open("/logs/system.log", FILE_APPEND);
    if (!logFile) {
        Serial.println("[SD] Cannot open log file");
        return;
    }

    logFile.print(millis());
    logFile.print(" - ");
    logFile.println(msg);
    logFile.close();
}

bool saveTelemetrySnapshot(const TelemetryPacket& pkt) {
    if (!sdReady) {
        return false;
    }

    File f = SD.open("/cfg/last_packet.txt", FILE_WRITE);
    if (!f) {
        return false;
    }

    f.printf("magic=%u\nactiveModule=%u\nfrequencyHz=%lu\nrssiDbm=%d\n",
             pkt.magic,
             pkt.activeModule,
             pkt.frequencyHz,
             pkt.rssiDbm);

    for (int i = 0; i < 64; ++i) {
        f.printf("%d%c", pkt.spectrumBins[i], (i == 63) ? '\n' : ',');
    }

    f.close();
    return true;
}

void setAdvAntennaState(bool enabled) {
    advAntennaEnabled = enabled;
    pinMode(PIN_ANTENNA_SW, OUTPUT);
    digitalWrite(PIN_ANTENNA_SW, enabled ? HIGH : LOW);
}

void scheduleAdvWindow(uint32_t delayMs, uint32_t durationMs, bool enabled) {
    advSchedule.enabled = enabled;
    advSchedule.startAtMs = millis() + delayMs;
    advSchedule.endAtMs = advSchedule.startAtMs + durationMs;

    if (!enabled) {
        setAdvAntennaState(false);
    }
}

void updateAdvScheduler() {
    if (!advSchedule.enabled) {
        setAdvAntennaState(false);
        return;
    }

    uint32_t now = millis();
    if (now < advSchedule.startAtMs) {
        setAdvAntennaState(false);
        return;
    }

    if (now < advSchedule.endAtMs) {
        setAdvAntennaState(true);
        return;
    }

    setAdvAntennaState(false);
    advSchedule.enabled = false;
}

const char* moduleName(uint8_t module) {
    switch (module) {
        case 1: return "CC1101";
        case 2: return "nRF24";
        default: return "IDLE";
    }
}

float mhzFromHz(uint32_t hz) {
    return (float)hz / 1000000.0f;
}

uint16_t dbmToColor(int16_t dbm) {
    // Thermal palette from -95 dBm (blue/cyan) to -40 dBm (yellow/red)
    if (dbm <= -95) return COLOR_BLUE;
    if (dbm >= -40) return COLOR_RED;

    float t = (float)(dbm + 95) / 55.0f;
    t = constrain(t, 0.0f, 1.0f);

    uint8_t r = 0, g = 0, b = 0;
    if (t < 0.25f) {
        float u = t / 0.25f;
        r = 0;
        g = (uint8_t)(64 + u * 160);
        b = 255;
    } else if (t < 0.50f) {
        float u = (t - 0.25f) / 0.25f;
        r = 0;
        g = 224 + (uint8_t)(u * 31);
        b = (uint8_t)(255 - u * 180);
    } else if (t < 0.75f) {
        float u = (t - 0.50f) / 0.25f;
        r = (uint8_t)(u * 255);
        g = 255;
        b = 0;
    } else {
        float u = (t - 0.75f) / 0.25f;
        r = 255;
        g = (uint8_t)(255 - u * 180);
        b = 0;
    }

    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

uint16_t blendColor(uint16_t a, uint16_t b, float mix) {
    uint8_t ar = (a >> 11) & 0x1F;
    uint8_t ag = (a >> 5) & 0x3F;
    uint8_t ab = a & 0x1F;

    uint8_t br = (b >> 11) & 0x1F;
    uint8_t bg = (b >> 5) & 0x3F;
    uint8_t bb = b & 0x1F;

    uint8_t rr = (uint8_t)(ar + (br - ar) * mix);
    uint8_t gg = (uint8_t)(ag + (bg - ag) * mix);
    uint8_t bbv = (uint8_t)(ab + (bb - ab) * mix);

    return (rr << 11) | (gg << 5) | bbv;
}

void drawStatusBar(const TelemetryPacket& pkt, bool lost) {
    canvas.fillRect(0, 0, SCREEN_W, 24, COLOR_PANEL);
    canvas.drawFastHLine(0, 23, SCREEN_W, COLOR_CYAN);

    canvas.setTextColor(COLOR_WHITE, COLOR_PANEL);
    canvas.setTextSize(1);
    canvas.setCursor(6, 6);

    if (lost) {
        canvas.print("NO SIGNAL FROM ADV");
        return;
    }

    canvas.printf("CHIP:%s", moduleName(pkt.activeModule));
    canvas.setCursor(122, 6);
    canvas.printf("FREQ:%.2f MHz", mhzFromHz(pkt.frequencyHz));
    canvas.setCursor(234, 6);
    canvas.printf("RSSI:%d dBm", pkt.rssiDbm);
}

void drawBarGraph(const TelemetryPacket& pkt) {
    const int graphX = 8;
    const int graphY = 34;
    const int graphW = SCREEN_W - 16;
    const int graphH = SCREEN_H - 46;

    canvas.fillRoundRect(graphX, graphY, graphW, graphH, 8, 0x0841);
    canvas.drawRoundRect(graphX, graphY, graphW, graphH, 8, COLOR_CYAN);

    for (int i = 0; i <= 8; ++i) {
        int x = graphX + (graphW * i) / 8;
        canvas.drawFastVLine(x, graphY, graphH, 0x3186);
    }
    for (int i = 0; i <= 6; ++i) {
        int y = graphY + (graphH * i) / 6;
        canvas.drawFastHLine(graphX, y, graphW, 0x3186);
    }

    int barGap = 2;
    int barW = (graphW - barGap * 63) / 64;

    for (int i = 0; i < 64; ++i) {
        int value = pkt.spectrumBins[i];
        int barH = map(value, 0, 255, 0, graphH - 4);
        int x = graphX + i * (barW + barGap);
        int y = graphY + graphH - barH;

        int16_t dbm = map(i, 0, 63, -95, -40) - (int16_t)value / 8;
        uint16_t color = dbmToColor(dbm);
        canvas.fillRect(x, y, barW, barH, color);

        if (value > peakHold[i]) {
            peakHold[i] = value;
        } else {
            if (peakHold[i] > 0) {
                peakHold[i] = max(0, (int)peakHold[i] - 2);
            }
        }

        int peakY = graphY + graphH - map(peakHold[i], 0, 255, 0, graphH - 4);
        canvas.drawFastHLine(x, peakY, barW, COLOR_WHITE);
    }

    canvas.setTextColor(COLOR_WHITE, 0x0841);
    canvas.setTextSize(1);
    canvas.setCursor(graphX + 4, graphY + 4);
    canvas.print("BAR GRAPH");
}

void drawWaterfall(const TelemetryPacket& pkt) {
    const int graphX = 8;
    const int graphY = 34;
    const int graphW = SCREEN_W - 16;
    const int graphH = SCREEN_H - 46;

    // Shift history upward
    for (int y = 0; y < 95; ++y) {
        for (int x = 0; x < 64; ++x) {
            waterfall[x][y] = waterfall[x][y + 1];
        }
    }

    for (int x = 0; x < 64; ++x) {
        waterfall[x][95] = pkt.spectrumBins[x];
    }

    canvas.fillRoundRect(graphX, graphY, graphW, graphH, 8, 0x0000);
    canvas.drawRoundRect(graphX, graphY, graphW, graphH, 8, COLOR_CYAN);

    for (int x = 0; x < 64; ++x) {
        int px = graphX + map(x, 0, 63, 0, graphW - 1);
        for (int y = 0; y < graphH; ++y) {
            uint8_t v = waterfall[x][(y * 96) / graphH];
            if (v == 0) continue;

            int16_t dbm = map(x, 0, 63, -95, -40) - (int16_t)v / 8;
            uint16_t color = dbmToColor(dbm);
            canvas.drawFastVLine(px, graphY + y, 1, color);
        }
    }

    canvas.setTextColor(COLOR_WHITE, 0x0000);
    canvas.setTextSize(1);
    canvas.setCursor(graphX + 4, graphY + 4);
    canvas.print("WATERFALL");
}

void renderHUD(const TelemetryPacket& pkt) {
    const bool lost = (millis() - lastPacketMs) > 500UL;
    canvas.fillScreen(COLOR_BG);
    drawStatusBar(pkt, lost);

    if (lost) {
        canvas.setTextColor(COLOR_RED, COLOR_BG);
        canvas.setTextSize(2);
        canvas.setCursor(38, 84);
        canvas.print("NO SIGNAL FROM ADV");
        canvas.pushSprite(0, 0);
        return;
    }

    if (currentView == 0) {
        drawBarGraph(pkt);
    } else {
        drawWaterfall(pkt);
    }

    canvas.pushSprite(0, 0);
}

void onTelemetryReceive(const uint8_t* mac, const uint8_t* incomingData, int len) {
    if (incomingData == nullptr || len != sizeof(TelemetryPacket)) {
        return;
    }

    if (incomingData[0] != 0xA5) {
        return;
    }

    memcpy(&rxPacket, incomingData, sizeof(TelemetryPacket));
    newFrameReady = true;
    lastPacketMs = millis();

    if (sdReady) {
        saveTelemetrySnapshot(rxPacket);
        appendSdLog("telemetry captured");
    }
}

void setBrightness(uint8_t value) {
    backlightLevel = constrain(value, 20, 255);
    tft.setBrightness(backlightLevel);
}

void pollButtons() {
    static bool lastTop = HIGH;
    static bool lastBottom = HIGH;

    bool topPressed = (digitalRead(PIN_BTN_TOP) == LOW);
    bool bottomPressed = (digitalRead(PIN_BTN_BOTTOM) == LOW);

    if (topPressed && !lastTop) {
        currentView = (currentView + 1) & 1;
    }

    if (bottomPressed && !lastBottom) {
        backlightLevel = constrain((int)backlightLevel - 25, 20, 255);
        setBrightness(backlightLevel);
    }

    lastTop = topPressed;
    lastBottom = bottomPressed;
}

void setup() {
    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, HIGH);

    pinMode(PIN_BTN_TOP, INPUT_PULLUP);
    pinMode(PIN_BTN_BOTTOM, INPUT_PULLUP);

    pinMode(PIN_ANTENNA_SW, OUTPUT);
    digitalWrite(PIN_ANTENNA_SW, LOW);

    Serial.begin(115200);
    delay(100);

    initSdCard();
    appendSdLog("system boot");

    tft.init();
    tft.setRotation(1);
    tft.setBrightness(backlightLevel);

    canvas.setColorDepth(16);
    canvas.createSprite(SCREEN_W, SCREEN_H);
    canvas.setSwapBytes(true);

    memset(&rxPacket, 0, sizeof(rxPacket));
    memset(peakHold, 0, sizeof(peakHold));
    memset(waterfall, 0, sizeof(waterfall));

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(50);

    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(esp_now_recv_cb_t(onTelemetryReceive));
    }

    renderHUD(rxPacket);
}

void loop() {
    pollButtons();
    updateAdvScheduler();

    if (newFrameReady) {
        newFrameReady = false;
        renderHUD(rxPacket);
    } else if ((millis() - lastPacketMs) > 500UL) {
        renderHUD(rxPacket);
        delay(20);
    }

    delay(10);
}
