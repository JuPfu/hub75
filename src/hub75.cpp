#include <cstdlib>
#include <vector>
#include <memory>

#include "pico/stdlib.h"

#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pico/sync.h"

#include "hub75.hpp"
#include "hub75.pio.h"

#include "rul6024.h"
#include "fm6126a.h"
#include "fm6124dj.h"

// Deduced from https://jared.geek.nz/2013/02/linear-led-pwm/
// The CIE 1931 lightness formula is what actually describes how we perceive light.

#if BIT_DEPTH == 10
#if TEMPORAL_DITHERING != false
static const uint16_t lut[256] = {
    0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 18, 20, 21, 23, 25, 27,
    28, 30, 32, 34, 36, 37, 39, 41, 43, 45, 47, 49, 52, 54, 56, 59,
    61, 64, 66, 69, 72, 75, 77, 80, 83, 87, 90, 93, 96, 100, 103, 107,
    111, 115, 118, 122, 126, 131, 135, 139, 144, 148, 153, 157, 162, 167, 172, 177,
    182, 187, 193, 198, 204, 209, 215, 221, 227, 233, 239, 246, 252, 259, 265, 272,
    279, 286, 293, 300, 308, 315, 323, 330, 338, 346, 354, 362, 371, 379, 388, 396,
    405, 414, 423, 432, 442, 451, 461, 470, 480, 490, 501, 511, 521, 532, 543, 553,
    564, 576, 587, 598, 610, 622, 634, 646, 658, 670, 683, 695, 708, 721, 734, 748,
    761, 775, 788, 802, 816, 831, 845, 860, 874, 889, 904, 920, 935, 951, 966, 982,
    999, 1015, 1031, 1048, 1065, 1082, 1099, 1116, 1134, 1152, 1170, 1188, 1206, 1224, 1243, 1262,
    1281, 1300, 1320, 1339, 1359, 1379, 1399, 1420, 1440, 1461, 1482, 1503, 1525, 1546, 1568, 1590,
    1612, 1635, 1657, 1680, 1703, 1726, 1750, 1774, 1797, 1822, 1846, 1870, 1895, 1920, 1945, 1971,
    1996, 2022, 2048, 2074, 2101, 2128, 2155, 2182, 2209, 2237, 2265, 2293, 2321, 2350, 2378, 2407,
    2437, 2466, 2496, 2526, 2556, 2587, 2617, 2648, 2679, 2711, 2743, 2774, 2807, 2839, 2872, 2905,
    2938, 2971, 3005, 3039, 3073, 3107, 3142, 3177, 3212, 3248, 3283, 3319, 3356, 3392, 3429, 3466,
    3503, 3541, 3578, 3617, 3655, 3694, 3732, 3772, 3811, 3851, 3891, 3931, 3972, 4012, 4054, 4095};
#else
static const uint16_t lut[256] = {
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
#endif
#elif BIT_DEPTH == 8
#if TEMPORAL_DITHERING != false
static const uint16_t lut[256] = {
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
#else
static const uint16_t lut[256] = {
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
#endif

// Frame buffer for the HUB75 matrix - memory area where pixel data is stored
volatile uint32_t *frame_buffer;  /// Pointer to < Interwoven image data for examples;
volatile uint32_t *frame_buffer1; ///< Interwoven image data for examples;
volatile uint32_t *frame_buffer2; ///< Interwoven image data for examples;
volatile uint32_t *dma_buffer;    ///< Interwoven image data for examples;
static volatile bool swap_pending = false;

static void configure_dma_channels();
static void configure_pio(bool);
static void setup_dma_transfers();
static void setup_dma_irq();

// Dummy pixel data emitted at the end of each row to ensure the last genuine pixels of a row are displayed - keep volatile!
static volatile uint32_t dummy_pixel_data[2] = {0x0, 0x0};
// Control data for the latch signal
static volatile uint32_t row_start_data = 0;

// Width and height of the HUB75 LED matrix
static uint width;
static uint height;
static uint offset;

// DMA channel numbers
int pixel_chan;
int dummy_pixel_chan;
int oen_chan;

// This channel's interrupt handler restarts the pixel data DMA channel.
int row_start_chan;

// PIO configuration structure for state machine numbers and corresponding program offsets
static struct
{
    uint sm_data;
    PIO data_pio;
    uint data_prog_offs;
    uint sm_row;
    PIO row_pio;
    uint row_prog_offs;
} pio_config;

// Variables for row addressing and bit plane selection
static uint32_t row_address = 0;
static uint32_t bit_plane = 0;

static uint32_t wait_cycles = 0;

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
struct OenRecord
{
    uint32_t row_address; ///< 5-bit row select; selects which pair of rows to drive
    uint32_t lit_cycles;  ///< OEn ON  duration for this bit plane, scaled by brightness
    uint32_t dark_cycles; ///< OEn OFF duration = full BCM period - lit_cycles
} __attribute__((packed));

static volatile OenRecord oen_data = {0, 0, 0};

// Derived constants
static constexpr int ACC_SHIFT = (ACC_BITS - BIT_DEPTH);    // number of low bits preserved in accumulator
static constexpr uint16_t CLAMP_MAX = (1u << ACC_BITS) - 1; // 4095 for 10-bit, 1023 for 8-bit

// Per-channel accumulators (allocated at runtime)
static std::vector<uint16_t> acc_r, acc_g, acc_b;

// Variables for brightness control
// Q format shift: Q16 gives 1.0 == (1 << 16) == 65536
#define BRIGHTNESS_FP_SHIFT 10u

// Brightness as fixed-point Q16 (volatile because it may be changed at runtime)
static volatile uint32_t brightness_fp = (1u << BRIGHTNESS_FP_SHIFT); // default == 1.0

// Precomputed scaled lit and dark cycles per bit plane to avoid calculating in ISR
static volatile uint32_t lit_cycles[BIT_DEPTH];
static volatile uint32_t dark_cycles[BIT_DEPTH];

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

// Recompute scaled_basis[] using a temporary array and swap under IRQ protection.
__attribute__((optimize("unroll-loops"))) static void recompute_scaled_basis()
{
    uint32_t tmp_lit[BIT_DEPTH];
    uint32_t tmp_dark[BIT_DEPTH];

    for (int b = 0; b < BIT_DEPTH; ++b)
    {
        // Full BCM period for this bit plane: doubles with each plane (1, 2, 4, 8 …)
        // scaled by basis_factor for coarse panel calibration.
        uint32_t base = basis_factor << b;
        // Lit portion: fraction of the full period during which OEn is asserted.
        // brightness_fp is Q16 fixed-point: 0 = off, 65536 = full brightness.
        tmp_lit[b] = (uint32_t)((base * (uint64_t)brightness_fp) >> BRIGHTNESS_FP_SHIFT);
        // Dark portion: remaining time OEn is deasserted (panel off).
        // lit + dark = base, so total period is constant regardless of brightness.
        tmp_dark[b] = base - tmp_lit[b];
    }

    // update scaled_basis atomically with regard to interrupts reading it
    uint32_t irq = save_and_disable_interrupts();
    for (int b = 0; b < BIT_DEPTH; ++b)
    {
        lit_cycles[b] = tmp_lit[b];
        dark_cycles[b] = tmp_dark[b];
    }

    restore_interrupts(irq);
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
    recompute_scaled_basis();
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
    recompute_scaled_basis();
}

/**
 * @brief Initialize per-pixel accumulators used for temporal dithering.
 *
 * This must be called after width and height are set and after the frame_buffer allocation.
 * Allocates three arrays of width*height uint32 accumulators (R, G, B) and zero-initializes them.
 */
static void init_accumulators(std::size_t pixel_count)
{
    acc_r.assign(pixel_count, 0);
    acc_g.assign(pixel_count, 0);
    acc_b.assign(pixel_count, 0);
}

static volatile uint32_t frame_count = 0;
static volatile uint32_t frame_freq_us = 0; // last measured period for N frames
static absolute_time_t frame_time_start;

#define FRAME_MEASURE_INTERVAL 100

/**
 * @brief Interrupt handler for the Output Enable (OEn) finished event.
 *
 * This interrupt is triggered when the output enable DMA transaction is completed.
 * It updates row addressing and bit-plane selection for the next frame,
 * modifies the PIO state machine instruction, and restarts DMA transfers
 * for pixel data to ensure continuous frame updates.
 */
static void row_start_handler()
{
    // Clear the interrupt request for the finished DMA channel
    dma_channel_acknowledge_irq1(row_start_chan);

    // Advance row addressing; reset and increment bit-plane if needed

#if defined(HUB75_MULTIPLEX_2_ROWS)
    // plane wise BCM (Binary Coded Modulation)
    if (++row_address >= (height >> 1))
    {
        row_address = 0;
        if (++bit_plane >= BIT_DEPTH)
        {
            bit_plane = 0;

            if (frame_count == 0)
            {
                frame_time_start = get_absolute_time();
            }
            else if (frame_count == FRAME_MEASURE_INTERVAL)
            {
                frame_freq_us = (uint32_t)absolute_time_diff_us(frame_time_start, get_absolute_time());
                frame_count = -1; // reset so it measures again next interval

                uint32_t freq = 1000000u * FRAME_MEASURE_INTERVAL / frame_freq_us;
                printf("Frame frequency: %u Hz\n", freq);
                frame_freq_us = 0; // clear until next measurement
            }
            frame_count++;

            if (swap_pending)
            {
                dma_buffer = (frame_buffer == frame_buffer1) ? frame_buffer2 : frame_buffer1;
                swap_pending = false;
            }
        }
        // Patch the PIO program to make it shift to the next bit plane
        hub75_data_rgb_set_shift(pio_config.data_pio, pio_config.sm_data, pio_config.data_prog_offs, bit_plane);
    }
#elif defined(HUB75_P3_1415_16S_64X64_S31)
    // plane wise BCM (Binary Coded Modulation)
    if (++row_address >= (height >> 2))
    {
        row_address = 0;
        if (++bit_plane >= BIT_DEPTH)
        {
            bit_plane = 0;

            if (swap_pending)
            {
                dma_buffer = (frame_buffer == frame_buffer1) ? frame_buffer2 : frame_buffer1;
                swap_pending = false;
            }
        }
        // Patch the PIO program to make it shift to the next bit plane
        hub75_data_rgb_set_shift(pio_config.data_pio, pio_config.sm_data, pio_config.data_prog_offs, bit_plane);
    }
#elif defined(HUB75_P10_3535_16X32_4S)
    // line wise BCM (Binary Coded Modulation)
    // calls hub75_data_rgb888_set_shift more often than plane wise BCM
    hub75_data_rgb_set_shift(pio_config.data_pio, pio_config.sm_data, pio_config.data_prog_offs, bit_plane);

    if (++bit_plane >= BIT_DEPTH)
    {
        bit_plane = 0;
        if (++row_address >= (height >> 2))
        {
            row_address = 0;

            if (swap_pending)
            {
                dma_buffer = (frame_buffer == frame_buffer1) ? frame_buffer2 : frame_buffer1;
                swap_pending = false;
            }
        }
    };
#endif

    // Build the three-word OEn record for the next row/bit-plane and point
    // oen_chan at it.  The PIO state machine consumes all three words in order:
    //   word 0 — row address
    //   word 1 — lit duration  (OEn asserted,   panel ON)
    //   word 2 — dark duration (OEn deasserted, panel OFF)
    // Using lit + dark instead of a single pulse width keeps the total period
    // constant, giving a brightness-independent frame rate.
    oen_data.row_address = row_address;            // 5-bit row select for the next row pair
    oen_data.lit_cycles = lit_cycles[bit_plane];   // ON  duration — BCM weighted, brightness scaled
    oen_data.dark_cycles = dark_cycles[bit_plane]; // OFF duration — remainder of full BCM period

    dma_channel_set_read_addr(oen_chan, &oen_data, false);
#if defined(HUB75_MULTIPLEX_2_ROWS)
    dma_channel_set_read_addr(pixel_chan, &dma_buffer[row_address * (width << 1)], true);
#elif defined(HUB75_P10_3535_16X32_4S) || defined(HUB75_P3_1415_16S_64X64_S31)
    dma_channel_set_read_addr(pixel_chan, &dma_buffer[row_address * (width << 2)], true);
#endif

    // Re-arm row_start_chan to catch the next RX FIFO push.
    dma_channel_start(row_start_chan);
}

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

    frame_buffer1 = new uint32_t[width * height](); // Allocate memory for frame buffer and zero-initialize
    frame_buffer2 = new uint32_t[width * height](); // Allocate memory for frame buffer and zero-initialize

#if defined(HUB75_MULTIPLEX_2_ROWS)
    offset = width * (height >> 1);
#elif defined(HUB75_P3_1415_16S_64X64_S31)
    offset = width * (height >> 2);
#endif

#if TEMPORAL_DITHERING != false
    init_accumulators(width * height);
#endif

    if (panel_type == PANEL_FM6126A)
    {
        FM6126A_setup();
    }
    else if (panel_type == PANEL_FM6124DJ)
    {
        FM6124DJ_setup();
    }
    else if (panel_type == PANEL_RUL6024)
    {
        RUL6024_setup();
    }

    configure_pio(inverted_stb);
    configure_dma_channels();
    setup_dma_transfers();
    setup_dma_irq();
    recompute_scaled_basis();
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
    frame_buffer = frame_buffer1;
    dma_buffer = frame_buffer1;
    swap_pending = false;

    oen_data.row_address = 0;
    oen_data.lit_cycles = lit_cycles[0];
    oen_data.dark_cycles = dark_cycles[0];

    // Start DMA channels

    // Shift row 0 synchronously so it is ready before first LATCH.
    dma_channel_set_read_addr(pixel_chan, &dma_buffer[0], true);
    dma_channel_wait_for_finish_blocking(pixel_chan);
    dma_channel_wait_for_finish_blocking(dummy_pixel_chan);

    // Arm consumer before starting row PIO.
    dma_channel_set_write_addr(row_start_chan, &row_start_data, true);

    // Trigger first oen_chan — row PIO starts, displays row 0,
    dma_channel_start(oen_chan);
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
            &hub75_data_rgb_program,
            &pio_config.data_pio,
            &pio_config.sm_data,
            &pio_config.data_prog_offs,
            DATA_BASE_PIN, DATA_N_PINS + 1, true)) // +1 for CLK
    {
        panic("Failed to claim PIO SM for hub75_data_rgb_program\n");
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
    wait_cycles = clock_get_hz(clk_sys) / 4000000;

    hub75_data_rgb_program_init(pio_config.data_pio, pio_config.sm_data, pio_config.data_prog_offs, DATA_BASE_PIN, CLK_PIN);

    if (inverted_stb)
        hub75_row_inverted_program_init(pio_config.row_pio, pio_config.sm_row, pio_config.row_prog_offs, ROWSEL_BASE_PIN, ROWSEL_N_PINS, STROBE_PIN, wait_cycles);
    else
        hub75_row_program_init(pio_config.row_pio, pio_config.sm_row, pio_config.row_prog_offs, ROWSEL_BASE_PIN, ROWSEL_N_PINS, STROBE_PIN, wait_cycles);
}

/**
 * @brief Configures and claims DMA channels for HUB75 control.
 *
 * This function assigns DMA channels to handle pixel data transfer,
 * dummy pixel data, output enable signal, and output enable completion.
 */
static void configure_dma_channels()
{
    pixel_chan = dma_claim_unused_channel(true);
    dummy_pixel_chan = dma_claim_unused_channel(true);
    oen_chan = dma_claim_unused_channel(true);
    row_start_chan = dma_claim_unused_channel(true);
}

/**
 * @brief Configures a DMA input channel for transferring data to a PIO state machine.
 *
 * This function sets up a DMA channel to transfer data from memory to a PIO
 * state machine. It configures transfer size, address incrementing, and DMA
 * chaining to ensure seamless operation.
 *
 * @param channel DMA channel number to configure.
 * @param transfer_count Number of data transfers per DMA transaction.
 * @param dma_size Data transfer size (8, 16, or 32-bit).
 * @param read_incr Whether the read address should increment after each transfer.
 * @param chain_to DMA channel to chain the transfer to, enabling automatic triggering.
 * @param pio PIO instance that will receive the transferred data.
 * @param sm State machine within the PIO instance that will process the data.
 */
static void dma_input_channel_setup(uint channel,
                                    uint transfer_count,
                                    enum dma_channel_transfer_size dma_size,
                                    bool read_incr,
                                    uint chain_to,
                                    PIO pio,
                                    uint sm)
{
    dma_channel_config conf = dma_channel_get_default_config(channel);
    channel_config_set_transfer_data_size(&conf, dma_size);
    channel_config_set_read_increment(&conf, read_incr);
    channel_config_set_write_increment(&conf, false);
    uint dreq = pio_get_dreq(pio, sm, true);
    channel_config_set_dreq(&conf, dreq);

    channel_config_set_high_priority(&conf, true);

    channel_config_set_chain_to(&conf, chain_to);

    dma_channel_configure(
        channel,                                   // Channel to be configured
        &conf,                                     // DMA configuration
        &pio->txf[sm],                             // Write address: PIO TX FIFO
        NULL,                                      // Read address: set later
        dma_encode_transfer_count(transfer_count), // Number of transfers per transaction
        false                                      // Do not start transfer immediately
    );
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
#if defined(HUB75_MULTIPLEX_2_ROWS)
    dma_input_channel_setup(pixel_chan, width << 1, DMA_SIZE_32, true, dummy_pixel_chan, pio_config.data_pio, pio_config.sm_data);
#elif defined(HUB75_P10_3535_16X32_4S) || defined(HUB75_P3_1415_16S_64X64_S31)
    dma_input_channel_setup(pixel_chan, width << 2, DMA_SIZE_32, true, dummy_pixel_chan, pio_config.data_pio, pio_config.sm_data);
#endif

    dma_input_channel_setup(dummy_pixel_chan, 2, DMA_SIZE_32, false, oen_chan, pio_config.data_pio, pio_config.sm_data);

    // start oen_chan when dummy_pixel_data for N+1 row has been completely loaded
    dma_input_channel_setup(oen_chan, 3, DMA_SIZE_32, true, oen_chan, pio_config.row_pio, pio_config.sm_row);

    pio_sm_set_clkdiv(pio_config.data_pio, pio_config.sm_data, std::max(SM_CLOCKDIV_FACTOR, 1.0f));
    pio_sm_set_clkdiv(pio_config.row_pio, pio_config.sm_row, std::max(SM_CLOCKDIV_FACTOR, 1.0f));

    dma_channel_set_read_addr(dummy_pixel_chan, dummy_pixel_data, false);
    dma_channel_set_read_addr(oen_chan, &oen_data, false);

    dma_channel_config row_start_config = dma_channel_get_default_config(row_start_chan);
    channel_config_set_transfer_data_size(&row_start_config, DMA_SIZE_32);
    channel_config_set_read_increment(&row_start_config, false);
    channel_config_set_write_increment(&row_start_config, false);
    channel_config_set_dreq(&row_start_config, pio_get_dreq(pio_config.row_pio, pio_config.sm_row, false));
    dma_channel_configure(row_start_chan, &row_start_config, &row_start_data, &pio_config.row_pio->rxf[pio_config.sm_row], dma_encode_transfer_count(1), false);
}

/**
 * @brief Sets up and enables the DMA interrupt handler.
 *
 * Registers the interrupt service routine (ISR) for the output enable finished DMA channel.
 * This is the channel that triggers the end of the output enable signal, which in turn
 * triggers the start of the next row's pixel data transfer.
 */
static void setup_dma_irq()
{
    irq_set_exclusive_handler(DMA_IRQ_1, row_start_handler);
    dma_channel_set_irq1_enabled(row_start_chan, true);
    irq_set_enabled(DMA_IRQ_1, true);
}

#if TEMPORAL_DITHERING != false
// Main temporal dithering: 8→12→10 bit
uint32_t temporal_dithering(size_t j, uint32_t pixel)
{
    // --- 1. Expand 8-bit RGB using LUT ---
    uint16_t r16 = lut[(pixel >> 16) & 0xFF];
    uint16_t g16 = lut[(pixel >> 8) & 0xFF];
    uint16_t b16 = lut[(pixel >> 0) & 0xFF];

    // --- 2. Add residue ---
    uint16_t new_r = (uint32_t)r16 + acc_r[j];
    uint16_t new_g = (uint32_t)g16 + acc_g[j];
    uint16_t new_b = (uint32_t)b16 + acc_b[j];

    // --- 3. Clamp to 12-bit maximum ---

    if (new_r > CLAMP_MAX)
        new_r = CLAMP_MAX;
    if (new_g > CLAMP_MAX)
        new_g = CLAMP_MAX;
    if (new_b > CLAMP_MAX)
        new_b = CLAMP_MAX;

    // --- 4. Quantize to 10-bit output and compute fractional error ---
    // Scale 12-bit → 10-bit (divide by 64)
    uint32_t out_r = new_r >> ACC_SHIFT;
    uint32_t out_g = new_g >> ACC_SHIFT;
    uint32_t out_b = new_b >> ACC_SHIFT;

    // Residual = remainder of division (fractional component)
    acc_r[j] = new_r & 0x3;
    acc_g[j] = new_g & 0x3;
    acc_b[j] = new_b & 0x3;

    // --- 5. Recombine into packed 0xRRGGBB10-bit-style integer ---
    return (out_b << (2 * BIT_DEPTH)) | (out_g << BIT_DEPTH) | out_r;
}

// Main temporal dithering: 8→12→10 bit
uint32_t temporal_dithering(size_t j, uint8_t r, uint8_t g, uint8_t b)
{
    // --- 1. Expand 8-bit RGB using LUT ---
    uint16_t b16 = lut[b];
    uint16_t g16 = lut[g];
    uint16_t r16 = lut[r];

    // --- 2. Add residue  ---
    uint16_t new_r = r16 + acc_r[j];
    uint16_t new_g = g16 + acc_g[j];
    uint16_t new_b = b16 + acc_b[j];

    // --- 3. Clamp to 16-bit maximum ---
    if (new_r > CLAMP_MAX)
        new_r = CLAMP_MAX;
    if (new_g > CLAMP_MAX)
        new_g = CLAMP_MAX;
    if (new_b > CLAMP_MAX)
        new_b = CLAMP_MAX;

    // --- 4. Quantize to 10-bit output and compute fractional error ---
    // Scale 16-bit → 10-bit (divide by 64)
    uint32_t out_r = new_r >> ACC_SHIFT;
    uint32_t out_g = new_g >> ACC_SHIFT;
    uint32_t out_b = new_b >> ACC_SHIFT;

    // Residual = remainder of division (fractional component)
    acc_r[j] = new_r & 0x3;
    acc_g[j] = new_g & 0x3;
    acc_b[j] = new_b & 0x3;

    // --- 5. Recombine into packed 0xRRGGBB10-bit-style integer ---
    return (out_r << (2 * BIT_DEPTH)) | (out_g << BIT_DEPTH) | out_b;
}
#else
// Helper: apply LUT and pack into 30-bit RGB (10 bits per channel)
static inline uint32_t pack_lut_rgb(uint32_t color, const uint16_t *lut)
{
    return (lut[(color & 0x0000ff)] << (2 * BIT_DEPTH)) |
           (lut[(color >> 8) & 0x0000ff] << BIT_DEPTH) |
           (lut[(color >> 16) & 0x0000ff]);
}

// Helper: apply LUT and pack into 30-bit RGB (10 bits per channel)
static inline uint32_t pack_lut_rgb_(uint32_t r, uint32_t g, uint32_t b, const uint16_t *lut)
{
    return lut[r] << (2 * BIT_DEPTH) | lut[g] << BIT_DEPTH | lut[b];
}
#endif

#if USE_PICO_GRAPHICS == true
/**
 * @brief Update frame_buffer from PicoGraphics source (RGB888 / packed 32-bit),
 *        using accumulator temporal dithering while preserving the LUT mapping.
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
    for (size_t fb_index = 0, j = 0; fb_index < pixels; fb_index += 2, ++j)
    {
        frame_buffer[fb_index] = LUT_MAPPING(fb_index, src[j]);
        frame_buffer[fb_index + 1] = LUT_MAPPING(fb_index + 1, src[j + offset]);
    }
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
    swap_pending = true;
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
    constexpr uint total_pixels = MATRIX_PANEL_WIDTH * MATRIX_PANEL_HEIGHT;
    const uint rgb_offset = offset * 3;
    for (size_t fb_index = 0, j = 0; fb_index < total_pixels; j += 3, fb_index += 2)
    {
        frame_buffer[fb_index] = LUT_MAPPING_RGB(fb_index, src[j], src[j + 1], src[j + 2]);
        frame_buffer[fb_index + 1] = LUT_MAPPING_RGB((fb_index + 1), src[rgb_offset + j], src[rgb_offset + j + 1], src[rgb_offset + j + 2]);
    }
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
    swap_pending = true;
    frame_buffer = (frame_buffer == frame_buffer1) ? frame_buffer2 : frame_buffer1;
    restore_interrupts(irq);
}