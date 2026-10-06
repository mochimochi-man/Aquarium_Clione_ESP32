#pragma once
#include <stdint.h>
struct ClioneFrame { uint16_t width, height; const uint16_t* colors; const uint8_t* alpha; };
extern const ClioneFrame CLIONE_SWIM[8];
