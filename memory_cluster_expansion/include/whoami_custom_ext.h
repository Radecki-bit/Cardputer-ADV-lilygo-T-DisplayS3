#pragma once
#include <Arduino.h>

// Hooki pod szybkie dokompilowywanie nowych funkcji (Polecenie #2)
inline void whoamiCustomSetup() {}
inline void whoamiCustomLoop(uint8_t nodeRole, uint8_t clusterId, uint32_t& extraMetric) {
    extraMetric = 0;
}
