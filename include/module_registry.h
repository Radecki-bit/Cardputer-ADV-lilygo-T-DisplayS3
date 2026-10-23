#pragma once

#include "config.h"

void rebuildActiveGrid();
void toggleOptionalModule(ModuleID target, bool state);
bool isModuleLoaded(ModuleID id);
const char* moduleName(ModuleID id);
