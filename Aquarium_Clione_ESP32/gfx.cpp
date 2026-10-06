#pragma GCC optimize("O3")
#include "gfx.h"

#if CONFIG_IDF_TARGET_ESP32C3
int fbOriginY = 0;
#endif
uint16_t* FB = nullptr;

void IRAM_ATTR fbSwapBand(int y0, int y1) {
  uint32_t* p = (uint32_t*)(FB + (size_t)fbRow(y0) * FB_W);
  size_t n = (size_t)(y1 - y0) * FB_W / 2;      // two pixels per word
  for (size_t i = 0; i < n; i++) {
    uint32_t v = p[i];
    p[i] = ((v >> 8) & 0x00FF00FFu) | ((v << 8) & 0xFF00FF00u);
  }
}
