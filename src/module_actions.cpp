#include "module_actions.h"

void actionBluetooth() {
    Serial.println("[BT] BLE scanner active");
}

void actionWiFi() {
    Serial.println("[WiFi] Channel analyzer active");
}

void actionGemini() {
    Serial.println("[Gemini] Streaming request ready");
}

void actionMesh() {
    Serial.println("[Mesh] Map active");
}

void actionMicroPython() {
    Serial.println("[MPY] REPL ready");
}

void actionFlashManager() {
    Serial.println("[Flash] Partition/FS inspector active");
}

void actionOTA() {
    Serial.println("[OTA] Firmware package manager active");
}

void actionSettings() {
    Serial.println("[Settings] runtime configuration active");
}
