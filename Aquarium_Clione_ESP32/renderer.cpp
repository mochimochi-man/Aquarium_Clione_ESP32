#include "renderer.h"
#include "gfx.h"
#include <string.h>
#include "clione_render.h"
void renderBand(const clione::World& world, int y0, int y1) {
  memset(FB+(size_t)fbRow(y0)*FB_W,0,(size_t)(y1-y0)*FB_W*sizeof(*FB));
  clioneDrawBand(world,y0,y1);
}
