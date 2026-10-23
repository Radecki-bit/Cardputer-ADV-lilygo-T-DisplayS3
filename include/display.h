#pragma once

#include "config.h"

class DisplayManager {
public:
    DisplayManager();
    void begin();
    void renderBootSplash();
    void renderFrame(uint32_t frameTick);
    void drawBackground(uint32_t frameTick);
    void drawStatusBar();
    void drawGrid(uint32_t frameTick);

private:
    void drawTileBackground(int x, int y, int w, int h, uint16_t accent, bool selected, float scale);
    void drawIcon(int x, int y, int w, int h, ModuleID id, bool selected);
    void drawBluetoothIcon(int x, int y, int w, int h, uint16_t color);
    void drawWiFiIcon(int x, int y, int w, int h, uint16_t color);
    void drawGeminiIcon(int x, int y, int w, int h, uint16_t color);
    void drawMeshIcon(int x, int y, int w, int h, uint16_t color);
    void drawMicroPythonIcon(int x, int y, int w, int h, uint16_t color);
    void drawFolderIcon(int x, int y, int w, int h, uint16_t color);
    void drawOTAIcon(int x, int y, int w, int h, uint16_t color);
    void drawGearIcon(int x, int y, int w, int h, uint16_t color);
};

extern DisplayManager gDisplay;
