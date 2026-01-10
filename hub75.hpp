#include "pico.h"

#include "libraries/pico_graphics/pico_graphics.hpp"

// Set RGB_MATRIX_WIDTH and RGB_MATRIX_HEIGHT to the width and height of your matrix panel!
#define RGB_MATRIX_WIDTH 64
#define RGB_MATRIX_HEIGHT 64

// Wiring of the HUB75 matrix
#ifndef DATA_BASE_PIN       // start gpio pin of consequtive color pins e.g., r1, g1, b1, r2, g2, b2
#define DATA_BASE_PIN 0
#endif
#ifndef DATA_N_PINS
#define DATA_N_PINS 6       // count of consequtive color pins usually 6 
#endif
#ifndef ROWSEL_BASE_PIN
#define ROWSEL_BASE_PIN 6   // start gpio of address pins
#endif
#ifndef ROWSEL_N_PINS 
#define ROWSEL_N_PINS 5     // count of consequtive address pins
#endif
#ifndef CLK_PIN
#define CLK_PIN 11
#endif
#ifndef STROBE_PIN
#define STROBE_PIN 12
#endif
#ifndef OEN_PIN
#define OEN_PIN 13
#endif

#define EXIT_FAILURE 1

// Scan rate 1 : 32 for a 64x64 matrix panel means 64 pixel height divided by 32 pixel results in 2 rows lit simultaneously.
// Scan rate 1 : 16 for a 64x64 matrix panel means 64 pixel height divided by 16 pixel results in 4 rows lit simultaneously.
// Scan rate 1 : 16 for a 64x32 matrix panel means 32 pixel height divided by 16 pixel results in 2 rows lit simultaneously.
// Scan rate 1 : 8 for a 64x32 matrix panel means 32 pixel height divided by 8 pixel results in 4 rows lit simultaneously.
// Scan rate 1 : 4 for a 32x16 matrix panel means 16 pixel height divided by 4 pixel results in 4 rows lit simultaneously.
// ...

// Define your panel type
#define HUB75_MULTIPLEX_2_ROWS // two rows lit simultaneously
// #define HUB75_P10_3535_16X32_4S // four rows lit simultaneously
// #define HUB75_P3_1415_16S_64X64 // four rows lit simultaneously

#if !defined(HUB75_MULTIPLEX_2_ROWS) && !defined(HUB75_P10_3535_16X32_4S) && !defined(HUB75_P3_1415_16S_64X64)
#error "You must define HUB75_MULTIPLEX_2_ROWS or HUB75_P10_3535_16X32_4S or HUB75_P3_1415_16S_64X64 to match your panels type!"
#endif

#ifndef BIT_DEPTH
#define BIT_DEPTH 10 ///< Number of bit planes
#endif

// Accumulator precision has to fit the lut precision.
#ifndef ACC_BITS
#define ACC_BITS 12
#endif

// TEMPORAL_DITHERING is experimental - development is still in progress
#undef TEMPORAL_DITHERING // set to '#define TEMPORAL_DITHERING' to use temporal dithering

enum PanelType
{
    PANEL_GENERIC = 0,
    PANEL_FM6126A,
};

using namespace pimoroni;

void create_hub75_driver(uint w, uint h, PanelType pt, bool stb_inverted);
void start_hub75_driver();
void update_bgr(const uint8_t *src);
void update(PicoGraphics const *graphics);

void setBasisBrightness(uint8_t factor);
void setIntensity(float intensity);