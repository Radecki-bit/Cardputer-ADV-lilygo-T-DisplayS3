#include "config.h"

static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320UL;
            else crc >>= 1;
        }
    }
    return ~crc;
}

bool validateBootSignature() {
    const uint8_t lock_proj[] = {
        'S','m','o','l','i','n','s','k','i','|','L','a','b','s',' ',
        'L','a','u','n','c','h','e','r'
    };
    const uint8_t lock_auth[] = {'@','w','h','o','a','m','i'};

    uint32_t expected = crc32_update(0xFFFFFFFFUL, lock_proj, sizeof(lock_proj));
    expected = crc32_update(expected, lock_auth, sizeof(lock_auth));

    uint32_t actual = crc32_update(0xFFFFFFFFUL, (const uint8_t*)SYSTEM_PROJECT, strlen(SYSTEM_PROJECT));
    actual = crc32_update(actual, (const uint8_t*)SYSTEM_AUTHOR, strlen(SYSTEM_AUTHOR));

    return (actual == expected);
}

static void drawBootProgressBar(uint8_t percent, const char* stepName) {
    const int x = 20;
    const int y = 128;
    const int w = SCREEN_W - 40;
    const int h = 14;

    tft.fillRect(x, y - 18, w, 16, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(String("> ") + stepName, x, y - 16, 2);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(String(percent) + "%", x + w, y - 16, 2);

    tft.drawRoundRect(x, y, w, h, 4, TFT_CYAN);
    int fillW = ((w - 4) * percent) / 100;
    if (fillW > 0) {
        tft.fillRoundRect(x + 2, y + 2, fillW, h - 4, 2, TFT_GREEN);
    }

    String blocks = "[";
    int totalBlocks = 20;
    int activeBlocks = (percent * totalBlocks) / 100;
    for (int i = 0; i < totalBlocks; ++i) {
        blocks += (i < activeBlocks) ? "#" : ".";
    }
    blocks += "]";
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(blocks, SCREEN_W / 2, y + h + 4, 2);
}

void renderBootSignatureStatic() {
    if (!validateBootSignature()) {
        Serial.println("[FATAL] BOOT SIGNATURE TAMPERED - HALTING");
        delay(1000);
        esp_restart();
    }

    tft.fillScreen(TFT_BLACK);
    tft.drawRoundRect(10, 10, SCREEN_W - 20, SCREEN_H - 20, 12, TFT_CYAN);
    tft.setTextDatum(MC_DATUM);

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("Smolinski|Labs", SCREEN_W / 2, 52, 4);

    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    tft.drawString(SYSTEM_AUTHOR, SCREEN_W / 2, 86, 4);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("TACTICAL CLUSTER OS // " FIRMWARE_VERSION, SCREEN_W / 2, 122, 2);

    delay(2800);

    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("SYSTEM INITIALIZATION...", 20, 12, 2);

    const char* steps[] = {
        "Core ESP32-S3 (16M/8M PSRAM)",
        "Bluetooth Low Energy Stack",
        "WiFi 2.4GHz Radio Interface",
        "External u.FL Antenna Switch",
        "ESP-NOW Cluster Bridge",
        "Security & OTA Partitions"
    };
    const uint8_t totalSteps = 6;

    for (uint8_t idx = 0; idx < totalSteps; ++idx) {
        tft.fillRect(20, 32, SCREEN_W - 40, 72, TFT_BLACK);
        int startIdx = (idx > 2) ? (idx - 2) : 0;
        int line = 0;
        for (uint8_t i = startIdx; i <= idx; ++i) {
            tft.setTextColor((i == idx) ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
            tft.setTextDatum(TL_DATUM);
            String status = (i < idx) ? "[OK] " : "[..] ";
            tft.drawString(status + steps[i], 20, 34 + (line * 18), 2);
            line++;
        }

        uint8_t startP = (idx * 100) / totalSteps;
        uint8_t endP = ((idx + 1) * 100) / totalSteps;

        for (uint8_t p = startP; p <= endP; ++p) {
            drawBootProgressBar(p, steps[idx]);
            delay(45);
        }
        delay(250);
    }

    drawBootProgressBar(100, "SYSTEM READY");
    delay(500);
}
