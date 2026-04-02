#include <cstdlib>
#include <vector>
#include <memory>

#include "pico/stdlib.h"

#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/interp.h"
#include "pico/sync.h"

#include "hub75.hpp"
#include "hub75.pio.h"

#include "rul6024.h"
#include "fm6126a.h"
#include <cstring>

// Deduced from https://jared.geek.nz/2013/02/linear-led-pwm/
// The CIE 1931 lightness formula is what actually describes how we perceive light.

#if BITPLANES == 10
static const uint16_t CIE_RED[256] = {
    0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14,
    15, 16, 16, 17, 18, 18, 19, 20, 21, 21, 22, 23, 24, 25, 26, 26,
    27, 28, 29, 30, 31, 32, 33, 34, 35, 37, 38, 39, 40, 41, 42, 44,
    45, 46, 48, 49, 50, 52, 53, 55, 56, 58, 59, 61, 62, 64, 65, 67,
    69, 71, 72, 74, 76, 78, 80, 82, 83, 85, 87, 89, 91, 94, 96, 98,
    100, 102, 104, 107, 109, 111, 114, 116, 119, 121, 124, 126, 129, 131, 134, 137,
    139, 142, 145, 148, 151, 153, 156, 159, 162, 165, 169, 172, 175, 178, 181, 185,
    188, 191, 195, 198, 201, 205, 209, 212, 216, 219, 223, 227, 231, 235, 239, 242,
    246, 250, 255, 259, 263, 267, 271, 276, 280, 284, 289, 293, 298, 302, 307, 311,
    316, 321, 326, 331, 335, 340, 345, 350, 355, 361, 366, 371, 376, 382, 387, 392,
    398, 403, 409, 415, 420, 426, 432, 438, 444, 450, 456, 462, 468, 474, 480, 486,
    493, 499, 506, 512, 519, 525, 532, 538, 545, 552, 559, 566, 573, 580, 587, 594,
    601, 609, 616, 623, 631, 638, 646, 654, 661, 669, 677, 685, 693, 701, 709, 717,
    725, 733, 742, 750, 758, 767, 776, 784, 793, 802, 810, 819, 828, 837, 846, 855,
    865, 874, 883, 893, 902, 912, 921, 931, 941, 950, 960, 970, 980, 990, 1001, 1011};

static const uint16_t CIE_GREEN[256] = {
    0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7,
    7, 8, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 15,
    15, 16, 17, 17, 18, 19, 19, 20, 21, 22, 22, 23, 24, 25, 26, 27,
    28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 42, 43, 44,
    45, 47, 48, 50, 51, 52, 54, 55, 57, 58, 60, 61, 63, 65, 66, 68,
    70, 71, 73, 75, 77, 79, 81, 83, 84, 86, 88, 90, 93, 95, 97, 99,
    101, 103, 106, 108, 110, 113, 115, 118, 120, 123, 125, 128, 130, 133, 136, 138,
    141, 144, 147, 149, 152, 155, 158, 161, 164, 167, 171, 174, 177, 180, 183, 187,
    190, 194, 197, 200, 204, 208, 211, 215, 218, 222, 226, 230, 234, 237, 241, 245,
    249, 254, 258, 262, 266, 270, 275, 279, 283, 288, 292, 297, 301, 306, 311, 315,
    320, 325, 330, 335, 340, 345, 350, 355, 360, 365, 370, 376, 381, 386, 392, 397,
    403, 408, 414, 420, 425, 431, 437, 443, 449, 455, 461, 467, 473, 480, 486, 492,
    499, 505, 512, 518, 525, 532, 538, 545, 552, 559, 566, 573, 580, 587, 594, 601,
    609, 616, 624, 631, 639, 646, 654, 662, 669, 677, 685, 693, 701, 709, 717, 726,
    734, 742, 751, 759, 768, 776, 785, 794, 802, 811, 820, 829, 838, 847, 857, 866,
    875, 885, 894, 903, 913, 923, 932, 942, 952, 962, 972, 982, 992, 1002, 1013, 1023};

static const uint16_t CIE_BLUE[256] = {
    0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7,
    7, 8, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 15,
    15, 16, 17, 17, 18, 19, 19, 20, 21, 22, 22, 23, 24, 25, 26, 27,
    28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 42, 43, 44,
    45, 47, 48, 50, 51, 52, 54, 55, 57, 58, 60, 61, 63, 65, 66, 68,
    70, 71, 73, 75, 77, 79, 81, 83, 84, 86, 88, 90, 93, 95, 97, 99,
    101, 103, 106, 108, 110, 113, 115, 118, 120, 123, 125, 128, 130, 133, 136, 138,
    141, 144, 147, 149, 152, 155, 158, 161, 164, 167, 171, 174, 177, 180, 183, 187,
    190, 194, 197, 200, 204, 208, 211, 215, 218, 222, 226, 230, 234, 237, 241, 245,
    249, 254, 258, 262, 266, 270, 275, 279, 283, 288, 292, 297, 301, 306, 311, 315,
    320, 325, 330, 335, 340, 345, 350, 355, 360, 365, 370, 376, 381, 386, 392, 397,
    403, 408, 414, 420, 425, 431, 437, 443, 449, 455, 461, 467, 473, 480, 486, 492,
    499, 505, 512, 518, 525, 532, 538, 545, 552, 559, 566, 573, 580, 587, 594, 601,
    609, 616, 624, 631, 639, 646, 654, 662, 669, 677, 685, 693, 701, 709, 717, 726,
    734, 742, 751, 759, 768, 776, 785, 794, 802, 811, 820, 829, 838, 847, 857, 866,
    875, 885, 894, 903, 913, 923, 932, 942, 952, 962, 972, 982, 992, 1002, 1013, 1023};

#elif BITPLANES == 8
static const uint16_t CIE_RED[256] = {
    0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4,
    4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 7,
    7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 11, 11,
    11, 12, 12, 12, 13, 13, 13, 14, 14, 14, 15, 15, 16, 16, 16, 17,
    17, 18, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24, 24,
    25, 25, 26, 27, 27, 28, 28, 29, 30, 30, 31, 31, 32, 33, 33, 34,
    35, 35, 36, 37, 38, 38, 39, 40, 40, 41, 42, 43, 44, 44, 45, 46,
    47, 48, 49, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 58, 59, 60,
    61, 62, 63, 64, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 78,
    79, 80, 81, 82, 84, 85, 86, 87, 89, 90, 91, 92, 94, 95, 96, 98,
    99, 101, 102, 103, 105, 106, 108, 109, 111, 112, 114, 115, 117, 118, 120, 121,
    123, 124, 126, 128, 129, 131, 133, 134, 136, 138, 139, 141, 143, 145, 146, 148,
    150, 152, 154, 155, 157, 159, 161, 163, 165, 167, 169, 171, 173, 175, 177, 179,
    181, 183, 185, 187, 189, 191, 193, 195, 198, 200, 202, 204, 206, 209, 211, 213,
    216, 218, 220, 223, 225, 227, 230, 232, 234, 237, 239, 242, 244, 247, 249, 252};

static const uint16_t CIE_GREEN[256] = {
    0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4,
    4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 7,
    7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 10, 10, 10, 10, 11, 11,
    11, 12, 12, 12, 13, 13, 13, 14, 14, 15, 15, 15, 16, 16, 17, 17,
    17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24, 24, 25,
    25, 26, 26, 27, 28, 28, 29, 29, 30, 31, 31, 32, 32, 33, 34, 34,
    35, 36, 37, 37, 38, 39, 39, 40, 41, 42, 43, 43, 44, 45, 46, 47,
    47, 48, 49, 50, 51, 52, 53, 54, 54, 55, 56, 57, 58, 59, 60, 61,
    62, 63, 64, 65, 66, 67, 68, 70, 71, 72, 73, 74, 75, 76, 77, 79,
    80, 81, 82, 83, 85, 86, 87, 88, 90, 91, 92, 94, 95, 96, 98, 99,
    100, 102, 103, 105, 106, 108, 109, 110, 112, 113, 115, 116, 118, 120, 121, 123,
    124, 126, 128, 129, 131, 132, 134, 136, 138, 139, 141, 143, 145, 146, 148, 150,
    152, 154, 155, 157, 159, 161, 163, 165, 167, 169, 171, 173, 175, 177, 179, 181,
    183, 185, 187, 189, 191, 193, 196, 198, 200, 202, 204, 207, 209, 211, 214, 216,
    218, 220, 223, 225, 228, 230, 232, 235, 237, 240, 242, 245, 247, 250, 252, 255};

static const uint16_t CIE_BLUE[256] = {
    0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4,
    4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 7,
    7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 10, 10, 10, 10, 11, 11,
    11, 12, 12, 12, 13, 13, 13, 14, 14, 15, 15, 15, 16, 16, 17, 17,
    17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24, 24, 25,
    25, 26, 26, 27, 28, 28, 29, 29, 30, 31, 31, 32, 32, 33, 34, 34,
    35, 36, 37, 37, 38, 39, 39, 40, 41, 42, 43, 43, 44, 45, 46, 47,
    47, 48, 49, 50, 51, 52, 53, 54, 54, 55, 56, 57, 58, 59, 60, 61,
    62, 63, 64, 65, 66, 67, 68, 70, 71, 72, 73, 74, 75, 76, 77, 79,
    80, 81, 82, 83, 85, 86, 87, 88, 90, 91, 92, 94, 95, 96, 98, 99,
    100, 102, 103, 105, 106, 108, 109, 110, 112, 113, 115, 116, 118, 120, 121, 123,
    124, 126, 128, 129, 131, 132, 134, 136, 138, 139, 141, 143, 145, 146, 148, 150,
    152, 154, 155, 157, 159, 161, 163, 165, 167, 169, 171, 173, 175, 177, 179, 181,
    183, 185, 187, 189, 191, 193, 196, 198, 200, 202, 204, 207, 209, 211, 214, 216,
    218, 220, 223, 225, 228, 230, 232, 235, 237, 240, 242, 245, 247, 250, 252, 255};
#endif

uint16_t *bcm_lut[DITHER_PHASES];

// Frame buffer for the HUB75 matrix - memory area where pixel data is stored
volatile uint8_t *frame_buffer;  /// Pointer to < Interwoven image data for examples;
volatile uint8_t *frame_buffer1; ///< Interwoven image data for examples;
volatile uint8_t *frame_buffer2; ///< Interwoven image data for examples;
volatile uint8_t *dma_buffer;    ///< Interwoven image data for examples;

static uint32_t *lut_buffer;

// Three-word record sent to the hub75_row PIO state machine for each row/bit-plane.
// The PIO consumes all three words in order via DMA.
//
// Memory layout (must remain packed, no padding):
//   offset 0: row_address  — 5-bit row select
//   offset 4: lit_cycles   — OEn asserted   (panel ON)  duration, BCM weighted
//   offset 8: dark_cycles  — OEn deasserted (panel OFF) duration
//
// Invariant: lit_cycles + dark_cycles = basis_factor << bit_plane (constant),
// which guarantees a brightness-independent frame rate.
struct row_cmd_t
{
    uint32_t row_address; ///< 5-bit row select; selects which pair of rows to drive
    uint32_t lit_cycles;  ///< OEn ON  duration for this bit plane, scaled by brightness
    uint32_t dark_cycles; ///< OEn OFF duration = full BCM period - lit_cycles

} __attribute__((packed));

volatile row_cmd_t *row_cmd_buffer;
volatile row_cmd_t *row_cmd_buffer1;
volatile row_cmd_t *row_cmd_buffer2;
volatile row_cmd_t *dma_row_cmd_buffer;

static volatile bool swap_row_cmd_buffer_pending = false;
static volatile bool swap_frame_buffer_pending = false;

// Example: A split sequence for 10 bitplanes
// We split BP 9 into 4 parts, BP 8 into 2 parts.
static const uint8_t BCM_SEQUENCE[] = {
    9, 0, 1, 2, 8, 3, 4, 9, 5, 6, 8, 7, 9, 9 // 14 steps instead of 10
};

constexpr uint8_t bcm_sequence_length = sizeof(BCM_SEQUENCE) / sizeof(uint8_t);

// Reload-Buffer (nur Startadresse!)
static uint32_t *row_reload_buffer;

static void configure_pio(bool);
static void setup_dma_transfers();

// Width and height of the HUB75 LED matrix
static uint width;
static uint height;
static uint offset;

// DMA channel numbers
int row_chan;
int row_ctrl_chan;
int pixel_chan;
int pixel_ctrl_chan;

int bitplane_stream_chan;
int bitstream_ctrl_chan;

// PIO configuration structure for state machine numbers and corresponding program offsets
static struct
{
    uint sm_data;
    PIO data_pio;
    uint data_prog_offs;
    uint sm_row;
    PIO row_pio;
    uint row_prog_offs;

    uint sm_bitplane_stream;
    PIO pio_bitplane_stream;
    uint offs_bitplane_stream;
} pio_config;

// Variables for row addressing and bit plane selection
static uint32_t row_address = 0;
static uint32_t bitplane = 0;

static uint32_t row_in_bit_plane = 0;

// Derived constants
static constexpr int ACC_SHIFT = (ACC_BITS - BITPLANES);    // number of low bits preserved in accumulator
static constexpr uint16_t CLAMP_MAX = (1u << ACC_BITS) - 1; // 4095 for 10-bit, 1023 for 8-bit

// Per-channel accumulators (allocated at runtime)
static std::vector<uint16_t> acc_r, acc_g, acc_b;

// Variables for brightness control
// Q format shift: Q16 gives 1.0 == (1 << 16) == 65536
#define BRIGHTNESS_FP_SHIFT 16u

// Brightness as fixed-point Q16 (volatile because it may be changed at runtime)
static volatile uint32_t brightness_fp = (1u << BRIGHTNESS_FP_SHIFT); // default == 1.0

// Basis factor (coarse brightness)
static volatile uint32_t basis_factor = 6u;

// Inverse CIE 1931: perceptual input t (0..1) -> linear light Y (0..1)
//
// L* = t * 100  (scale from normalised to 0..100)
// If L* > 8:    Y = ((L* + 16) / 116)^3
// If L* <= 8:   Y = L* / 903.3
float cie1931_inverse(float t)
{
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;

    float L = t * 100.0f; // scale to 0..100

    float Y;
    if (L > 8.0f)
    {
        float f = (L + 16.0f) / 116.0f;
        Y = f * f * f;
    }
    else
    {
        Y = L / 903.3f; // linear segment for very dark values
    }

    return CLAMP(Y, 0.0f, 1.0f);
}

static inline void compute_bcm_cycles(uint32_t bitplane, uint32_t brightness_fp, uint32_t &lit, uint32_t &dark)
{
    // Full BCM period for this bit plane: doubles with each plane (1, 2, 4, 8 …)
    // scaled by basis_factor for coarse panel calibration.
    uint32_t base = (basis_factor << bitplane);
    // Lit portion: fraction of the full period during which OEn is asserted.
    // brightness_fp is Q16 fixed-point: 0 = off, 65536 = full brightness.
    lit = (uint32_t)((base * (uint64_t)brightness_fp) >> BRIGHTNESS_FP_SHIFT);
    // Dark portion: remaining time OEn is deasserted (panel off).
    // lit + dark = base, so total period is constant regardless of brightness.
    dark = base - lit;
}

static inline uint32_t encode_row_address(uint32_t row)
{
    // 5 Address Lines A–E
    return row & 0x1F;
}

void hub75_build_row_cmd_buffer(uint32_t brightness_fp)
{
    uint32_t idx = 0;

    // Iterate through our custom sequence instead of a linear 0-9
    for (uint8_t bp : BCM_SEQUENCE)
    {
        // Calculate the "weight" of this slice
        // If we split BP 9 into 4 parts, each part gets 1/4 of the duration
        uint32_t split_factor = 1;
        if (bp == 9)
            split_factor = 4;
        else if (bp == 8)
            split_factor = 2;

        for (uint32_t row = 0; row < PanelConfig::SCAN_DEPTH; ++row)
        {
            volatile row_cmd_t *cmd = &row_cmd_buffer[idx++];
            cmd->row_address = encode_row_address(row);

            uint32_t total_lit, total_dark;
            compute_bcm_cycles(bp, brightness_fp, total_lit, total_dark);

            // Divide the time by the number of times this BP appears in the sequence
            cmd->lit_cycles = total_lit / split_factor;
            cmd->dark_cycles = total_dark / split_factor;
        }
    }
}

/**
 * @brief Set the baseline brightness scaling factor for the panel.
 *
 * This acts as the coarse brightness control (default = 6u).
 * The purpose of BASIS_BRIGHTNESS_FACTOR is to calibrate the brightness of a matrix panel.
 * Different matrix panels are likely to have different base brightness levels.
 * Some panels need a higher value of basis_factor some other matrix panels might need a reduced value.
 *
 * @param factor Brightness factor (must be > 0, range 1–255).
 */
void setBasisBrightness(uint8_t factor)
{
    basis_factor = (factor > 0u) ? factor : 1u;

    hub75_build_row_cmd_buffer(brightness_fp);
}

/**
 * @brief Set the runtime brightness level of the panel.
 *
 * This acts as the fine brightness/intensity control, scaling within the
 * current basis brightness range.
 *
 * @param intensity Intensity value in range [0.0f, 1.0f].
 *        Values outside are clamped to the valid range.
 */
void setIntensity(float intensity)
{
    setIntensity(intensity, true);
}

/**
 * @brief Set the runtime brightness level of the panel.
 *
 * This acts as the fine brightness/intensity control, scaling within the
 * current basis brightness range.
 *
 * @param intensity Intensity value in range [0.0f, 1.0f].
 *        Values outside are clamped to the valid range.
 */
void setIntensity(float intensity, bool linear_brightness_control)
{
    if (intensity <= 0.0f)
    {
        brightness_fp = 0;
    }
    else if (intensity >= 1.0f)
    {
        brightness_fp = (1u << BRIGHTNESS_FP_SHIFT);
    }
    else
    {
        float y = intensity;
        if (linear_brightness_control)
        {
            // Convert perceptual input to linear light output.
            // Without this, the panel appears to jump from dark to bright very quickly because human vision is logarithmic.
            y = cie1931_inverse(intensity);
        }
        brightness_fp = (uint32_t)(y * (float)(1u << BRIGHTNESS_FP_SHIFT) + 0.5f);
    }

    hub75_build_row_cmd_buffer(brightness_fp);
}

static volatile uint32_t frame_count = 0;
static volatile uint32_t frame_freq_us = 0; // last measured period for N frames
static absolute_time_t frame_time_start;

#define FRAME_MEASURE_INTERVAL 100

void ctrl_chan_handler()
{
    if (dma_channel_get_irq0_status(row_ctrl_chan))
    {
        // Clear the interrupt request for DMA channel
        dma_channel_acknowledge_irq0(row_ctrl_chan);

        if (frame_count == 0)
        {
            frame_time_start = get_absolute_time();
        }
        else if (frame_count >= FRAME_MEASURE_INTERVAL)
        {
            frame_freq_us = (uint32_t)absolute_time_diff_us(frame_time_start, get_absolute_time());
            frame_count = -1; // reset so it measures again next interval

            uint32_t freq = 1000000u * FRAME_MEASURE_INTERVAL / frame_freq_us;
            printf("Frame frequency: %u Hz\n", freq);
            frame_freq_us = 0; // clear until next measurement
        }
        frame_count++;

        if (swap_row_cmd_buffer_pending)
        {
            dma_row_cmd_buffer = (row_cmd_buffer == row_cmd_buffer1) ? row_cmd_buffer2 : row_cmd_buffer1;
            swap_row_cmd_buffer_pending = false;
        }
    }
    else if (dma_channel_get_irq0_status(pixel_ctrl_chan))
    {
        // Clear the interrupt request for DMA channel
        dma_channel_acknowledge_irq0(pixel_ctrl_chan);

        if (swap_frame_buffer_pending)
        {
            dma_buffer = (frame_buffer == frame_buffer1) ? frame_buffer2 : frame_buffer1;
            swap_frame_buffer_pending = false;
        }
    }

    dma_channel_start(pixel_chan);
    dma_channel_start(row_chan);
}

void setup_irq()
{
    dma_channel_set_irq0_enabled(row_ctrl_chan, true);
    dma_channel_set_irq0_enabled(pixel_ctrl_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, ctrl_chan_handler);
    irq_set_enabled(DMA_IRQ_0, true);
}

// void bitplane_stream_chan_handler()
// {
//     // Clear the interrupt request for DMA channel
//     dma_channel_acknowledge_irq0(bitplane_stream_chan);

//     // go through all bitplanes in BCM_SEQUENCE
//     if ( ++bitplane < bcm_sequence_length ) {
//         uint shamt = BCM_SEQUENCE[bitplane];
//         hub75_bitplane_setup_set_shift(pio_config.pio_bitplane_stream, pio_config.sm_bitplane_stream, pio_config.offs_bitplane_stream, shamt);

//         dma_channel_set_read_addr(bitplane_stream_chan, lut_buffer, true);
//     } else {
//         bitplane = 0;

//         swap_frame_buffer_pending = true;
//         frame_buffer = (frame_buffer == frame_buffer1) ? frame_buffer2 : frame_buffer1;
//     }
// }

// void setup_bitplane_stream_irq()
// {
//     dma_channel_set_irq1_enabled(bitplane_stream_chan, true);
//     irq_set_exclusive_handler(DMA_IRQ_1, bitplane_stream_chan_handler);
//     irq_set_enabled(DMA_IRQ_1, true);
// }

/**
 * @brief Initializes the HUB75 display by setting up DMA and PIO subsystems.
 *
 * This function configures the necessary hardware components to drive a HUB75
 * LED matrix display. It initializes DMA channels, PIO state machines, and
 * interrupt handlers.
 *
 * @param w Width of the HUB75 display in pixels.
 * @param h Height of the HUB75 display in pixels.
 */
void create_hub75_driver(uint w, uint h, uint panel_type = PANEL_TYPE, bool inverted_stb = INVERTED_STB)
{
    width = w;
    height = h;

    frame_buffer1 = new uint8_t[PanelConfig::WIDTH * PanelConfig::HEIGHT / PanelConfig::ROWS_IN_PARALLEL * bcm_sequence_length]; // Allocate memory for frame buffer
    frame_buffer2 = new uint8_t[PanelConfig::WIDTH * PanelConfig::HEIGHT / PanelConfig::ROWS_IN_PARALLEL * bcm_sequence_length]; // Allocate memory for frame buffer

    dma_buffer = frame_buffer1;
    frame_buffer = frame_buffer2;

    row_cmd_buffer1 = new row_cmd_t[PanelConfig::SCAN_DEPTH * bcm_sequence_length];
    row_cmd_buffer2 = new row_cmd_t[PanelConfig::SCAN_DEPTH * bcm_sequence_length];

    dma_row_cmd_buffer = row_cmd_buffer1;
    row_cmd_buffer = row_cmd_buffer2;

    // *bcm_lut = new uint16_t[256][DITHER_PHASES]();

    lut_buffer = new uint32_t[width * height](); // Allocate memory for frame buffer

    offset = width * PanelConfig::SCAN_DEPTH;

    if (panel_type == PANEL_FM6126A)
    {
        FM6126A_setup();
    }
    else if (panel_type == PANEL_RUL6024)
    {
        RUL6024_setup();
    }

    configure_pio(inverted_stb);
    setup_dma_transfers();
    setup_irq();
    hub75_build_row_cmd_buffer(brightness_fp);
}

/**
 * @brief Starts the DMA transfers for the HUB75 display driver.
 *
 * This function initializes the DMA transfers by setting up the write address
 * for the Output Enable finished DMA channel and the read address for pixel data.
 * It ensures that the display begins processing frames.
 */
void start_hub75_driver()
{
    dma_row_cmd_buffer = row_cmd_buffer2;
    row_cmd_buffer = row_cmd_buffer1;

    dma_buffer = frame_buffer2;
    frame_buffer = frame_buffer1;

    swap_row_cmd_buffer_pending = false;
    swap_frame_buffer_pending = false;

    dma_channel_set_read_addr(row_ctrl_chan, &dma_row_cmd_buffer, false);
    dma_channel_set_read_addr(pixel_ctrl_chan, &dma_buffer, false);

    dma_channel_set_read_addr(pixel_chan, dma_buffer, true);
    dma_channel_set_read_addr(row_chan, dma_row_cmd_buffer, true);
}

/**
 * @brief Configures the PIO state machines for HUB75 matrix control.
 *
 * This function sets up the PIO state machines responsible for shifting
 * pixel data and controlling row addressing. If a PIO state machine cannot
 * be claimed, it prints an error message.
 */
static void configure_pio(bool inverted_stb)
{
    // On RP2350B, GPIO 30-47 are only accessible via PIO2
    // Force both state machines onto PIO2
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &hub75_bitplane_stream_program,
            &pio_config.data_pio,
            &pio_config.sm_data,
            &pio_config.data_prog_offs,
            DATA_BASE_PIN, DATA_N_PINS + 1, true)) // +1 for CLK
    {
        panic("Failed to claim PIO SM for hub75_bitplane_stream_program\n");
    }

    if (inverted_stb)
    {
        if (!pio_claim_free_sm_and_add_program_for_gpio_range(
                &hub75_row_inverted_program,
                &pio_config.row_pio,
                &pio_config.sm_row,
                &pio_config.row_prog_offs,
                ROWSEL_BASE_PIN, ROWSEL_N_PINS + 2, true)) // +2 for STROBE+OEN
        {
            panic("Failed to claim PIO SM for hub75_row_inverted_program\n");
        }
    }
    else
    {
        if (!pio_claim_free_sm_and_add_program_for_gpio_range(
                &hub75_row_program,
                &pio_config.row_pio,
                &pio_config.sm_row,
                &pio_config.row_prog_offs,
                ROWSEL_BASE_PIN, ROWSEL_N_PINS + 2, true)) // +2 for STROBE+OEN
        {
            panic("Failed to claim PIO SM for hub75_row_program\n");
        }
    }

    // Implementation of Pimoronis anti ghosting solution: https://github.com/pimoroni/pimoroni-pico/commit/9e7c2640d426f7b97ca2d5e9161d3f0a00f21abf
    uint wait_cycles = clock_get_hz(clk_sys) / 4000000;

    hub75_bitplane_stream_program_init(pio_config.data_pio, pio_config.sm_data, pio_config.data_prog_offs, DATA_BASE_PIN, CLK_PIN, MATRIX_PANEL_WIDTH);

    if (inverted_stb)
        hub75_row_inverted_program_init(pio_config.row_pio, pio_config.sm_row, pio_config.row_prog_offs, ROWSEL_BASE_PIN, ROWSEL_N_PINS, STROBE_PIN, wait_cycles);
    else
        hub75_row_program_init(pio_config.row_pio, pio_config.sm_row, pio_config.row_prog_offs, ROWSEL_BASE_PIN, ROWSEL_N_PINS, STROBE_PIN, wait_cycles);

    if (!pio_claim_free_sm_and_add_program(
            &hub75_bitplane_stream_program,
            &pio_config.pio_bitplane_stream,
            &pio_config.sm_bitplane_stream,
            &pio_config.offs_bitplane_stream))
    {
        panic("Failed to claim PIO SM for hub75_bitplane_stream_program\n");
    }
}

/**
 * @brief Sets up DMA transfers for the HUB75 matrix.
 *
 * Configures multiple DMA channels to transfer pixel data, dummy pixel data,
 * and output enable signal, to the PIO state machines controlling the HUB75 matrix.
 * Also configures the DMA channel which gets active when an output enable signal has finished
 */
static void setup_dma_transfers()
{
    row_chan = dma_claim_unused_channel(true);
    row_ctrl_chan = dma_claim_unused_channel(true);

    // row channel
    dma_channel_config row_chan_config = dma_channel_get_default_config(row_chan);

    channel_config_set_transfer_data_size(&row_chan_config, DMA_SIZE_32);
    channel_config_set_read_increment(&row_chan_config, true);
    channel_config_set_write_increment(&row_chan_config, false);

    channel_config_set_high_priority(&row_chan_config, true);

    channel_config_set_dreq(&row_chan_config, pio_get_dreq(pio_config.row_pio, pio_config.sm_row, true));

    channel_config_set_chain_to(&row_chan_config, row_ctrl_chan);

    dma_channel_configure(row_chan, &row_chan_config, &pio_config.row_pio->txf[pio_config.sm_row], dma_row_cmd_buffer, dma_encode_transfer_count(bcm_sequence_length * PanelConfig::SCAN_DEPTH * 3), false);

    // row ctrl channel
    dma_channel_config row_ctrl_chan_config = dma_channel_get_default_config(row_ctrl_chan);

    channel_config_set_transfer_data_size(&row_ctrl_chan_config, DMA_SIZE_32);
    channel_config_set_read_increment(&row_ctrl_chan_config, false);
    channel_config_set_write_increment(&row_ctrl_chan_config, false);

    channel_config_set_dreq(&row_ctrl_chan_config, DREQ_FORCE);

    channel_config_set_high_priority(&row_chan_config, true);

    dma_channel_configure(row_ctrl_chan, &row_ctrl_chan_config, &dma_hw->ch[row_chan].read_addr, dma_row_cmd_buffer, 1, false);

    // pixel channel
    pixel_chan = dma_claim_unused_channel(true);
    pixel_ctrl_chan = dma_claim_unused_channel(true);

    dma_channel_config pixel_chan_config = dma_channel_get_default_config(pixel_chan);

    channel_config_set_transfer_data_size(&pixel_chan_config, DMA_SIZE_8);
    channel_config_set_read_increment(&pixel_chan_config, true);
    channel_config_set_write_increment(&pixel_chan_config, false);

    channel_config_set_dreq(&pixel_chan_config, pio_get_dreq(pio_config.data_pio, pio_config.sm_data, true));

    channel_config_set_high_priority(&pixel_chan_config, true);

    channel_config_set_chain_to(&pixel_chan_config, pixel_ctrl_chan);

    dma_channel_configure(pixel_chan, &pixel_chan_config, &pio_config.data_pio->txf[pio_config.sm_data], dma_buffer, dma_encode_transfer_count(width * PanelConfig::SCAN_DEPTH * bcm_sequence_length), false);

    // pixel ctrl channel
    dma_channel_config pixel_ctrl_chan_config = dma_channel_get_default_config(pixel_ctrl_chan);

    channel_config_set_transfer_data_size(&pixel_ctrl_chan_config, DMA_SIZE_32);
    channel_config_set_read_increment(&pixel_ctrl_chan_config, false);
    channel_config_set_write_increment(&pixel_ctrl_chan_config, false);

    channel_config_set_dreq(&pixel_ctrl_chan_config, DREQ_FORCE);

    channel_config_set_high_priority(&pixel_ctrl_chan_config, true);

    dma_channel_configure(pixel_ctrl_chan, &pixel_ctrl_chan_config, &dma_hw->ch[pixel_chan].read_addr, dma_buffer, 1, false);

    pio_sm_set_clkdiv(pio_config.data_pio, pio_config.sm_data, std::max(SM_CLOCKDIV_FACTOR, 1.0f));
    pio_sm_set_clkdiv(pio_config.row_pio, pio_config.sm_row, std::max(SM_CLOCKDIV_FACTOR, 1.0f));
}

static void setup_bitplane_creation()
{
    bitplane_stream_chan = dma_claim_unused_channel(true);
    // bitstream_ctrl_chan = dma_claim_unused_channel(true);

    // bitstream channel
    dma_channel_config bitplane_stream_chan_config = dma_channel_get_default_config(row_chan);

    channel_config_set_transfer_data_size(&bitplane_stream_chan_config, DMA_SIZE_32);
    channel_config_set_read_increment(&bitplane_stream_chan_config, true);
    channel_config_set_write_increment(&bitplane_stream_chan_config, true);

    channel_config_set_high_priority(&bitplane_stream_chan_config, true);

    channel_config_set_dreq(&bitplane_stream_chan_config, pio_get_dreq(pio_config.pio_bitplane_stream, pio_config.sm_bitplane_stream, true));

    channel_config_set_chain_to(&bitplane_stream_chan_config, bitstream_ctrl_chan);

    dma_channel_configure(row_chan, &bitplane_stream_chan_config, &pio_config.row_pio->txf[pio_config.sm_bitplane_stream], lut_buffer, dma_encode_transfer_count(PIXELS), false);

    // // bitstream ctrl channel
    // dma_channel_config bitstream_ctrl_chan_config = dma_channel_get_default_config(bitstream_ctrl_chan);

    // channel_config_set_transfer_data_size(&bitstream_ctrl_chan_config, DMA_SIZE_32);
    // channel_config_set_read_increment(&bitstream_ctrl_chan_config, false);
    // channel_config_set_write_increment(&bitstream_ctrl_chan_config, false);

    // channel_config_set_dreq(&bitstream_ctrl_chan_config, DREQ_FORCE);

    // channel_config_set_high_priority(&bitstream_ctrl_chan_config, true);

    // dma_channel_configure(row_ctrl_chan, &bitstream_ctrl_chan_config, &dma_hw->ch[row_chan].read_addr, dma_row_cmd_buffer, 1, false);
}

// Helper: apply LUT and pack into 30-bit RGB (10 bits per channel)
static inline uint32_t pack_lut_rgb(uint32_t color, const uint16_t *lut)
{
    return (lut[(color & 0x0000ff)] << (2 * BITPLANES)) |
           (lut[(color >> 8) & 0x0000ff] << BITPLANES) |
           (lut[(color >> 16) & 0x0000ff]);
}

// Helper: apply LUT and pack into 30-bit RGB (10 bits per channel)
static inline uint32_t pack_lut_rgb_(uint32_t r, uint32_t g, uint32_t b, const uint16_t *lut)
{
    return lut[r] << (2 * BITPLANES) | lut[g] << BITPLANES | lut[b];
}

void build_bitplanes_optimized(
    const uint32_t *__restrict src, // RGB101010 packed
    uint32_t *__restrict dst,       // DMA buffer
    uint32_t num_pixels)
{
    constexpr int BP = BITPLANES;

    for (uint32_t i = 0; i < num_pixels; i += 32)
    {
        // Akkumulatoren für 32 Pixel
        uint32_t r_plane[BP] = {0};
        uint32_t g_plane[BP] = {0};
        uint32_t b_plane[BP] = {0};

        // 32 Pixel blockweise verarbeiten
        for (uint32_t bit = 0; bit < 32; ++bit)
        {
            uint32_t p = src[i + bit];

            uint32_t r = (p >> 20) & 0x3FF;
            uint32_t g = (p >> 10) & 0x3FF;
            uint32_t b = (p >> 0) & 0x3FF;

            // jetzt ALLE Bitplanes gleichzeitig extrahieren
            for (uint32_t bp = 0; bp < BP; ++bp)
            {
                uint32_t mask = 1u << bp;

                r_plane[bp] |= ((r >> bp) & 1u) << bit;
                g_plane[bp] |= ((g >> bp) & 1u) << bit;
                b_plane[bp] |= ((b >> bp) & 1u) << bit;
            }
        }

        // jetzt linear in Ziel schreiben (DMA-freundlich!)
        for (uint32_t bp = 0; bp < BP; ++bp)
        {
            *dst++ = r_plane[bp];
            *dst++ = g_plane[bp];
            *dst++ = b_plane[bp];
        }
    }
}

static inline void build_expanded_rgb10(const uint32_t *src, uint32_t *dst, size_t pixels)
{
    for (size_t i = 0; i < pixels; ++i)
    {
        uint32_t p = src[i];

        // Standardize: Red = High (20), Green = Mid (10), Blue = Low (0)
        uint32_t r = CIE_RED[(p >> 16) & 0xFF];  // R from source (bits 16-23)
        uint32_t g = CIE_GREEN[(p >> 8) & 0xFF]; // G from source (bits 8-15)
        uint32_t b = CIE_BLUE[p & 0xFF];         // B from source (bits 0-7)

        // CORRECT PACKING FOR INTERLEAVER:
        dst[i] = ((b & 0x3FF) << 20) | ((g & 0x3FF) << 10) | (r & 0x3FF);
    }
}

void setup_interp_for_bitplane(uint8_t bp)
{
    // We use INTERP0 for the TOP pixel and INTERP1 for the BOTTOM pixel

    // --- INTERP0: TOP PIXEL (Outputs to bits 5, 4, 3) ---
    interp_config cfg0 = interp_default_config();
    // Lane 0: Extract Red (bit 20+bp) -> Move to bit 5
    interp_config_set_shift(&cfg0, 20 + bp);
    interp_config_set_mask(&cfg0, 0, 0); // Only bit 0
    // Logic: result = (accum >> shift) & mask.
    // We'll use a manual shift in the loop for the G/B bits to keep it simple,
    // OR use the 'Cross-result' to OR them together.

    // --- INTERP1: BOTTOM PIXEL (Outputs to bits 2, 1, 0) ---
    interp_config cfg1 = interp_default_config();
    interp_config_set_shift(&cfg1, 20 + bp); // Bottom Red
    interp_config_set_mask(&cfg1, 0, 0);

    interp_set_config(interp0, 0, &cfg0);
    interp_set_config(interp1, 0, &cfg1);
}

void build_plane_interpolator(const uint32_t *top, const uint32_t *bot, volatile uint8_t *dst, size_t width, uint8_t bp)
{
    for (size_t x = 0; x < width; ++x)
    {
        uint32_t t = top[x];
        uint32_t b = bot[x];

        // We extract 1 bit from each color field (R=20, G=10, B=0)
        // and pack them into the 6-bit HUB75 byte: [TR TG TB BR BG BB]

        uint8_t res = 0;
        // Top Pixel (Bits 5, 4, 3)
        res |= ((t >> (20 + bp)) & 1) << 5; // Red
        res |= ((t >> (10 + bp)) & 1) << 4; // Green
        res |= ((t >> (0 + bp)) & 1) << 3;  // Blue

        // Bottom Pixel (Bits 2, 1, 0)
        res |= ((b >> (20 + bp)) & 1) << 2; // Red
        res |= ((b >> (10 + bp)) & 1) << 1; // Green
        res |= ((b >> (0 + bp)) & 1) << 0;  // Blue

        dst[x] = res;
    }
}

void build_bitplanes_interleaved(const uint32_t *__restrict expanded,
                                 volatile uint8_t *__restrict dst,
                                 size_t width)
{
    const size_t rows = PanelConfig::SCAN_DEPTH;
    size_t current_plane_offset = 0;

    for (uint8_t bp : BCM_SEQUENCE)
    {
        // 1. CONFIGURE HARDWARE ONCE PER BITPLANE
        // This sets the internal shifts/masks for the current 'bp'
        setup_interp_for_bitplane(bp);

        volatile uint8_t *bp_base = dst + current_plane_offset;

        for (size_t row = 0; row < rows; ++row)
        {
            const uint32_t *top = expanded + (row + rows) * width;
            const uint32_t *bot = expanded + row * width;
            volatile uint8_t *out = bp_base + (row * width);

            // 2. THE FAST STREAMING LOOP
            // This function should now ONLY contain the interp->accum/peek logic
            build_plane_interpolator(top, bot, (uint8_t *)out, width, bp);
        }
        current_plane_offset += (rows * width);
    }
}

enum class ColorOrder
{
    RGB,
    BGR
};

template <ColorOrder Order>
void build_expanded_generic(
    const uint8_t *__restrict src,
    uint32_t *__restrict expanded,
    size_t pixels)
{
    uint b = 2;
    uint r = 0;

    if constexpr (Order == ColorOrder::BGR)
    {
        b = 0;
        r = 2;
    }

    for (size_t i = 0; i < pixels; ++i)
    {
        const uint8_t *p = &src[i * 3];

        // Pack into 10-bit slots for the bitplane engine
        expanded[i] = (CIE_RED[p[r]] << 20) | (CIE_GREEN[p[1]] << 10) | CIE_BLUE[p[b]];
    }
}

#if USE_PICO_GRAPHICS == true
/**
 * @brief Update frame_buffer from PicoGraphics source (RGB888 / packed 32-bit),
 *
 * @param src Graphics object to be updated - RGB888 format, 24-bits in uint32_t array
 */
__attribute__((optimize("unroll-loops"))) void update(
    PicoGraphics const *graphics // Graphics object to be updated - RGB888 format, 24-bits in uint32_t array
)
{
    if (graphics->pen_type != PicoGraphics::PEN_RGB888)
        return;

    __attribute__((aligned(4))) uint32_t const *src = static_cast<uint32_t const *>(graphics->frame_buffer);

#if defined(HUB75_MULTIPLEX_2_ROWS)
    constexpr size_t pixels = MATRIX_PANEL_WIDTH * MATRIX_PANEL_HEIGHT;

    build_expanded_rgb10(src, lut_buffer, pixels);
    build_bitplanes_interleaved(lut_buffer, frame_buffer, width);

    // dma_channel_set_write_addr(bitplane_stream_chan, frame_buffer, false);
    // dma_channel_set_read_addr(bitplane_stream_chan, lut_buffer, true);
#elif defined HUB75_P10_3535_16X32_4S
    int line = 0;
    int counter = 0;

    constexpr int COLUMN_PAIRS = MATRIX_PANEL_WIDTH >> 1;
    constexpr int HALF_PAIRS = COLUMN_PAIRS >> 1;

    constexpr int PAIR_HALF_BIT = HALF_PAIRS;
    constexpr int PAIR_HALF_SHIFT = __builtin_ctz(HALF_PAIRS);

    constexpr int ROW_STRIDE = MATRIX_PANEL_WIDTH;
    constexpr int ROWS_PER_GROUP = MATRIX_PANEL_HEIGHT / SCAN_GROUPS;
    constexpr int GROUP_ROW_OFFSET = ROWS_PER_GROUP * ROW_STRIDE;
    constexpr int HALF_PANEL_OFFSET = (MATRIX_PANEL_HEIGHT >> 1) * ROW_STRIDE;

    constexpr int total_pairs = (MATRIX_PANEL_WIDTH * MATRIX_PANEL_HEIGHT) >> 1;

    for (int j = 0, fb_index = 0; j < total_pairs; ++j, fb_index += 2)
    {
        int32_t index = !(j & PAIR_HALF_BIT) ? j - (line << PAIR_HALF_SHIFT)
                                             : GROUP_ROW_OFFSET + j - ((line + 1) << PAIR_HALF_SHIFT);

        frame_buffer[fb_index] = LUT_MAPPING(fb_index, src[index]);
        frame_buffer[fb_index + 1] = LUT_MAPPING(fb_index + 1, src[index + HALF_PANEL_OFFSET]);

        if (++counter >= COLUMN_PAIRS)
        {
            counter = 0;
            ++line;
        }
    }
#elif defined HUB75_P3_1415_16S_64X64_S31
    constexpr uint total_pixels = MATRIX_PANEL_WIDTH * MATRIX_PANEL_HEIGHT;
    constexpr uint line_offset = 2 * MATRIX_PANEL_WIDTH;

    constexpr uint quarter = total_pixels >> 2;

    uint quarter1 = 0 * quarter;
    uint quarter2 = 1 * quarter;
    uint quarter3 = 2 * quarter;
    uint quarter4 = 3 * quarter;

    uint p = 0; // per line pixel counter

    // Number of logical rows processed
    uint line = 0;

    // Framebuffer write pointer
    volatile uint32_t *dst = frame_buffer;

    // Each iteration processes 4 physical rows (2 scan-row pairs)
    while (line < (height >> 2))
    {
        ptrdiff_t fb_index = dst - frame_buffer;
        // even src lines
        dst[0] = LUT_MAPPING(fb_index, src[quarter2]);
        quarter2++;
        dst[1] = LUT_MAPPING(fb_index + 1, src[quarter4]);
        quarter4++;
        // odd src lines
        dst[line_offset + 0] = LUT_MAPPING(fb_index + line_offset, src[quarter1]);
        quarter1++;
        dst[line_offset + 1] = LUT_MAPPING(fb_index + line_offset + 1, src[quarter3]);
        quarter3++;

        dst += 2;
        p++;

        // End of logical row
        if (p == width)
        {
            p = 0;
            line++;
            dst += line_offset; // advance to next scan-row pair
        }
    }
#endif
    uint32_t irq = save_and_disable_interrupts();
    swap_frame_buffer_pending = true;
    frame_buffer = (frame_buffer == frame_buffer1) ? frame_buffer2 : frame_buffer1;
    restore_interrupts(irq);
}
#endif

/**
 * @brief Updates the frame buffer with pixel data from the source array.
 *
 * This function takes a source array of pixel data and updates the frame buffer
 * with interleaved pixel values. The pixel values are gamma-corrected to 10 bits using a lookup table.
 *
 * @param src Graphics object to be updated - RGB888 format, 24-bits in uint32_t array
 */
__attribute__((optimize("unroll-loops"))) void update_bgr(const uint8_t *src)
{
#ifdef HUB75_MULTIPLEX_2_ROWS

    constexpr size_t pixels = MATRIX_PANEL_WIDTH * MATRIX_PANEL_HEIGHT;

    build_expanded_generic<ColorOrder::RGB>(src, lut_buffer, pixels);
    build_bitplanes_interleaved(lut_buffer, frame_buffer, width);

#elif defined HUB75_P10_3535_16X32_4S
    int line = 0;
    int counter = 0;

    constexpr int COLUMN_PAIRS = MATRIX_PANEL_WIDTH >> 1;
    constexpr int HALF_PAIRS = COLUMN_PAIRS >> 1;

    constexpr int PAIR_HALF_BIT = HALF_PAIRS;
    constexpr int PAIR_HALF_SHIFT = __builtin_ctz(HALF_PAIRS);

    constexpr int ROW_STRIDE = MATRIX_PANEL_WIDTH;
    constexpr int ROWS_PER_GROUP = MATRIX_PANEL_HEIGHT / SCAN_GROUPS;
    constexpr int GROUP_ROW_OFFSET = ROWS_PER_GROUP * ROW_STRIDE;
    constexpr int HALF_PANEL_OFFSET = ((MATRIX_PANEL_HEIGHT >> 1) * ROW_STRIDE) * 3;

    constexpr int total_pairs = (MATRIX_PANEL_WIDTH * MATRIX_PANEL_HEIGHT) >> 1;

    for (int j = 0, fb_index = 0; j < total_pairs; ++j, fb_index += 2)
    {
        int32_t index = !(j & PAIR_HALF_BIT) ? (j - (line << PAIR_HALF_SHIFT)) * 3
                                             : (GROUP_ROW_OFFSET + j - ((line + 1) << PAIR_HALF_SHIFT)) * 3;

        frame_buffer[fb_index] = LUT_MAPPING_RGB(fb_index, src[index], src[index + 1], src[index + 2]);
        index += HALF_PANEL_OFFSET;
        frame_buffer[fb_index + 1] = LUT_MAPPING_RGB(fb_index + 1, src[index], src[index + 1], src[index + 2]);

        if (++counter >= COLUMN_PAIRS)
        {
            counter = 0;
            ++line;
        }
    }
#elif defined HUB75_P3_1415_16S_64X64_S31
    constexpr uint total_pixels = MATRIX_PANEL_WIDTH * MATRIX_PANEL_HEIGHT;
    constexpr uint line_width = 2 * MATRIX_PANEL_WIDTH;

    constexpr uint quarter = (total_pixels >> 2) * 3;

    uint quarter1 = 0 * quarter;
    uint quarter2 = 1 * quarter;
    uint quarter3 = 2 * quarter;
    uint quarter4 = 3 * quarter;

    uint p = 0; // per line pixel counter

    // Number of logical rows processed
    uint line = 0;

    // Framebuffer write pointer
    volatile uint32_t *dst = frame_buffer;

    // Each iteration processes 4 physical rows (2 scan-row pairs)
    while (line < (height >> 2))
    {
        ptrdiff_t fb_index = dst - frame_buffer;
        // even src lines
        dst[0] = LUT_MAPPING_RGB(fb_index, src[quarter2], src[quarter2 + 1], src[quarter2 + 2]);
        quarter2 += 3;
        dst[1] = LUT_MAPPING_RGB(fb_index + 1, src[quarter4], src[quarter4 + 1], src[quarter4 + 2]);
        quarter4 += 3;
        // odd src lines
        dst[line_width + 0] = LUT_MAPPING_RGB(fb_index + line_width, src[quarter1], src[quarter1 + 1], src[quarter1 + 2]);
        quarter1 += 3;
        dst[line_width + 1] = LUT_MAPPING_RGB(fb_index + line_width + 1, src[quarter3], src[quarter3 + 1], src[quarter3 + 2]);
        quarter3 += 3;

        dst += 2;
        p++;

        // End of logical row
        if (p == width)
        {
            p = 0;
            line++;
            dst += line_width; // advance to next scan-row pair
        }
    }
#endif
    uint32_t irq = save_and_disable_interrupts();
    swap_frame_buffer_pending = true;
    frame_buffer = (frame_buffer == frame_buffer1) ? frame_buffer2 : frame_buffer1;
    restore_interrupts(irq);
}
