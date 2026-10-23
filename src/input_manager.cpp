#include "input_manager.h"
#include "system_state.h"

static uint32_t lastBtnPress = 0;

void setClusterPowerMode(ClusterMode newMode) {
    gSys.clusterMode = newMode;
}

void processPhysicalButtons() {
    if (millis() - lastBtnPress < 180) return;

    if (digitalRead(PIN_BTN_TOP) == LOW) {
        lastBtnPress = millis();
        if (!gActiveGrid.empty()) {
            gSys.selectedIndex = (gSys.selectedIndex + 1) % gActiveGrid.size();
            gActiveGrid[gSys.selectedIndex].clickScale = 0.85f;
        }
    }

    if (digitalRead(PIN_BTN_BOTTOM) == LOW) {
        lastBtnPress = millis();
        if (!gActiveGrid.empty() && gSys.selectedIndex < (int)gActiveGrid.size()) {
            gActiveGrid[gSys.selectedIndex].clickScale = 0.75f;
            if (gActiveGrid[gSys.selectedIndex].onActivate) {
                gActiveGrid[gSys.selectedIndex].onActivate();
            }
        }
    }
}

void processAdvSerialInput() {
    while (Serial.available() > 0) {
        uint8_t key = Serial.read();
        if (key == 'd' || key == KEY_RIGHT) {
            gSys.selectedIndex = (gSys.selectedIndex + 1) % gActiveGrid.size();
        } else if (key == 'a' || key == KEY_LEFT) {
            gSys.selectedIndex = (gSys.selectedIndex - 1 + gActiveGrid.size()) % gActiveGrid.size();
        } else if (key == '\n' || key == KEY_ENTER) {
            if (gActiveGrid[gSys.selectedIndex].onActivate) {
                gActiveGrid[gSys.selectedIndex].onActivate();
            }
        }
    }
}
