#pragma once
#include "clione_model.h"
void clioneDrawBand(const clione::World& world, int y0, int y1);

// Call once after simulation update, before rendering any bands.
void clionePrepareFrame();
