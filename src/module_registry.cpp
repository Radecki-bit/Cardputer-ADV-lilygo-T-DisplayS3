#include "module_registry.h"
#include "system_state.h"
#include "module_actions.h"

void rebuildActiveGrid() {
    gMasterModules[0].onActivate = actionBluetooth;
    gMasterModules[1].onActivate = actionWiFi;
    gMasterModules[2].onActivate = actionGemini;
    gMasterModules[3].onActivate = actionMesh;
    gMasterModules[4].onActivate = actionMicroPython;
    gMasterModules[5].onActivate = actionFlashManager;
    gMasterModules[6].onActivate = actionOTA;
    gMasterModules[7].onActivate = actionSettings;

    gActiveGrid.clear();
    for (int i = 0; i < 8; ++i) {
        if (gMasterModules[i].installed) {
            gActiveGrid.push_back(gMasterModules[i]);
        }
    }
}

void toggleOptionalModule(ModuleID target, bool state) {
    for (int i = 0; i < 8; ++i) {
        if (gMasterModules[i].id == target) {
            gMasterModules[i].installed = state;
            break;
        }
    }
    rebuildActiveGrid();
}

bool isModuleLoaded(ModuleID id) {
    for (const auto& m : gActiveGrid) {
        if (m.id == id) return true;
    }
    return false;
}

const char* moduleName(ModuleID id) {
    const auto& lang = kI18n[static_cast<uint8_t>(gSys.currentLang)];
    switch (id) {
        case ModuleID::MOD_BT:       return lang.bt;
        case ModuleID::MOD_WIFI:     return lang.wifi;
        case ModuleID::MOD_GEMINI:   return lang.gemini;
        case ModuleID::MOD_MESH:     return lang.mesh;
        case ModuleID::MOD_MPY:      return lang.mpy;
        case ModuleID::MOD_FLASH:    return lang.flash;
        case ModuleID::MOD_OTA:      return lang.ota;
        case ModuleID::MOD_SETTINGS: return lang.settings;
        default:                     return "UNKNOWN";
    }
}
