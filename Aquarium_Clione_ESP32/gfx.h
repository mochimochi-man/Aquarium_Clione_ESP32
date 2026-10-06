#ifndef CLIONE_GFX_H
#define CLIONE_GFX_H
// ---------------------------------------------------------------------------
// gfx.h - RGB565 framebuffer shared by the renderer and the display loop.
//
// The S3 keeps a full 320x240 framebuffer; the C3 reuses a 48-row band.
// Scene coordinates always remain in the full panel space.
// ---------------------------------------------------------------------------
#include <Arduino.h>

static const int FB_W = 320;
static const int FB_H = 240;

#if CONFIG_IDF_TARGET_ESP32C3
static constexpr int FB_ROWS = 48;
extern int fbOriginY;
#else
static constexpr int FB_ROWS = FB_H;
static constexpr int fbOriginY = 0;
#endif
// The band origin changes only after the preceding display transfer completes.
static inline int fbRow(int y) { return y - fbOriginY; }

extern uint16_t* FB;  // composite target, internal RAM

static constexpr uint16_t rgb565(int r, int g, int b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// alpha 0..32
static inline uint16_t blend565(uint16_t dst, uint16_t src, uint32_t a) {
  if (a >= 32) return src;
  if (a == 0) return dst;
  uint32_t ia = 32 - a;
  uint32_t drb = dst & 0xF81F, dg = dst & 0x07E0;
  uint32_t srb = src & 0xF81F, sg = src & 0x07E0;
  uint32_t orb = ((srb * a + drb * ia) >> 5) & 0xF81F;
  uint32_t og  = ((sg  * a + dg  * ia) >> 5) & 0x07E0;
  return (uint16_t)(orb | og);
}

// Byte-swap a band in place, ready for the panel.
//
// LovyanGFX only starts a real asynchronous DMA when the source needs no
// conversion, which means the panel's own byte order (swap565). Compositing
// stays in native order - one swap pass over the finished band is far cheaper
// than carrying swapped pixels through every blend.
void fbSwapBand(int y0, int y1);

#endif // CLIONE_GFX_H
