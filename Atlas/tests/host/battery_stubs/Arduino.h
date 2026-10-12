#pragma once
#include "../stubs/Arduino.h"
constexpr int INPUT = 0, ADC_11db = 3;
extern uint32_t fixtureAdcPinMv;
inline uint32_t analogReadMilliVolts(int) { return fixtureAdcPinMv; }
inline void analogSetPinAttenuation(int,int) {}
