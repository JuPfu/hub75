#include "pico.h"

#ifndef USE_PICO_GRAPHICS
#define USE_PICO_GRAPHICS true
#endif

#if USE_PICO_GRAPHICS == true
#include "libraries/pico_graphics/pico_graphics.hpp"
#endif

// See README.md file chapter "How to Configure" for some hints how to adapt the configuration to your panel

// Set MATRIX_PANEL_WIDTH and MATRIX_PANEL_HEIGHT to the width and height of your matrix panel!
#ifndef MATRIX_PANEL_WIDTH
#define MATRIX_PANEL_WIDTH 64
#endif
#ifndef MATRIX_PANEL_HEIGHT
#define MATRIX_PANEL_HEIGHT 64
#endif

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
#define ROWSEL_N_PINS 4 // count of consecutive address pins - adapt to the number of address pins of your panel
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
// #define HUB75_MULTIPLEX_2_ROWS      // default - two rows lit simultaneously
// #define HUB75_P10_3535_16X32_4S     // four rows lit simultaneously (can be defined via CMake)
// #define HUB75_P3_1415_16S_64X64_S31 // four rows lit simultaneously
//
// Default to HUB75_MULTIPLEX_2_ROWS if no multiplexing mode is defined
// Only define default if none of the mapping modes are already defined
#if !defined(HUB75_MULTIPLEX_2_ROWS) && !defined(HUB75_P10_3535_16X32_4S) && !defined(HUB75_P3_1415_16S_64X64_S31)
#define HUB75_MULTIPLEX_2_ROWS // two rows lit simultaneously
#endif

// If panel type FM6126A or panel type RUL6024 is selected, an initialisation sequence is sent to the panel
#define PANEL_GENERIC 0
#define PANEL_FM6126A 1
#define PANEL_RUL6024 2

// set your panel type
// e.g. P3-64*64-32S-V2.0 might have a RUL6024 chip, if so, set PANEL_TYPE to PANEL_RUL6024
#ifndef PANEL_TYPE
#define PANEL_TYPE PANEL_GENERIC
#endif

#ifndef INVERTED_STB
#define INVERTED_STB false
#endif

#ifndef SM_CLOCKDIV_FACTOR
// To prevent flicker or ghosting it might be worth a try to reduce state machine speed.
// For panels with height less or equal to 16 rows try a factor of 8.0f
// For panels with height less or equal to 32 rows try a factor of 2.0f or 4.0f
// Even for panels with height less or equal to 62 rows a factor of about 2.0f might solve such an issue
#define SM_CLOCKDIV_FACTOR 1.0f
#endif

#ifndef SEPARATE_CIE_CHANNELS
#define SEPARATE_CIE_CHANNELS false
#endif

#if SEPARATE_CIE_CHANNELS == false
#define CIE_RED CIE
#define CIE_GREEN CIE
#define CIE_BLUE CIE
#endif

// Balanced Light Output
// High-weight bit-planes are split into multiple smaller slices within the BCM sequence.
// This increases the effective refresh rate and cuts down flicker at the cost of some more memory consumption.
#ifndef BALANCED_LIGHT_OUTPUT
BALANCED_LIGHT_OUTPUT true
#endif

// Used in hub75_demo.cpp
// Start hub75 driver on core1 if HUB75_MULTICORE is set to true
// Start hub75 driver on core0 if HUB75_MULTICORE is set to false
// The hub75 driver has not much CPU load. Most of it task are handled by DMA and PIO.
// Only the interupt handler oen_finished_handler is CPU bound.
#ifndef HUB75_MULTICORE
#define HUB75_MULTICORE true
#endif

// --- modifications below this line might imply changes in source code ---

constexpr int PIXELS = MATRIX_PANEL_WIDTH * MATRIX_PANEL_HEIGHT;

#define LUT_MAPPING(COLOUR) pack_lut_rgb(COLOUR)
#define LUT_MAPPING_RGB(R, G, B) pack_lut_rgb_(R, G, B)

// At the moment only used for HUB75_P10_3535_16X32_4S panels
#define SCAN_GROUPS (1 << ROWSEL_N_PINS)

#if !defined(BITPLANES)
#define BITPLANES 10 // default
#endif

#if BITPLANES != 8 && BITPLANES != 10
#error "BITPLANES must be 8 or 10"
#endif

#define EXIT_FAILURE 1

// Frame rate
// Use only for testing or debugging
#ifndef FRAME_RATE
#define FRAME_RATE false
#endif

#if USE_PICO_GRAPHICS == true
using namespace pimoroni;
#endif

namespace PanelConfig
{
    constexpr uint32_t WIDTH = MATRIX_PANEL_WIDTH;
    constexpr uint32_t HEIGHT = MATRIX_PANEL_HEIGHT;

    // The number of address lines (A, B, C...) defines the multiplexing depth
    constexpr uint32_t ADDR_PINS = ROWSEL_N_PINS;
    constexpr uint32_t ADDR_MASK =  (1 << ADDR_PINS) - 1;

    // How many unique binary addresses are sent to the panel
    // This is the value your DMA loop for 'row_cmd' should iterate over.
    constexpr uint32_t SCAN_DEPTH = (1u << ADDR_PINS); // 16 for 1/16 scan

    // How many physical rows are updated per clock pulse (parallelism)
    // For standard panels, this is usually 2.
    constexpr uint32_t ROWS_IN_PARALLEL = HEIGHT / SCAN_DEPTH;

    // SCAN_MODE_WIDTH: Line width depending on ROWS_IN_PARALLEL (scan-mode)
    // Hub75 standard panel with scan-mode 2 -> (MATRIX_PANEL_WIDTH >> 1) * 2
    // Matrix panel with scan-mode 4 -> (MATRIX_PANEL_WIDTH >> 1) * 4
    // Used in hub75_bitplane_stream as value of Y-register
    // Each OUT instruction writes color information for 2 pixels: r0g0b0 and r1b1g1
    constexpr uint32_t SCAN_MODE_WIDTH = (WIDTH >> 1) * ROWS_IN_PARALLEL;
}

void create_hub75_driver(uint w, uint h, uint pt, bool stb_inverted);
void start_hub75_driver();
void update_bgr(const uint8_t *src);
#if USE_PICO_GRAPHICS == true
void update(PicoGraphics const *graphics);
#endif

void setBasisBrightness(uint8_t factor);
void setIntensity(float intensity);
void setIntensity(float intensity, bool linear_brightness_control);
