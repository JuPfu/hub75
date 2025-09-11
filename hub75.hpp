#include "pico.h"

#include "libraries/pico_graphics/pico_graphics.hpp"

#define BIT_DEPTH 10 ///< Number of bit planes

using namespace pimoroni;

enum PanelType
{
    PANEL_GENERIC = 0,
    PANEL_FM6126A,
};

void create_hub75_driver(uint w, uint h, PanelType pt, bool stb_inverted);
void start_hub75_driver();
void update_bgr(uint8_t *src);
void update(PicoGraphics const *graphics);

void setBasis(uint8_t factor);
void setIntensity(float intensity);