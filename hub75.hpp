#include "pico.h"

#include "libraries/pico_graphics/pico_graphics.hpp"

// See README.md file chapter "How to Configure" for some hints how to adapt the configuration to your panel

// Set MATRIX_PANEL_WIDTH and MATRIX_PANEL_HEIGHT to the width and height of your matrix panel!
#define MATRIX_PANEL_WIDTH 64
#define MATRIX_PANEL_HEIGHT 64

// Wiring of the HUB75 matrix
#ifndef DATA_BASE_PIN // start gpio pin of consecutive color pins e.g., r1, g1, b1, r2, g2, b2
#define DATA_BASE_PIN 0
#endif
#ifndef DATA_N_PINS
#define DATA_N_PINS 6 // count of consecutive color pins usually 6
#endif
#ifndef ROWSEL_BASE_PIN
#define ROWSEL_BASE_PIN 6 // start gpio pin of address pins
#endif
#ifndef ROWSEL_N_PINS
#define ROWSEL_N_PINS 5 // count of consecutive address pins - adapt to the number of address pins of your panel
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

// Scan rate 1 : 32 for a 64x64 matrix panel means 64 pixel height divided by 32 pixel results in 2 rows lit simultaneously.
// Scan rate 1 : 16 for a 64x64 matrix panel means 64 pixel height divided by 16 pixel results in 4 rows lit simultaneously.
// Scan rate 1 : 16 for a 64x32 matrix panel means 32 pixel height divided by 16 pixel results in 2 rows lit simultaneously.
// Scan rate 1 : 8 for a 64x32 matrix panel means 32 pixel height divided by 8 pixel results in 4 rows lit simultaneously.
// Scan rate 1 : 4 for a 32x16 matrix panel means 16 pixel height divided by 4 pixel results in 4 rows lit simultaneously.
// ...

// Set your panel
//
// Example:
// The P3-64*64-32S-V2.0 is a standard Hub75 panel with two rows multiplexed, so define HUB75_MULTIPLEX_2_ROWS should be correct
//
#define HUB75_MULTIPLEX_2_ROWS // two rows lit simultaneously
// #define HUB75_P10_3535_16X32_4S // four rows lit simultaneously
// #define HUB75_P3_1415_16S_64X64_S31 // four rows lit simultaneously

#if !defined(HUB75_MULTIPLEX_2_ROWS) && !defined(HUB75_P10_3535_16X32_4S) && !defined(HUB75_P3_1415_16S_64X64_S31)
#error "You must define HUB75_MULTIPLEX_2_ROWS or HUB75_P10_3535_16X32_4S or HUB75_P3_1415_16S_64X64_S31 to match your panels type!"
#endif

// If panel type FM6126A or panel type RUL6024 is selected, an initialisation sequence is sent to the panel
#define PANEL_GENERIC 0
#define PANEL_FM6126A 1
#define PANEL_RUL6024 2

// set your panel type
// e.g. P3-64*64-32S-V2.0 might have a RUL6024 chip, if so, set PANEL_TYPE to PANEL_RUL6024
#define PANEL_TYPE PANEL_GENERIC

#define INVERTED_STB false

// TEMPORAL_DITHERING is experimental - development is still in progress
#define TEMPORAL_DITHERING // set to '#define TEMPORAL_DITHERING' to use temporal dithering

// --- modifications below this line might imply changes in source code ---

#ifdef TEMPORAL_DITHERING
#define LUT_MAPPING(IDX, COLOUR) temporal_dithering(IDX, COLOUR)
#define LUT_MAPPING_RGB(IDX, R, G, B) temporal_dithering(IDX, R, G, B)
#else
#define LUT_MAPPING(IDX, COLOUR) pack_lut_rgb(COLOUR, lut)
#define LUT_MAPPING_RGB(IDX, R, G, B) pack_lut_rgb_(R, G, B, lut)
#endif

// At the moment only used for HUB75_P10_3535_16X32_4S panels
#define SCAN_GROUPS (1 << ROWSEL_N_PINS)

#ifndef BIT_DEPTH
#define BIT_DEPTH 10 ///< Number of bit planes
#endif

// Accumulator precision has to fit the lut precision.
#ifndef ACC_BITS
#define ACC_BITS 12
#endif

#define EXIT_FAILURE 1

using namespace pimoroni;

void create_hub75_driver(uint w, uint h, uint pt, bool stb_inverted);
void start_hub75_driver();
void update_bgr(const uint8_t *src);
void update(PicoGraphics const *graphics);

void setBasisBrightness(uint8_t factor);
void setIntensity(float intensity);