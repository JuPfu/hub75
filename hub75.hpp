#include "pico.h"

#include "libraries/pico_graphics/pico_graphics.hpp"

using namespace pimoroni;

void create_hub75_driver(uint width, uint height);
void start_hub75_driver();
void update_bgr(uint8_t *src);
void update(PicoGraphics const *graphics);