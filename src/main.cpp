#include "config.h"
#include "system_state.h"
#include "display.h"
#include "input_manager.h"
#include "module_registry.h"
#include "module_actions.h"

TaskHandle_t guiTaskHandle = nullptr;
TaskHandle_t clusterTaskHandle = nullptr;

void guiRenderTask(void* pvParameters) {
    uint32_t frameTick = 0;

    while (true) {
        if (gSys.clusterMode != ClusterMode::MODE_COMPUTE_OFFLOAD) {
            gDisplay.renderFrame(frameTick++);
        }
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

void clusterComputeTask(void* pvParameters) {
    while (true) {
        processAdvSerialInput();
        processPhysicalButtons();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

void setup() {
    pinMode(PIN_BTN_TOP, INPUT_PULLUP);
    pinMode(PIN_BTN_BOTTOM, INPUT_PULLUP);
    pinMode(PIN_LCD_BL, OUTPUT);
    pinMode(PIN_ANTENNA_SW, OUTPUT);

    digitalWrite(PIN_LCD_BL, HIGH);
    digitalWrite(PIN_ANTENNA_SW, LOW);

    Serial.begin(115200);

    gDisplay.begin();
    gDisplay.renderBootSplash();

    rebuildActiveGrid();

    snprintf(gSys.dateTimeStr, sizeof(gSys.dateTimeStr), "24.10.26 00:53");

    actionBluetooth();
    actionWiFi();

    xTaskCreatePinnedToCore(clusterComputeTask, "ClusterCore0", 8192, nullptr, 3, &clusterTaskHandle, 0);
    xTaskCreatePinnedToCore(guiRenderTask, "GuiCore1", 8192, nullptr, 2, &guiTaskHandle, 1);
}

void loop() {
    vTaskDelete(nullptr);
}
