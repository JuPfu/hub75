#include <cstdlib>
#include <vector>

#include "pico/stdlib.h"

#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/sync.h"

#include "hub75.hpp"
#include "hub75.pio.h"

#include "rul6024.h"

// Deduced from https://jared.geek.nz/2013/02/linear-led-pwm/
// The CIE 1931 lightness formula is what actually describes how we perceive light.

#ifdef TEMPORAL_DITHERING
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

// Frame buffer for the HUB75 matrix - memory area where pixel data is stored
volatile __attribute__((aligned(4))) uint32_t *frame_buffer; ///< Interwoven image data for examples;

static __attribute__((aligned(2))) uint16_t *src_map;

// Utility function to claim a DMA channel and panic() if there are none left
static int claim_dma_channel(const char *channel_name);

static void configure_dma_channels();
static void configure_pio(bool);
static void setup_dma_transfers();
static void setup_dma_irq();

// Dummy pixel data emitted at the end of each row to ensure the last genuine pixels of a row are displayed - keep volatile!
static volatile uint32_t dummy_pixel_data[8] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
// Control data for the output enable signal - keep volatile!
static volatile uint32_t oen_finished_data = 0;

const bool clk_polarity = 1;
const bool stb_polarity = 1;
const bool oe_polarity = 0;

// Width and height of the HUB75 LED matrix
static uint width;
static uint height;
static uint offset;

// DMA channel numbers
int pixel_chan;
int dummy_pixel_chan;
int oen_chan;

// DMA channel that becomes active when output enable (OEn) has finished.
// This channel's interrupt handler restarts the pixel data DMA channel.
int oen_finished_chan;

// PIO configuration structure for state machine numbers and corresponding program offsets
typedef struct
{
    uint sm_data;
    PIO data_pio;
    uint data_prog_offs;
    uint sm_row;
    PIO row_pio;
    uint row_prog_offs;
} PioConfig;

static PioConfig pio_config;

// Variables for row addressing and bit plane selection
static volatile uint32_t row_address = 0;
static volatile uint32_t bit_plane = 0;
static volatile uint32_t row_in_bit_plane = 0;

// Derived constants
static const int ACC_SHIFT = (ACC_BITS - 10); // number of low bits preserved in accumulator

// Per-channel accumulators (allocated at runtime)
static std::vector<uint32_t> acc_r, acc_g, acc_b;

// Variables for brightness control
// Q format shift: Q16 gives 1.0 == (1 << 16) == 65536
#define BRIGHTNESS_FP_SHIFT 16u

// Brightness as fixed-point Q16 (volatile because it may be changed at runtime)
static volatile uint32_t brightness_fp = (1u << BRIGHTNESS_FP_SHIFT); // default == 1.0

// Precomputed scaled basis per bit plane to avoid calculating in ISR
static volatile uint32_t scaled_basis[BIT_DEPTH];

// Basis factor (coarse brightness)
static volatile uint32_t basis_factor = 6u;

inline __attribute__((always_inline)) uint32_t set_row_in_bit_plane(uint32_t row_address, uint32_t bit_plane)
{
    // scaled_basis[bit_plane] already includes brightness scaling.
    // left shift by 5 to form the OEn-length encoding.
    return row_address | (scaled_basis[bit_plane] << 5);
}

// Recompute scaled_basis[] using a temporary array and swap under IRQ protection.
// scaled_basis[b] = (basis_factor << b) * brightness_fp  >> BRIGHTNESS_FP_SHIFT
__attribute__((optimize("unroll-loops"))) static void recompute_scaled_basis()
{
    uint32_t tmp[BIT_DEPTH];

    for (int b = 0; b < BIT_DEPTH; ++b)
    {
        // use 64-bit intermediate to avoid overflow during multiply
        uint64_t base = (uint64_t)basis_factor << b;
        tmp[b] = (uint32_t)((base * (uint64_t)brightness_fp) >> BRIGHTNESS_FP_SHIFT);
    }

    // update scaled_basis atomically w.r.t. interrupts reading it
    uint32_t irq = save_and_disable_interrupts();
    for (int b = 0; b < BIT_DEPTH; ++b)
        scaled_basis[b] = tmp[b];
    restore_interrupts(irq);
}

/**
 * @brief Set the baseline brightness scaling factor for the panel.
 *
 * This acts as the coarse brightness control (default = 6u).
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
        // stable conversion to Q16
        brightness_fp = (uint32_t)(intensity * (float)(1u << BRIGHTNESS_FP_SHIFT) + 0.5f);
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


/**
 * @brief Interrupt handler for the Output Enable (OEn) finished event.
 *
 * This interrupt is triggered when the output enable DMA transaction is completed.
 * It updates row addressing and bit-plane selection for the next frame,
 * modifies the PIO state machine instruction, and restarts DMA transfers
 * for pixel data to ensure continuous frame updates.
 */
static void oen_finished_handler()
{
    // Clear the interrupt request for the finished DMA channel
    dma_hw->ints0 = 1u << oen_finished_chan;

    // Advance row addressing; reset and increment bit-plane if needed
#if defined(HUB75_MULTIPLEX_2_ROWS)
    // plane wise BCM (Binary Coded Modulation)
    if (++row_address >= (height >> 1))
    {
        row_address = 0;
        if (++bit_plane >= BIT_DEPTH)
        {
            bit_plane = 0;
        }
        // Patch the PIO program to make it shift to the next bit plane
        hub75_data_rgb888_set_shift(pio_config.data_pio, pio_config.sm_data, pio_config.data_prog_offs, bit_plane);
    }
#elif defined(HUB75_P3_1415_16S_64X64)
    // plane wise BCM (Binary Coded Modulation)
    if (++row_address >= (height >> 2))
    {
        row_address = 0;
        if (++bit_plane >= BIT_DEPTH)
        {
            bit_plane = 0;
        }
        // Patch the PIO program to make it shift to the next bit plane
        hub75_data_rgb888_set_shift(pio_config.data_pio, pio_config.sm_data, pio_config.data_prog_offs, bit_plane);
    }
#elif defined(HUB75_MULTIPLEX_4_ROWS)
    // line wise BCM (Binary Coded Modulation)
    // calls hub75_data_rgb888_set_shift more often than plane wise BCM
    hub75_data_rgb888_set_shift(pio_config.data_pio, pio_config.sm_data, pio_config.data_prog_offs, bit_plane);
    if (++bit_plane >= BIT_DEPTH)
    {
        bit_plane = 0;
        if (++row_address >= (height >> 2))
        {
            row_address = 0;
        }
    };
#endif

    // Compute address and length of OEn pulse for next row
    row_in_bit_plane = set_row_in_bit_plane(row_address, bit_plane);

    dma_channel_set_read_addr(oen_chan, &row_in_bit_plane, false);

    // Restart DMA channels for the next row's data transfer
    dma_channel_set_write_addr(oen_finished_chan, &oen_finished_data, true);
#if defined(HUB75_MULTIPLEX_2_ROWS)
    dma_channel_set_read_addr(pixel_chan, &frame_buffer[row_address * (width << 1)], true);
#elif defined(HUB75_MULTIPLEX_4_ROWS) || defined(HUB75_P3_1415_16S_64X64)
    dma_channel_set_read_addr(pixel_chan, &frame_buffer[row_address * (width << 2)], true);
#endif
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
    dma_channel_set_write_addr(oen_finished_chan, &oen_finished_data, true);
#if defined(HUB75_MULTIPLEX_2_ROWS)
    dma_channel_set_read_addr(pixel_chan, &frame_buffer[row_address * (width << 1)], true);
#elif defined(HUB75_MULTIPLEX_4_ROWS) || defined(HUB75_P3_1415_16S_64X64)
    dma_channel_set_read_addr(pixel_chan, &frame_buffer[row_address * (width << 2)], true);
#endif
}

void FM6126A_init_register()
{
    // Set up GPIO
    gpio_init(DATA_BASE_PIN);
    gpio_set_function(DATA_BASE_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(DATA_BASE_PIN, true);
    gpio_put(DATA_BASE_PIN, 0);
    gpio_init((DATA_BASE_PIN + 1));
    gpio_set_function((DATA_BASE_PIN + 1), GPIO_FUNC_SIO);
    gpio_set_dir((DATA_BASE_PIN + 1), true);
    gpio_put((DATA_BASE_PIN + 1), 0);
    gpio_init((DATA_BASE_PIN + 2));
    gpio_set_function((DATA_BASE_PIN + 2), GPIO_FUNC_SIO);
    gpio_set_dir((DATA_BASE_PIN + 2), true);
    gpio_put((DATA_BASE_PIN + 2), 0);

    gpio_init((DATA_BASE_PIN + 3));
    gpio_set_function((DATA_BASE_PIN + 3), GPIO_FUNC_SIO);
    gpio_set_dir((DATA_BASE_PIN + 3), true);
    gpio_put((DATA_BASE_PIN + 3), 0);
    gpio_init((DATA_BASE_PIN + 4));
    gpio_set_function((DATA_BASE_PIN + 4), GPIO_FUNC_SIO);
    gpio_set_dir((DATA_BASE_PIN + 4), true);
    gpio_put((DATA_BASE_PIN + 4), 0);
    gpio_init((DATA_BASE_PIN + 5));
    gpio_set_function((DATA_BASE_PIN + 5), GPIO_FUNC_SIO);
    gpio_set_dir((DATA_BASE_PIN + 5), true);
    gpio_put((DATA_BASE_PIN + 5), 0);

    gpio_init(ROWSEL_BASE_PIN);
    gpio_set_function(ROWSEL_BASE_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(ROWSEL_BASE_PIN, true);
    gpio_put(ROWSEL_BASE_PIN, 0);
    gpio_init((ROWSEL_BASE_PIN + 1));
    gpio_set_function((ROWSEL_BASE_PIN + 1), GPIO_FUNC_SIO);
    gpio_set_dir((ROWSEL_BASE_PIN + 1), true);
    gpio_put((ROWSEL_BASE_PIN + 1), 0);
    gpio_init((ROWSEL_BASE_PIN + 2));
    gpio_set_function((ROWSEL_BASE_PIN + 2), GPIO_FUNC_SIO);
    gpio_set_dir((ROWSEL_BASE_PIN + 2), true);
    gpio_put((ROWSEL_BASE_PIN + 2), 0);
    gpio_init((ROWSEL_BASE_PIN + 3));
    gpio_set_function((ROWSEL_BASE_PIN + 3), GPIO_FUNC_SIO);
    gpio_set_dir((ROWSEL_BASE_PIN + 3), true);
    gpio_put((ROWSEL_BASE_PIN + 3), 0);
    gpio_init((ROWSEL_BASE_PIN + 4));
    gpio_set_function((ROWSEL_BASE_PIN + 4), GPIO_FUNC_SIO);
    gpio_set_dir((ROWSEL_BASE_PIN + 4), true);
    gpio_put((ROWSEL_BASE_PIN + 4), 0);

    gpio_init(CLK_PIN);
    gpio_set_function(CLK_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(CLK_PIN, true);
    gpio_put(CLK_PIN, !clk_polarity);
    gpio_init(STROBE_PIN);
    gpio_set_function(STROBE_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(STROBE_PIN, true);
    gpio_put(CLK_PIN, !stb_polarity);
    gpio_init(OEN_PIN);
    gpio_set_function(OEN_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(OEN_PIN, true);
    gpio_put(CLK_PIN, !oe_polarity);
}

void FM6126A_write_register(uint16_t value, uint8_t position)
{
    gpio_put(OEN_PIN, HIGH);
    gpio_put(CLK_PIN, LOW);
    gpio_put(STROBE_PIN, LOW);

    sleep_ms(10);

    uint8_t threshold = width - position;
    for (auto i = 0u; i < width; i++)
    {
        auto j = i % 16;
        bool b = value & (1 << j);

        gpio_put(DATA_BASE_PIN, b);
        gpio_put((DATA_BASE_PIN + 1), b);
        gpio_put((DATA_BASE_PIN + 2), b);
        gpio_put((DATA_BASE_PIN + 3), b);
        gpio_put((DATA_BASE_PIN + 4), b);
        gpio_put((DATA_BASE_PIN + 5), b);

        // Assert strobe/latch if i > threshold
        // This somehow indicates to the FM6126A which register we want to write :|
        gpio_put(STROBE_PIN, i > threshold);
        gpio_put(CLK_PIN, HIGH);
        sleep_ms(10);
        gpio_put(CLK_PIN, LOW);
    }
    gpio_put(OEN_PIN, LOW);
}

/**
 * @brief Generate initialisation sequence for FM6126A based led matrix panels.
 *
 * First initialise all GPIOs connected to the led matrix panel.
 * Second send the initialisation sequence to the FM6126A based led matrix panel.
 * The source code is based on Pimoronis Hub75 driver, see https://github.com/pimoroni/pimoroni-pico/blob/main/drivers/hub75/hub75.cpp
 *
 */
void FM6126A_setup()
{
    FM6126A_init_register();

    // Ridiculous register write nonsense for the FM6126A-based 64x64 matrix
    FM6126A_write_register(0b1111111111111110, 12);
    FM6126A_write_register(0b0000010000000000, 13);

    // FM6126A_write_register(0b1111111111000000, 12);
    // FM6126A_write_register(0b0000000001000000, 13);

    // FM6126A_write_register(WREG1, 12);
    // FM6126A_write_register(WREG2, 13);
}

// void FM6126A_setup()
// {
//     PIO pio = pio0;
//     uint sm = 0;

//     FM6126A panel(/*pin_r1=*/DATA_BASE_PIN, /*pin_clk=*/CLK_PIN, /*pin_lat=*/STROBE_PIN, /*pin_oe=*/OEN_PIN);

//     panel.initialize();
// }

void RUL6024_init_register()
{
    // Set up GPIO
    for (auto i = 0; i < DATA_N_PINS; i++)
    {
        gpio_init(DATA_BASE_PIN + i);
        gpio_set_function(DATA_BASE_PIN + i, GPIO_FUNC_SIO);
        gpio_set_dir(DATA_BASE_PIN + i, true);
        gpio_put(DATA_BASE_PIN + i, 0);
    }

    for (auto i = 0; i < ROWSEL_N_PINS; i++)
    {
        gpio_init(ROWSEL_BASE_PIN + i);
        gpio_set_function(ROWSEL_BASE_PIN + i, GPIO_FUNC_SIO);
        gpio_set_dir(ROWSEL_BASE_PIN + i, true);
        gpio_put(ROWSEL_BASE_PIN + i, 0);
    }

    gpio_init(CLK_PIN);
    gpio_set_function(CLK_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(CLK_PIN, true);
    gpio_put(CLK_PIN, LOW);

    gpio_init(STROBE_PIN);
    gpio_set_function(STROBE_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(STROBE_PIN, true);
    gpio_put(CLK_PIN, LOW);

    gpio_init(OEN_PIN);
    gpio_set_function(OEN_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(OEN_PIN, true);
    gpio_put(OEN_PIN, LOW);
}

void RUL6024_write_register(uint16_t value, uint8_t position)
{
    gpio_put(STROBE_PIN, LOW);
    sleep_us(10);

    uint8_t threshold = width - position;
    for (auto i = 0u; i < width; i++)
    {
        auto j = i % 16;
        bool b = value & (1 << j);

        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        gpio_put(DATA_BASE_PIN, b);
        gpio_put((DATA_BASE_PIN + 1), b);
        gpio_put((DATA_BASE_PIN + 2), b);
        gpio_put((DATA_BASE_PIN + 3), b);
        gpio_put((DATA_BASE_PIN + 4), b);
        gpio_put((DATA_BASE_PIN + 5), b);

        // Assert strobe/latch if i > threshold
        // This somehow indicates to the FM6126A which register we want to write :|
        gpio_put(STROBE_PIN, i > threshold);
        sleep_us(10);
        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);
    }
}

void RUL6024_write_command(uint8_t command)
{
    // The chip contains a simple 16-bit shift register. The grayscale value and configuration
    // value are latched into the shift register (the data transmitted to the chip first is the high bit
    // of the register). The control command is parsed by counting the length of the LE signal.
    // Different LE lengths represent different commands. For example, a LE signal with a
    // length of 3 represents the "Data_Latch" command, which is used to control the shift
    // register to latch the value and send the 16-bit data in the shift register to the
    // output channel. The following table lists all the commands and their meanings.
    //
    // Command Name    LE length     Command Description
    //
    // RESET_OEN       1 & 2         The reset signal of the time-sharing display function is 1 LE width first, followed by 2 LE widths.
    // DATA_LATCH      3             Latch 16 bit data and send it to output channel
    // Reserved        4 to 10       Reserved
    // WR_REG1         11            Write configuration register 1
    // WR_REG2         12            Write configuration register 2

    switch (command)
    {
    case CMD_RESET_OEN:
        printf("DO RESET_OEN COMMAND\n");
        // The reset signal of the time-sharing display function is 1 LE width first, followed by 2 LE widths.

        gpio_put(OEN_PIN, HIGH);
        sleep_us(10);
        gpio_put(CLK_PIN, LOW);
        gpio_put(STROBE_PIN, LOW); // clk    --_--
        sleep_us(10);              // LE     _____
                                   // OE     ---__
        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);

        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        gpio_put(STROBE_PIN, HIGH);
        sleep_us(10);
        // gpio_put(OEN_PIN, LOW);
        // sleep_us(10);

        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);

        gpio_put(STROBE_PIN, LOW);
        gpio_put(CLK_PIN, LOW);
        sleep_us(10);

        // gpio_put(OEN_PIN, HIGH);
        // sleep_us(10);

        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);
        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        gpio_put(OEN_PIN, LOW);
        sleep_us(10);

        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);

        gpio_put(CLK_PIN, LOW);
        gpio_put(STROBE_PIN, HIGH);
        sleep_us(10);
        gpio_put(OEN_PIN, HIGH);

        // LE set to high for 2 clock cycle
        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);
        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);
        gpio_put(STROBE_PIN, LOW);
        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        break;
    case CMD_DATA_LATCH:
        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);
        gpio_put(STROBE_PIN, HIGH);
        sleep_us(10);
        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);
        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);
        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        gpio_put(CLK_PIN, HIGH);
        sleep_us(10);
        gpio_put(CLK_PIN, LOW);
        sleep_us(10);
        gpio_put(STROBE_PIN, LOW);
        sleep_us(10);
        gpio_put(OEN_PIN, LOW);
        break;
    case CMD_WREG1:
        gpio_put(CLK_PIN, LOW);
        gpio_put(STROBE_PIN, LOW);
        gpio_put(OEN_PIN, HIGH);
        sleep_us(10);

        for (auto i = 0; i <= CMD_WREG1; i++)
        {
            gpio_put(CLK_PIN, HIGH);
            sleep_us(10);
            if (i == 0)
            {
                gpio_put(STROBE_PIN, HIGH);
                sleep_us(10);
            }
            gpio_put(CLK_PIN, LOW);
            sleep_us(10);
        }

        // FM6126A_write_register(WREG1, 11);
        RUL6024_write_register(WREG1, 12);

        gpio_put(OEN_PIN, LOW);
        sleep_us(10);

        break;
    case CMD_WREG2:
        gpio_put(OEN_PIN, HIGH);
        gpio_put(CLK_PIN, LOW);
        gpio_put(STROBE_PIN, LOW);
        sleep_us(10);

        for (auto i = 0; i <= CMD_WREG2; i++)
        {
            gpio_put(CLK_PIN, HIGH);
            sleep_us(10);
            if (i == 0)
            {
                gpio_put(STROBE_PIN, HIGH);
                sleep_us(10);
            }
            gpio_put(CLK_PIN, LOW);
            sleep_us(10);
        }

        // FM6126A_write_register(WREG2, 12);
        RUL6024_write_register(WREG2, 12);

        gpio_put(OEN_PIN, LOW);
        sleep_us(10);
        break;
    }
}

void RUL6024_setup()
{
    RUL6024_init_register();

    RUL6024_write_command(CMD_WREG1);
    RUL6024_write_command(CMD_WREG2);
    // RESET_OEN is required after writing WREG2
    RUL6024_write_command(CMD_RESET_OEN);
    // RUL6024_write_command(CMD_DATA_LATCH);
}

void setup_map(uint16_t *src_map)
{
    const int total_pixels = width * height >> 1;
    const int four_rows_offset = width * 4;

    for (int j = 0, line = 0, counter = 0; j < total_pixels; ++j)
    {
        if ((j & 8) == 0)
            src_map[j] = j - (line << 3);
        else
            src_map[j] = j - ((line + 1) << 3) + four_rows_offset;

        if (++counter >= 16)
        {
            counter = 0;
            line++;
        }
    }
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
void create_hub75_driver(uint w, uint h, PanelType panel_type = PANEL_GENERIC, bool inverted_stb = false)
{
    width = w;
    height = h;

    frame_buffer = new uint32_t[width * height](); // Allocate memory for frame buffer and zero-initialize

#if defined(HUB75_MULTIPLEX_2_ROWS)
    offset = width * (height >> 1);
#elif defined(HUB75_P3_1415_16S_64X64)
    offset = width * (height >> 2);
#endif

#if defined(HUB75_MULTIPLEX_4_ROWS)
    src_map = new uint16_t[width * height >> 1](); // Precomputed index lookup
    setup_map(src_map);
#endif

#ifdef TEMPORAL_DITHERING
    init_accumulators(width * height);
#endif

    if (panel_type == PANEL_FM6126A)
    {
        FM6126A_setup();
    }

    configure_pio(inverted_stb);
    configure_dma_channels();
    setup_dma_transfers();
    setup_dma_irq();
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
    if (!pio_claim_free_sm_and_add_program(&hub75_data_rgb888_program, &pio_config.data_pio, &pio_config.sm_data, &pio_config.data_prog_offs))
    {
        fprintf(stderr, "Failed to claim PIO state machine for hub75_data_rgb888_program\n");
    }

    if (inverted_stb)
    {
        if (!pio_claim_free_sm_and_add_program(&hub75_row_inverted_program, &pio_config.row_pio, &pio_config.sm_row, &pio_config.row_prog_offs))
        {
            fprintf(stderr, "Failed to claim PIO state machine for hub75_row_inverted_program\n");
        }
    }
    else
    {
        if (!pio_claim_free_sm_and_add_program(&hub75_row_program, &pio_config.row_pio, &pio_config.sm_row, &pio_config.row_prog_offs))
        {
            fprintf(stderr, "Failed to claim PIO state machine for hub75_row_program\n");
        }
    }

    hub75_data_rgb888_program_init(pio_config.data_pio, pio_config.sm_data, pio_config.data_prog_offs, DATA_BASE_PIN, CLK_PIN);
    hub75_row_program_init(pio_config.row_pio, pio_config.sm_row, pio_config.row_prog_offs, ROWSEL_BASE_PIN, ROWSEL_N_PINS, STROBE_PIN);
}

/**
 * @brief Configures and claims DMA channels for HUB75 control.
 *
 * This function assigns DMA channels to handle pixel data transfer,
 * dummy pixel data, output enable signal, and output enable completion.
 * If a DMA channel cannot be claimed, the function prints an error message and exits.
 */
static void configure_dma_channels()
{
    pixel_chan = claim_dma_channel("pixel channel");
    dummy_pixel_chan = claim_dma_channel("dummy pixel channel");
    oen_chan = claim_dma_channel("output enable channel");
    oen_finished_chan = claim_dma_channel("output enable has finished channel");
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
    channel_config_set_dreq(&conf, pio_get_dreq(pio, sm, true));

    channel_config_set_chain_to(&conf, chain_to);

    dma_channel_configure(
        channel,        // Channel to be configured
        &conf,          // DMA configuration
        &pio->txf[sm],  // Write address: PIO TX FIFO
        NULL,           // Read address: set later
        transfer_count, // Number of transfers per transaction
        false           // Do not start transfer immediately
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
#elif defined(HUB75_MULTIPLEX_4_ROWS) || defined(HUB75_P3_1415_16S_64X64)
    dma_input_channel_setup(pixel_chan, width << 2, DMA_SIZE_32, true, dummy_pixel_chan, pio_config.data_pio, pio_config.sm_data);
#endif
    dma_input_channel_setup(dummy_pixel_chan, 8, DMA_SIZE_32, false, oen_chan, pio_config.data_pio, pio_config.sm_data);
    dma_input_channel_setup(oen_chan, 1, DMA_SIZE_32, true, oen_chan, pio_config.row_pio, pio_config.sm_row);

    dma_channel_set_read_addr(dummy_pixel_chan, dummy_pixel_data, false);

    row_in_bit_plane = set_row_in_bit_plane(row_address, bit_plane);
    dma_channel_set_read_addr(oen_chan, &row_in_bit_plane, false);

    dma_channel_config oen_finished_config = dma_channel_get_default_config(oen_finished_chan);
    channel_config_set_transfer_data_size(&oen_finished_config, DMA_SIZE_32);
    channel_config_set_read_increment(&oen_finished_config, false);
    channel_config_set_write_increment(&oen_finished_config, false);
    channel_config_set_dreq(&oen_finished_config, pio_get_dreq(pio_config.row_pio, pio_config.sm_row, false));
    dma_channel_configure(oen_finished_chan, &oen_finished_config, &oen_finished_data, &pio_config.row_pio->rxf[pio_config.sm_row], 1, false);
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
    irq_set_exclusive_handler(DMA_IRQ_0, oen_finished_handler);
    dma_channel_set_irq0_enabled(oen_finished_chan, true);
    irq_set_enabled(DMA_IRQ_0, true);
}

/**
 * @brief Claims an available DMA channel.
 *
 * Attempts to claim an unused DMA channel. If no channels are available,
 * prints an error message and exits the program.
 *
 * @param channel_name A descriptive name for the channel, used in error messages.
 * @return The claimed DMA channel number.
 */
static inline int claim_dma_channel(const char *channel_name)
{
    int dma_channel = dma_claim_unused_channel(true);
    if (dma_channel < 0)
    {
        fprintf(stderr, "Failed to claim DMA channel for %s\n", channel_name);
        exit(EXIT_FAILURE); // Stop execution
    }
    return dma_channel;
}

#ifdef TEMPORAL_DITHERING
// Main temporal dithering: 8→12→10 bit
uint32_t temporal_dithering(size_t j, uint32_t pixel)
{
    // --- 1. Expand 8-bit RGB using LUT ---
    uint32_t r16 = lut[(pixel >> 16) & 0xFF];
    uint32_t g16 = lut[(pixel >> 8) & 0xFF];
    uint32_t b16 = lut[(pixel >> 0) & 0xFF];

    // --- 2. Add residue ---
    uint32_t new_r = (uint32_t)r16 + acc_r[j];
    uint32_t new_g = (uint32_t)g16 + acc_g[j];
    uint32_t new_b = (uint32_t)b16 + acc_b[j];

    // --- 3. Clamp to 12-bit maximum ---
    if (new_r > 4095)
        new_r = 4095;
    if (new_g > 4095)
        new_g = 4095;
    if (new_b > 4095)
        new_b = 4095;

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
    return (out_b << 20) | (out_g << 10) | out_r;
}

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
    if (graphics->pen_type == PicoGraphics::PEN_RGB888)
    {
        __attribute__((aligned(4))) uint32_t const *src = static_cast<uint32_t const *>(graphics->frame_buffer);

#ifdef HUB75_MULTIPLEX_2_ROWS
        const size_t pixels = width * height;
        for (size_t fb_index = 0, j = 0; fb_index < pixels; fb_index += 2, ++j)
        {
            frame_buffer[fb_index] = temporal_dithering(j, src[j]);
            frame_buffer[fb_index + 1] = temporal_dithering(j + offset, src[j + offset]);
        }
#elif defined HUB75_MULTIPLEX_4_ROWS
        // For four-rows-lit multiplexing we step by 4 and use offsets 0, offset, 2*offset, 3*offset
        int eight_rows_offset = width * 8;
        int total_pixels = width * height >> 1;

        for (int j = 0, fb_index = 0; j < total_pixels; ++j, fb_index += 2)
        {
            uint32_t index = src_map[j];
            frame_buffer[fb_index] = temporal_dithering(index, src[index]);
            frame_buffer[fb_index + 1] = temporal_dithering(index + eight_rows_offset, src[index + eight_rows_offset]);
        }
#elif defined HUB75_P3_1415_16S_64X64
        const uint total_pixels = width * height;

        const uint quarter = total_pixels >> 2;

        uint quarter1 = 0 * quarter;
        uint quarter2 = 1 * quarter;
        uint quarter3 = 2 * quarter;
        uint quarter4 = 3 * quarter;

        for (uint i = 0, line = 0; i < total_pixels;)
        {
            if (!(line & 1)) // even src lines
            {
                for (uint j = 0; j < width; j++)
                {
                    frame_buffer[i++] = temporal_dithering(quarter2, src[quarter2]); // second quarter
                    frame_buffer[i++] = temporal_dithering(quarter4, src[quarter4]); // fourth quarter
                    quarter2++;
                    quarter4++;
                }
            }
            else // odd src lines
            {
                for (uint j = 0; j < width; j++)
                {
                    frame_buffer[i++] = temporal_dithering(quarter1, src[quarter1]); // first quarter
                    frame_buffer[i++] = temporal_dithering(quarter3, src[quarter3]); // third quarter
                    quarter1++;
                    quarter3++;
                }
            }
            line++;
        }
#endif
    }
}
#elif not defined TEMPORAL_DITHERING

// Helper: apply LUT and pack into 30-bit RGB (10 bits per channel)
static inline uint32_t pack_lut_rgb(uint32_t color, const uint16_t *lut)
{
    return (lut[(color & 0x0000ff)] << 20) |
           (lut[(color >> 8) & 0x0000ff] << 10) |
           (lut[(color >> 16) & 0x0000ff]);
}

// Helper: apply LUT and pack into 30-bit RGB (10 bits per channel)
static inline uint32_t pack_lut_bgr(uint32_t b, uint32_t g, uint32_t r, const uint16_t *lut)
{
    return lut[r] << 20 | lut[g] << 10 | lut[b];
}

/**
 * @brief Updates the frame buffer with pixel data from the source array.
 *
 * This function takes a source array of pixel data and updates the frame buffer
 * with interleaved pixel values. The pixel values are gamma-corrected to 10 bits using a lookup table.
 *
 * @param src Pointer to the source pixel data array (RGB888 format).
 */
__attribute__((optimize("unroll-loops"))) void update(
    PicoGraphics const *graphics // Graphics object to be updated - RGB888 format, this is 24-bits (8 bits per color channel) in a uint32_t array
)
{
    if (graphics->pen_type == PicoGraphics::PEN_RGB888)
    {
        uint32_t const *src = static_cast<uint32_t const *>(graphics->frame_buffer);

        // Ramping up color resolution from 8 to 10 bits via CIE luminance respectively gamma table look-up.
        // Interweave pixels from intermediate buffer into target image to fit the format expected by Hub75 LED panel.

#if defined(HUB75_MULTIPLEX_2_ROWS)
        for (int i = 0, j = 0; i < width * height; i += 2, j++)
        {
            frame_buffer[i] = lut[(src[j] & 0x0000ff) >> 0] << 20 | lut[(src[j] & 0x00ff00) >> 8] << 10 | lut[(src[j] & 0xff0000) >> 16];
            frame_buffer[i + 1] = lut[(src[j + offset] & 0x0000ff) >> 0] << 20 | lut[(src[j + offset] & 0x00ff00) >> 8] << 10 | lut[(src[j + offset] & 0xff0000) >> 16];
        }
#elif defined HUB75_MULTIPLEX_4_ROWS
        const int eight_rows_offset = 8 * width;
        const int total_pixels = (width * height) >> 1;

        for (int j = 0, fb_index = 0; j < total_pixels; ++j, fb_index += 2)
        {
            uint32_t index = src_map[j];
            frame_buffer[fb_index] = pack_lut_rgb(src[index], lut);
            frame_buffer[fb_index + 1] = pack_lut_rgb(src[index + eight_rows_offset], lut);
        }
#elif defined HUB75_P3_1415_16S_64X64
        const uint total_pixels = width * height;

        const uint quarter = total_pixels >> 2;

        uint quarter1 = 0 * quarter;
        uint quarter2 = 1 * quarter;
        uint quarter3 = 2 * quarter;
        uint quarter4 = 3 * quarter;

        for (uint i = 0, line = 0; i < total_pixels;)
        {
            if (!(line & 1)) // even src lines
            {
                for (uint j = 0; j < width; j++)
                {
                    frame_buffer[i++] = pack_lut_rgb(src[quarter2++], lut); // second quarter
                    frame_buffer[i++] = pack_lut_rgb(src[quarter4++], lut); // fourth quarter
                }
            }
            else // odd src lines
            {
                for (uint j = 0; j < width; j++)
                {
                    frame_buffer[i++] = pack_lut_rgb(src[quarter1++], lut); // first quarter
                    frame_buffer[i++] = pack_lut_rgb(src[quarter3++], lut); // third quarter
                }
            }
            line++;
        }
#endif
    }
}
#endif

#ifdef TEMPORAL_DITHERING
// Main temporal dithering: 8→12→10 bit
uint32_t temporal_dithering(size_t j, uint8_t r, uint8_t g, uint8_t b)
{
    // --- 1. Expand 8-bit RGB using LUT ---
    uint32_t b16 = lut[b];
    uint32_t g16 = lut[g];
    uint32_t r16 = lut[r];

    // --- 2. Add residue  ---
    uint32_t new_r = r16 + acc_r[j];
    uint32_t new_g = g16 + acc_g[j];
    uint32_t new_b = b16 + acc_b[j];

    // --- 3. Clamp to 16-bit maximum ---
    if (new_r > 4095)
        new_r = 4095;
    if (new_g > 4095)
        new_g = 4095;
    if (new_b > 4095)
        new_b = 4095;

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
    return (out_r << 20) | (out_g << 10) | out_b;
}

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
    const size_t pixels = width * height;
    const uint rgb_offset = offset * 3;
#ifdef HUB75_MULTIPLEX_2_ROWS
    for (size_t i = 0, j = 0; i < pixels; j += 3, i += 2)
    {
        frame_buffer[i] = temporal_dithering(i, src[j], src[j + 1], src[j + 2]);
        frame_buffer[i + 1] = temporal_dithering(i, src[rgb_offset + j], src[rgb_offset + j + 1], src[rgb_offset + j + 2]);
    }
#elif defined HUB75_MULTIPLEX_4_ROWS
    const int eight_rows_offset = 8 * width * 3;
    const int total_pixels = (width * height) >> 1;

    for (int j = 0, fb_index = 0; j < total_pixels; ++j, fb_index += 2)
    {
        uint32_t index = src_map[j];
        frame_buffer[fb_index] = temporal_dithering(index, src[index * 3], src[index * 3 + 1], src[index * 3 + 2]);
        frame_buffer[fb_index + 1] = temporal_dithering(index, src[index * 3 + eight_rows_offset], src[index * 3 + 1 + eight_rows_offset], src[index * 3 + 2 + eight_rows_offset]);
    }
#elif defined HUB75_P3_1415_16S_64X64
    const uint total_pixels = width * height;

    const uint quarter = (total_pixels >> 2) * 3;

    uint quarter1 = 0 * quarter;
    uint quarter2 = 1 * quarter;
    uint quarter3 = 2 * quarter;
    uint quarter4 = 3 * quarter;

    for (uint i = 0, line = 0; i < total_pixels;)
    {
        if (!(line & 1)) // even src lines
        {
            for (uint j = 0; j < width; j++)
            {
                frame_buffer[i++] = temporal_dithering(quarter2, src[quarter2], src[quarter2 + 1], src[quarter2 + 2]); // second quarter
                frame_buffer[i++] = temporal_dithering(quarter4, src[quarter4], src[quarter4 + 1], src[quarter4 + 2]); // fourth quarter
                quarter2 += 3;
                quarter4 += 3;
            }
        }
        else // odd src lines
        {
            for (uint j = 0; j < width; j++)
            {
                frame_buffer[i++] = temporal_dithering(quarter1, src[quarter1], src[quarter1 + 1], src[quarter1 + 2]); // first quarter
                frame_buffer[i++] = temporal_dithering(quarter3, src[quarter3], src[quarter3 + 1], src[quarter3 + 2]); // third quarter
                quarter1 += 3;
                quarter3 += 3;
            }
        }
        line++;
    }
#endif
}

#elif not defined TEMPORAL_DITHERING
/**
 * @brief Updates the frame buffer with pixel data from the source array.
 *
 * This function takes a source array of pixel data and updates the frame buffer
 * with interleaved pixel values. The pixel values are gamma-corrected to 10 bits using a lookup table.
 *
 * @param src Pointer to the source pixel data array (BGR888 format).
 */
__attribute__((optimize("unroll-loops"))) void update_bgr(const uint8_t *src)
{
    // Ramping up color resolution from 8 to 10 bits via CIE luminance respectively gamma table look-up.
    // Interweave pixels as required by Hub75 LED panel matrix

#ifdef HUB75_MULTIPLEX_2_ROWS
    uint rgb_offset = offset * 3;
    for (int j = 0, k = 0; j < width * height; j += 2, k += 3)
    {
        frame_buffer[j] = lut[src[k]] << 20 | lut[src[k + 1]] << 10 | lut[src[k + 2]];
        frame_buffer[j + 1] = lut[src[rgb_offset + k]] << 20 | lut[src[rgb_offset + k + 1]] << 10 | lut[src[rgb_offset + k + 2]];
    }
#elif defined HUB75_MULTIPLEX_4_ROWS
    const int eight_rows_offset = 8 * width;
    const int total_pixels = (width * height) >> 1;

    for (int j = 0, k = 0; j < total_pixels; j += 1, k += 2)
    {
        uint32_t index = src_map[j];
        frame_buffer[k] = (lut[src[index * 3 + 2]] << 20) | (lut[src[index * 3 + 1]] << 10) | (lut[src[index * 3 + 0]]);
        frame_buffer[k + 1] = (lut[src[(index + eight_rows_offset) * 3 + 2]] << 20) | (lut[src[(index + eight_rows_offset) * 3 + 1]] << 10) | (lut[src[(index + eight_rows_offset) * 3 + 0]]);
    }
#elif defined HUB75_P3_1415_16S_64X64
    const uint total_pixels = width * height;

    const uint quarter = (total_pixels >> 2) * 3;

    uint quarter1 = 0 * quarter;
    uint quarter2 = 1 * quarter;
    uint quarter3 = 2 * quarter;
    uint quarter4 = 3 * quarter;

    for (uint i = 0, line = 0; i < total_pixels;)
    {
        if (!(line & 1)) // even src lines
        {
            for (uint j = 0; j < width; j++)
            {
                frame_buffer[i++] = lut[src[quarter2 + 0]] << 20 | lut[src[quarter2 + 1]] << 10 | lut[src[quarter2 + 2]]; // second quarter
                frame_buffer[i++] = lut[src[quarter4 + 0]] << 20 | lut[src[quarter4 + 1]] << 10 | lut[src[quarter4 + 2]]; // fourth quarter
                quarter2 += 3;
                quarter4 += 3;
            }
        }
        else // odd src lines
        {
            for (uint j = 0; j < width; j++)
            {
                frame_buffer[i++] = lut[src[quarter1 + 0]] << 20 | lut[src[quarter1 + 1]] << 10 | lut[src[quarter1 + 2]]; // first quarter
                frame_buffer[i++] = lut[src[quarter3 + 0]] << 20 | lut[src[quarter3 + 1]] << 10 | lut[src[quarter3 + 2]]; // third quarter
                quarter1 += 3;
                quarter3 += 3;
            }
        }
        line++;
    }
#endif
}
#endif
