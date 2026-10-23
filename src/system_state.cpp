#include "system_state.h"

SystemState gSys;

std::vector<TileModule> gActiveGrid;

const I18nStrings kI18n[4] = {
    {"Bluetooth", "WiFi", "Gemini AI", "Meshtastic", "MicroPython", "Flash Mgr", "OTA", "Settings", "HEADLESS COMPUTE TO ADV", "CLUSTER ACTIVE"},
    {"Bluetooth", "WLAN", "Gemini KI", "Meshtastic", "MicroPython", "Flash-Mgr", "OTA", "Optionen", "RECHENLEISTUNG AN ADV", "CLUSTER AKTIV"},
    {"Блютуз", "WiFi", "Gemini ИИ", "Мештастик", "MicroPython", "Флеш-Мгр", "OTA", "Настройки", "МОЩНОСТЬ НА ADV", "КЛАСТЕР АКТИВЕН"},
    {"Bluetooth", "WiFi", "Gemini AI", "Meshtastic", "MicroPython", "Pamięć", "OTA", "Ustawienia", "MOC OBLICZENIOWA -> ADV", "KLASTER AKTYWNY"}
};

TileModule gMasterModules[8] = {
    {ModuleID::MOD_BT,       0x02BF, true, 1.0f, nullptr},
    {ModuleID::MOD_WIFI,     0x073F, true, 1.0f, nullptr},
    {ModuleID::MOD_GEMINI,   0xFFFF, true, 1.0f, nullptr},
    {ModuleID::MOD_MESH,     0x6752, true, 1.0f, nullptr},
    {ModuleID::MOD_MPY,      0xFDA0, true, 1.0f, nullptr},
    {ModuleID::MOD_FLASH,    0xFFE0, true, 1.0f, nullptr},
    {ModuleID::MOD_OTA,      0x07E8, true, 1.0f, nullptr},
    {ModuleID::MOD_SETTINGS, 0xAD55, true, 1.0f, nullptr}
};

SystemState& SystemState::instance() {
    static SystemState s;
    return s;
}
