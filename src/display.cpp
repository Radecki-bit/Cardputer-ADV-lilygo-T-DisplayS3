#include "display.h"
#include "system_state.h"
#include "module_registry.h"

LGFX tft;

DisplayManager gDisplay;

DisplayManager::DisplayManager() {}

void DisplayManager::begin() {
    tft.begin();
    tft.setRotation(1);
    tft.setBrightness(255);
    tft.setColorDepth(16);
    tft.setSwapBytes(true);
    tft.initDMA();
    tft.fillScreen(TFT_BLACK);
}

void DisplayManager::renderBootSplash() {
    renderBootSignatureStatic();
}

void DisplayManager::drawBackground(uint32_t frameTick) {
    const uint16_t colorA = 0x0A00;
    const uint16_t colorB = 0x0011;
    const uint16_t colorC = 0x2003;

    for (int y = 0; y < SCREEN_H; ++y) {
        uint16_t c = (y % 2 == 0) ? colorA : colorB;
        if (((y + frameTick) % 22) < 2) c = colorC;
        tft.drawFastHLine(0, y, SCREEN_W, c);
    }

    for (int x = 0; x < SCREEN_W; x += 16) {
        int phase = (x + frameTick * 2) % 160;
        tft.drawFastVLine(x + phase, 0, SCREEN_H, 0x0019);
    }
}

void DisplayManager::drawStatusBar() {
    tft.fillRect(0, 0, SCREEN_W, TOPBAR_H, TFT_DARKGREY);

    String mode = "STANDALONE";
    if (gSys.clusterMode == ClusterMode::MODE_DOCKED_CLUSTER) mode = "ADV CLUSTER";
    else if (gSys.clusterMode == ClusterMode::MODE_COMPUTE_OFFLOAD) mode = "OFFLOAD";

    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(mode, 8, 5, 2);

    tft.drawRect(110, 5, 10, 10, TFT_GREEN);
    if (gSys.externalAntennaEn) tft.fillRect(112, 7, 6, 6, TFT_GREEN);
    else tft.fillRect(112, 7, 6, 6, TFT_RED);

    tft.setTextDatum(TR_DATUM);
    tft.drawString(gSys.dateTimeStr, SCREEN_W - 8, 5, 2);
}

void DisplayManager::drawGrid(uint32_t frameTick) {
    for (size_t i = 0; i < gActiveGrid.size(); ++i) {
        int col = i % GRID_COLS;
        int row = i / GRID_COLS;
        int x = GRID_START_X + col * (TILE_W + TILE_GAP);
        int y = GRID_START_Y + row * (TILE_H + TILE_GAP);

        float scale = gActiveGrid[i].clickScale;
        bool selected = (int)i == gSys.selectedIndex;
        drawTileBackground(x, y, TILE_W, TILE_H, gActiveGrid[i].accentColor565, selected, scale);

        if (gActiveGrid[i].clickScale < 1.0f) {
            gActiveGrid[i].clickScale += 0.04f;
            if (gActiveGrid[i].clickScale > 1.0f) gActiveGrid[i].clickScale = 1.0f;
        }

        drawIcon(x + 9, y + 9, TILE_W - 18, TILE_H - 18, gActiveGrid[i].id, selected);
    }
}

void DisplayManager::renderFrame(uint32_t frameTick) {
    tft.fillScreen(TFT_BLACK);
    drawBackground(frameTick);
    drawStatusBar();
    drawGrid(frameTick);
}

void DisplayManager::drawTileBackground(int x, int y, int w, int h, uint16_t accent, bool selected, float scale) {
    int cx = x + w / 2;
    int cy = y + h / 2;

    int sx = (int)((float)w * scale);
    int sy = (int)((float)h * scale);

    int rx = cx - sx / 2;
    int ry = cy - sy / 2;

    if (selected) {
        for (int r = 1; r <= 5; ++r) {
            tft.drawRoundRect(rx - r, ry - r, sx + r * 2, sy + r * 2, 8, accent);
        }
    }

    tft.fillRoundRect(x, y, w, h, 8, TFT_BLACK);
    tft.drawRoundRect(x, y, w, h, 8, accent);
    tft.fillRoundRect(x + 2, y + 2, w - 4, h - 4, 6, 0x1111);
}

void DisplayManager::drawIcon(int x, int y, int w, int h, ModuleID id, bool selected) {
    switch (id) {
        case ModuleID::MOD_BT: drawBluetoothIcon(x, y, w, h, TFT_BLUE); break;
        case ModuleID::MOD_WIFI: drawWiFiIcon(x, y, w, h, TFT_CYAN); break;
        case ModuleID::MOD_GEMINI: drawGeminiIcon(x, y, w, h, TFT_WHITE); break;
        case ModuleID::MOD_MESH: drawMeshIcon(x, y, w, h, TFT_GREEN); break;
        case ModuleID::MOD_MPY: drawMicroPythonIcon(x, y, w, h, TFT_WHITE); break;
        case ModuleID::MOD_FLASH: drawFolderIcon(x, y, w, h, TFT_YELLOW); break;
        case ModuleID::MOD_OTA: drawOTAIcon(x, y, w, h, TFT_GREEN); break;
        case ModuleID::MOD_SETTINGS: drawGearIcon(x, y, w, h, TFT_ORANGE); break;
        default: break;
    }
    if (selected) {
        tft.drawRoundRect(x - 2, y - 2, w + 4, h + 4, 8, TFT_CYAN);
    }
}

void DisplayManager::drawBluetoothIcon(int x, int y, int w, int h, uint16_t color) {
    int cx = x + w / 2;
    int cy = y + h / 2;
    tft.drawLine(cx - 10, cy, cx + 10, cy, color);
    tft.drawLine(cx, cy - 10, cx, cy + 10, color);
    tft.drawCircle(cx, cy, 18, color);
    for (int i = 0; i < 3; ++i) {
        tft.drawArc(cx, cy, 26 + i * 8, 12 + i * 8, 0, 180, color);
    }
}

void DisplayManager::drawWiFiIcon(int x, int y, int w, int h, uint16_t color) {
    int cx = x + w / 2;
    int cy = y + h / 2;
    tft.drawArc(cx, cy, 26, 10, 180, 360, color);
    tft.drawArc(cx, cy, 18, 2, 180, 360, color);
    tft.drawArc(cx, cy, 10, 2, 180, 360, color);
    tft.drawLine(cx - 4, cy + 10, cx + 4, cy + 18, color);
    tft.fillCircle(cx, cy + 18, 3, color);
}

void DisplayManager::drawGeminiIcon(int x, int y, int w, int h, uint16_t color) {
    int cx = x + w / 2;
    int cy = y + h / 2;
    tft.drawRoundRect(cx - 18, cy - 18, 36, 36, 8, color);
    tft.drawLine(cx - 10, cy - 10, cx + 10, cy + 10, color);
    tft.drawLine(cx + 10, cy - 10, cx - 10, cy + 10, color);
    tft.drawFastHLine(cx - 12, cy, 24, color);
}

void DisplayManager::drawMeshIcon(int x, int y, int w, int h, uint16_t color) {
    int cx = x + w / 2;
    int cy = y + h / 2;
    tft.drawString("M", cx, cy - 12, 4);
    tft.drawRoundRect(cx - 16, cy - 12, 32, 26, 4, color);
    tft.drawFastHLine(cx - 10, cy + 18, 20, color);
}

void DisplayManager::drawMicroPythonIcon(int x, int y, int w, int h, uint16_t color) {
    int cx = x + w / 2;
    int cy = y + h / 2;
    tft.drawRoundRect(cx - 18, cy - 18, 36, 36, 8, color);
    tft.drawString("PY", cx, cy - 10, 2);
    tft.fillCircle(cx - 8, cy + 8, 3, color);
    tft.fillCircle(cx + 8, cy + 8, 3, color);
}

void DisplayManager::drawFolderIcon(int x, int y, int w, int h, uint16_t color) {
    tft.drawRoundRect(x + 4, y + 6, w - 10, h - 12, 6, color);
    tft.fillRoundRect(x + 6, y + 8, 18, 10, 4, color);
    tft.drawFastVLine(x + 14, y + 10, 6, TFT_BLACK);
    tft.drawFastHLine(x + 12, y + 20, w - 20, color);
}

void DisplayManager::drawOTAIcon(int x, int y, int w, int h, uint16_t color) {
    tft.fillRoundRect(x + 2, y + 2, w - 4, h - 4, 2, TFT_BLACK);
    tft.fillRoundRect(x + 6, y + 6, w - 12, h - 12, 2, TFT_BLACK);
    tft.drawString("OTA", x + w / 2, y + h / 2 - 8, 2);
    tft.drawFastHLine(x + 10, y + h / 2 + 12, w - 20, color);
}

void DisplayManager::drawGearIcon(int x, int y, int w, int h, uint16_t color) {
    int cx = x + w / 2;
    int cy = y + h / 2;
    tft.drawCircle(cx, cy, 12, color);
    for (int i = 0; i < 8; ++i) {
        float ang = (i / 8.0f) * PI * 2.0f;
        int sx = cx + (int)(cos(ang) * 18.0f);
        int sy = cy + (int)(sin(ang) * 18.0f);
        int ex = cx + (int)(cos(ang) * 28.0f);
        int ey = cy + (int)(sin(ang) * 28.0f);
        tft.drawLine(sx, sy, ex, ey, color);
    }
    tft.fillCircle(cx, cy, 5, color);
}
