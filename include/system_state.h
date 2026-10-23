#pragma once

#include "config.h"

struct SystemState {
    LangID currentLang = LangID::LANG_PL;
    ClusterMode clusterMode = ClusterMode::MODE_STANDALONE_UI;
    bool connectedToADV = false;
    bool externalAntennaEn = false;
    bool screenOn = true;
    int selectedIndex = 0;
    uint8_t activeSubNodes = 0;
    char dateTimeStr[20] = "24.10.26 00:53";

    static SystemState& instance();
};

extern SystemState gSys;
extern std::vector<TileModule> gActiveGrid;
extern TileModule gMasterModules[8];
