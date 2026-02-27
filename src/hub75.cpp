#include <cstdlib>
#include <vector>
#include <memory>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/sync.h"

#include "hub75.hpp"
#include "hub75.pio.h"

#include "rul6024.h"
#include "fm6126a.h"

// Deduced from https://jared.geek.nz/2013/02/linear-led-pwm/
// The CIE 1931 lightness formula is what actually describes how we perceive light.

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

// Frame buffer for the HUB75 matrix - memory area where pixel data is stored
std::unique_ptr<uint32_t[]> frame_buffer; ///< Interwoven image data for examples;

// Utility function to claim a DMA channel and panic() if there are none left
static int claim_dma_channel(const char *channel_name);

static void configure_dma_channels();
static void configure_pio(bool);
static void setup_dma_transfers();
static void setup_dma_irq();

// Dummy pixel data emitted at the end of each row to ensure the last genuine pixels of a row are displayed - keep volatile!
static uint32_t dummy_pixel_data[8] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
// Control data for the output enable signal
static uint32_t oen_finished_data = 0;

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
static uint32_t row_in_bit_plane = 0;

// Derived constants
static const int ACC_SHIFT = (ACC_BITS - 10); // number of low bits preserved in accumulator

// Per-channel accumulators (allocated at runtime)
static std::vector<uint16_t> acc_r, acc_g, acc_b;

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
    dma_channel_acknowledge_irq1(oen_finished_chan);

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
#elif defined(HUB75_P3_1415_16S_64X64_S31)
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
#elif defined(HUB75_P10_3535_16X32_4S)
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
#elif defined(HUB75_P10_3535_16X32_4S) || defined(HUB75_P3_1415_16S_64X64_S31)
    dma_channel_set_read_addr(pixel_chan, &frame_buffer[row_address * (width << 2)], true);
#endif
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

    frame_buffer = std::make_unique<uint32_t[]>(width * height); // Allocate memory for frame buffer and zero-initialize

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
 * @brief Secondary core entry point - creates and starts driver for HUB75 rgb matrix.
 */
void core1_entry()
{
    create_hub75_driver(MATRIX_PANEL_WIDTH, MATRIX_PANEL_HEIGHT, PANEL_TYPE, INVERTED_STB);

    dma_channel_set_write_addr(oen_finished_chan, &oen_finished_data, true);
#if defined(HUB75_MULTIPLEX_2_ROWS)
    dma_channel_set_read_addr(pixel_chan, &frame_buffer[row_address * (width << 1)], true);
#elif defined(HUB75_P10_3535_16X32_4S) || defined(HUB75_P3_1415_16S_64X64_S31)
    dma_channel_set_read_addr(pixel_chan, &frame_buffer[row_address * (width << 2)], true);
#endif

    // KEEP CORE 1 ALIVE — without this, Core 1's NVIC is torn down and DMA_IRQ_1 stops firing
    while (true)
    {
        tight_loop_contents();
    }
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
    multicore_reset_core1();             // Reset core 1
    multicore_launch_core1(core1_entry); // Launch core 1 entry function - the Hub75 driver is doing its job there
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
            &hub75_data_rgb888_program,
            &pio_config.data_pio,
            &pio_config.sm_data,
            &pio_config.data_prog_offs,
            DATA_BASE_PIN, DATA_N_PINS + 1, true)) // +1 for CLK
    {
        panic("Failed to claim PIO SM for hub75_data_rgb888_program\n");
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

    dma_input_channel_setup(dummy_pixel_chan, 8, DMA_SIZE_32, false, oen_chan, pio_config.data_pio, pio_config.sm_data);
    dma_input_channel_setup(oen_chan, 1, DMA_SIZE_32, true, oen_chan, pio_config.row_pio, pio_config.sm_row);

    pio_sm_set_clkdiv(pio_config.data_pio, pio_config.sm_data, std::max(SM_CLOCKDIV_FACTOR, 1.0f));
    pio_sm_set_clkdiv(pio_config.row_pio, pio_config.sm_row, std::max(SM_CLOCKDIV_FACTOR, 1.0f));

    dma_channel_set_read_addr(dummy_pixel_chan, dummy_pixel_data, false);

    row_in_bit_plane = set_row_in_bit_plane(row_address, bit_plane);
    dma_channel_set_read_addr(oen_chan, &row_in_bit_plane, false);

    dma_channel_config oen_finished_config = dma_channel_get_default_config(oen_finished_chan);
    channel_config_set_transfer_data_size(&oen_finished_config, DMA_SIZE_32);
    channel_config_set_read_increment(&oen_finished_config, false);
    channel_config_set_write_increment(&oen_finished_config, false);
    channel_config_set_dreq(&oen_finished_config, pio_get_dreq(pio_config.row_pio, pio_config.sm_row, false));
    dma_channel_configure(oen_finished_chan, &oen_finished_config, &oen_finished_data, &pio_config.row_pio->rxf[pio_config.sm_row], dma_encode_transfer_count(1), false);
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
    irq_set_exclusive_handler(DMA_IRQ_1, oen_finished_handler);
    dma_channel_set_irq1_enabled(oen_finished_chan, true);
    irq_set_enabled(DMA_IRQ_1, true);
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
#else
// Helper: apply LUT and pack into 30-bit RGB (10 bits per channel)
static inline uint32_t pack_lut_rgb(uint32_t color, const uint16_t *lut)
{
    return (lut[(color & 0x0000ff)] << 20) |
           (lut[(color >> 8) & 0x0000ff] << 10) |
           (lut[(color >> 16) & 0x0000ff]);
}

// Helper: apply LUT and pack into 30-bit RGB (10 bits per channel)
static inline uint32_t pack_lut_rgb_(uint32_t r, uint32_t g, uint32_t b, const uint16_t *lut)
{
    return lut[r] << 20 | lut[g] << 10 | lut[b];
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
    if (graphics->pen_type == PicoGraphics::PEN_RGB888)
    {
        __attribute__((aligned(4))) uint32_t const *src = static_cast<uint32_t const *>(graphics->frame_buffer);

#if defined(HUB75_MULTIPLEX_2_ROWS)
        constexpr size_t pixels = MATRIX_PANEL_WIDTH * MATRIX_PANEL_HEIGHT;
        for (size_t fb_index = 0, j = 0; fb_index < pixels; fb_index += 2, ++j)
        {
            frame_buffer[fb_index] = LUT_MAPPING(j, src[j]);
            frame_buffer[fb_index + 1] = LUT_MAPPING(j + offset, src[j + offset]);
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

            frame_buffer[fb_index] = LUT_MAPPING(index, src[index]);
            frame_buffer[fb_index + 1] = LUT_MAPPING(index + HALF_PANEL_OFFSET, src[index + HALF_PANEL_OFFSET]);

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
        uint32_t *dst = frame_buffer;

        // Each iteration processes 4 physical rows (2 scan-row pairs)
        while (line < (height >> 2))
        {
            // even src lines
            dst[0] = LUT_MAPPING(quarter2, src[quarter2]);
            quarter2++;
            dst[1] = LUT_MAPPING(quarter4, src[quarter4]);
            quarter4++;
            // odd src lines
            dst[line_offset + 0] = LUT_MAPPING(quarter1, src[quarter1]);
            quarter1++;
            dst[line_offset + 1] = LUT_MAPPING(quarter3, src[quarter3]);
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
    }
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
    for (size_t i = 0, j = 0; i < total_pixels; j += 3, i += 2)
    {
        frame_buffer[i] = LUT_MAPPING_RGB(i, src[j], src[j + 1], src[j + 2]);
        frame_buffer[i + 1] = LUT_MAPPING_RGB(i, src[rgb_offset + j], src[rgb_offset + j + 1], src[rgb_offset + j + 2]);
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

        frame_buffer[fb_index] = LUT_MAPPING_RGB(index, src[index], src[index + 1], src[index + 2]);
        index += HALF_PANEL_OFFSET;
        frame_buffer[fb_index + 1] = LUT_MAPPING_RGB(index, src[index], src[index + 1], src[index + 2]);

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
    uint32_t *dst = frame_buffer;

    // Each iteration processes 4 physical rows (2 scan-row pairs)
    while (line < (height >> 2))
    {
        // even src lines
        dst[0] = LUT_MAPPING_RGB(quarter2, src[quarter2], src[quarter2 + 1], src[quarter2 + 2]);
        quarter2 += 3;
        dst[1] = LUT_MAPPING_RGB(quarter4, src[quarter4], src[quarter4 + 1], src[quarter4 + 2]);
        quarter4 += 3;
        // odd src lines
        dst[line_width + 0] = LUT_MAPPING_RGB(quarter1, src[quarter1], src[quarter1 + 1], src[quarter1 + 2]);
        quarter1 += 3;
        dst[line_width + 1] = LUT_MAPPING_RGB(quarter3, src[quarter3], src[quarter3 + 1], src[quarter3 + 2]);
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
}
