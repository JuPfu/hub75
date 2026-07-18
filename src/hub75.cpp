#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <memory>

#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pico/sync.h"

#include "hub75.hpp"
#include "hub75.pio.h"

#include "rul6024.h"
#include "fm6126a.h"

#include "cie.hpp"

using HUB75::DISPLAY_HEIGHT;
using HUB75::DISPLAY_WIDTH;
using HUB75::TOTAL_PIXELS;

// Frame buffer for the HUB75 matrix - memory area where pixel data is stored
uint8_t *frame_buffer; ///< Back buffer — written by bitplane builder (read_chan_handler)
uint8_t *dma_buffer;   ///< Front buffer — read by pixel_chan DMA → panel streamer

/**
 * @struct row_cmd_t
 * @brief Command structure for row control PIO state machine.
 *
 * Each entry defines the timing and addressing for one row
 * in a specific bitplane slice.
 *
 * Memory layout (packed, DMA streamed):
 * -------------------------------------
 * [0] addr_delay  : bits[4:0] row_address (A..E lines), bits[31:5] t_addr (PIO cycles)
 * [1] lit_cycles  : OE active duration (LEDs ON)
 * [2] dark_cycles : OE inactive duration (LEDs OFF)
 *
 * addr_delay is packed this way because the hub75_row PIO program consumes
 * it as one 32-bit DMA word: `out pins, 5` peels off the row address, then
 * `out x, 27` takes the rest straight into the address-settle wait loop
 *
 * Constraints:
 * ------------
 * - Must remain tightly packed (no padding!)
 * - Consumed sequentially by DMA → PIO
 * - Total duration per entry is constant:
 *     lit_cycles + dark_cycles = BCM base period
 */
struct row_cmd_t
{
    uint32_t addr_delay;  ///< packed: bits[4:0]=row_address, bits[31:5]=t_addr (address-settling delay, bitplane dependent)
    uint32_t lit_cycles;  ///< OEn ON  duration for this bit plane, scaled by brightness
    uint32_t dark_cycles; ///< OEn OFF duration = full BCM period - lit_cycles

} __attribute__((packed));

constexpr uint32_t row_cmd_struct_members = sizeof(row_cmd_t) / sizeof(uint32_t);

row_cmd_t *row_cmd_buffer;
row_cmd_t *dma_row_cmd_buffer;

static volatile bool swap_row_cmd_buffer_pending = false;
static volatile bool swap_frame_buffer_pending = false;

#if BITPLANES == 10
#if BALANCED_LIGHT_OUTPUT == true
// Split sequence for 10 bitplanes
// Split BP 9 into 4 parts, BP 8 into 2 parts.
static const uint8_t BCM_SEQUENCE[] = {
    9, 0, 8, 1, 9, 2, 7, 3, 9, 4, 8, 5, 9, 6 // 14 steps instead of 10
};
#else
static const uint8_t BCM_SEQUENCE[] = {
    0, 9, 2, 7, 4, 5, 1, 8, 3, 6 // 10 steps
};
#endif
#elif BITPLANES == 8
#if BALANCED_LIGHT_OUTPUT == true
// Split sequence for 8 bitplanes
// Split BP 7 into 3 parts, BP 6 into 2 parts
static const uint8_t BCM_SEQUENCE[] = {
    7, 0, 6, 1, 7, 2, 5, 3, 7, 4, 6 // 11 steps instead of 8
};
#else
static const uint8_t BCM_SEQUENCE[] = {
    0, 7, 2, 5, 1, 6, 3, 4 // 8 steps
};
#endif
#endif

constexpr uint8_t bcm_sequence_length = sizeof(BCM_SEQUENCE) / sizeof(uint8_t);

constexpr float SM_CLOCKDIV = (SM_CLOCKDIV_FACTOR < 1.0f) ? 1.0f : SM_CLOCKDIV_FACTOR;
static uint8_t frame_buffer1[(TOTAL_PIXELS >> 1) * bcm_sequence_length] __attribute__((aligned(4)));
static uint8_t frame_buffer2[(TOTAL_PIXELS >> 1) * bcm_sequence_length] __attribute__((aligned(4)));

static row_cmd_t row_cmd_buffer1[PanelConfig::SCAN_DEPTH * bcm_sequence_length] __attribute__((aligned(4)));
static row_cmd_t row_cmd_buffer2[PanelConfig::SCAN_DEPTH * bcm_sequence_length] __attribute__((aligned(4)));

static uint32_t rgb_buffer[TOTAL_PIXELS] __attribute__((aligned(4)));

static void configure_pio(bool);
static void setup_dma_transfers();

/**
 * @struct hub75_timing_config_t
 * @brief Encapsulates physical and derived timing parameters.
 *
 * Layers:
 * -------
 * 1. Physical domain (ns):
 *    - latch_ns
 *    - addr_ns
 *
 * 2. Derived domain (PIO cycles):
 *    - latch_cycles
 *    - addr_cycles
 *
 * Conversion:
 * -----------
 * Based on:
 *   clk_sys_hz and PIO clock divider
 *
 * This allows:
 * - Hardware portability
 * - Runtime tuning
 */
typedef struct
{
    // --- Physical timing (configuration level) ---
    uint32_t latch_ns; // STB settle
    uint32_t addr_ns;  // Address settle

    // --- Derived PIO-cycles (Runtime) ---
    uint16_t latch_cycles;
    uint16_t addr_cycles;

    // --- System parameter (used for re-scaling) ---
    float clk_sys_hz;
    float clkdiv;

} hub75_timing_config_t;

hub75_timing_config_t hub75_timing_config;

// DMA channel numbers
int row_chan = -1;
int row_ctrl_chan = -1;
int pixel_chan = -1;
int pixel_ctrl_chan = -1;

int read_chan = -1;
int write_chan = -1;

// PIO configuration structure for state machine numbers and corresponding program offsets
static struct
{
    uint sm_data;
    PIO data_pio;
    uint data_prog_offs;
    uint sm_row;
    PIO row_pio;
    uint row_prog_offs;

    uint sm_read;
    PIO pio_read;
    uint offs_read;
} pio_config;

// Variable for bit plane selection
static uint32_t bitplane = 0;

// Variables for brightness control
// Q format shift: Q16 gives 1.0 == (1 << 16) == 65536
#define BRIGHTNESS_FP_SHIFT 16u

// Brightness as fixed-point Q16 ( because it may be changed at runtime)
static uint32_t brightness_fp = (1u << BRIGHTNESS_FP_SHIFT); // default == 1.0

// Basis factor (coarse brightness)
static uint32_t basis_factor = 6u;

// Inverse CIE 1931: perceptual input t (0..1) -> linear light Y (0..1)
//
// L* = t * 100  (scale from normalised to 0..100)
// If L* > 8:    Y = ((L* + 16) / 116)^3
// If L* <= 8:   Y = L* / 903.3
static float cie1931_inverse(float t)
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

/**
 * @brief Compute BCM timing for a given bitplane.
 *
 * @param bitplane       Bit significance (0 = LSB)
 * @param brightness_fp  Brightness in Q16 fixed-point (0..65536)
 * @param[out] lit       Active cycles (OE = ON)
 * @param[out] dark      Inactive cycles (OE = OFF)
 *
 * BCM principle:
 * --------------
 * Each bitplane has weight: weight = 2^bitplane
 *
 * This is scaled by: basis_factor (panel calibration)
 *
 * Brightness control:
 * -------------------
 * brightness_fp scales only the ON time:
 *
 *   lit  = base * brightness
 *   dark = base - lit
 *
 * This guarantees:
 *   constant total time → stable refresh rate
 */
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
    // Address lines masked depending on ROWSEL_N_PINS
    return row & PanelConfig::ADDR_MASK;
}

static inline uint ns_to_pio_cycles(uint32_t ns, float clk_sys_hz, float clkdiv)
{
    float t_cycle_ns = (clkdiv / clk_sys_hz) * 1e9f;
    return (uint)ceilf(ns / t_cycle_ns);
}

static inline void hub75_timing_recompute(hub75_timing_config_t *cfg)
{
    cfg->latch_cycles = ns_to_pio_cycles(cfg->latch_ns, cfg->clk_sys_hz, cfg->clkdiv);
    cfg->addr_cycles = ns_to_pio_cycles(cfg->addr_ns, cfg->clk_sys_hz, cfg->clkdiv);
}

void hub75_set_timing_ns(hub75_timing_config_t *cfg, uint32_t latch_ns, uint32_t addr_ns)
{
    cfg->latch_ns = latch_ns;
    cfg->addr_ns = addr_ns;

    hub75_timing_recompute(cfg);
}

void hub75_timing_init(hub75_timing_config_t *cfg, float clk_sys_hz, float clkdiv)
{
    cfg->latch_ns = BASE_LATCH_NS;
    cfg->addr_ns = BASE_ADDR_NS;

    cfg->clk_sys_hz = clk_sys_hz;
    cfg->clkdiv = clkdiv;

    hub75_timing_recompute(cfg);
}

/**
 * @brief Build row command buffer for a complete frame.
 *
 * Generates timing + addressing sequences for:
 *   all bitplanes × all scan rows
 *
 * Features:
 * ---------
 * - Supports BCM bitplane reordering (BCM_SEQUENCE)
 * - Supports bitplane splitting (balanced light output)
 * - Applies brightness scaling
 * - Applies timing compensation per bitplane
 *
 * Output:
 * -------
 * row_cmd_buffer[] filled sequentially and ready for DMA streaming.
 *
 * Double buffering:
 * -----------------
 * The buffer is not swapped immediately.
 * Instead:
 *   swap_row_cmd_buffer_pending = true
 * and swap occurs in DMA IRQ (safe point).
 */
void hub75_build_row_cmd_buffer(uint32_t brightness_fp)
{
    uint32_t idx = 0;

    // Iterate through BCM sequence
    for (uint8_t bp : BCM_SEQUENCE)
    {
        uint32_t split_factor = 1;
#if BALANCED_LIGHT_OUTPUT
#if BITPLANES == 10
        if (bp == 9)
        {
            // Calculate the "weight" of this slice
            // Split BP 9 into 4 parts, each part gets 1/4 of the duration
            split_factor = 4;
        }
        else if (bp == 8)
        {
            split_factor = 2;
        }
#elif BITPLANES == 8
        if (bp == 7)
        {
            // Calculate the "weight" of this slice
            // Split BP 7 into 3 parts, each part gets 1/3 of the duration
            split_factor = 3;
        }
        else if (bp == 6)
        {
            split_factor = 2;
        }
#endif
#endif
        uint32_t total_lit, total_dark;
        compute_bcm_cycles(bp, brightness_fp, total_lit, total_dark);

        uint32_t base_per_slice = (basis_factor << bp) / split_factor;
        uint32_t lit_cycles = (base_per_slice * brightness_fp) >> BRIGHTNESS_FP_SHIFT;
        uint32_t dark_cycles = base_per_slice - lit_cycles;

        for (uint32_t row = 0; row < PanelConfig::SCAN_DEPTH; ++row)
        {
            uint32_t t_addr = hub75_timing_config.addr_cycles + (bp >> 1); // address settle
            row_cmd_t *cmd = &row_cmd_buffer[idx++];
            // low 5 bits = row address (hub75_row PIO consumes exactly 5 bits via `out pins, 5`),
            // upper 27 bits = t_addr, taken by the following `out x, 27`
            cmd->addr_delay = (t_addr << 5) | (encode_row_address(row) & 0x1Fu);
            cmd->lit_cycles = lit_cycles;
            cmd->dark_cycles = dark_cycles;
        }
    }
    swap_row_cmd_buffer_pending = true;
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

#if FRAME_RATE
// use only for testing or debugging
static int frame_count = 0;
static uint32_t frame_freq_us = 0; // last measured period for N frames
static absolute_time_t frame_time_start;

#define FRAME_MEASURE_INTERVAL 100
#endif

/**
 * @brief DMA IRQ0 handler for frame synchronization and buffer swapping.
 *
 * Responsibilities:
 * -----------------
 * 1. Detect end-of-frame for:
 *    - row DMA (row_ctrl_chan)
 *    - pixel DMA (pixel_ctrl_chan)
 *
 * 2. Perform safe double-buffer swaps:
 *    - row_cmd_buffer
 *    - frame_buffer
 *
 * Design rationale:
 * -----------------
 * Swapping only at frame boundaries ensures:
 * - No tearing
 * - No partially updated frames
 *
 * Lock-free design:
 * -----------------
 * Uses flags:
 *   swap_row_cmd_buffer_pending
 *   swap_frame_buffer_pending
 *
 * No mutexes required.
 */
void ctrl_chan_handler()
{
    if (dma_channel_get_irq0_status(row_ctrl_chan))
    {
        // Clear the interrupt request for DMA channel
        dma_channel_acknowledge_irq0(row_ctrl_chan);

#if FRAME_RATE
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
#endif

        if (swap_row_cmd_buffer_pending)
        {
            // dma_row_cmd_buffer → active front buffer (DMA reads from it).
            // row_cmd_buffer → back buffer (modified by setBasisBrightness).
            // Swap: the new back buffer becomes the new front buffer.

            row_cmd_t *new_front = row_cmd_buffer;
            row_cmd_buffer = (new_front == row_cmd_buffer1) ? row_cmd_buffer2 : row_cmd_buffer1;
            dma_row_cmd_buffer = new_front;

            // Reconfigure row_ctrl_chan with a new dma_row_cmd_buffer pointer
            dma_channel_set_read_addr(row_ctrl_chan, &dma_row_cmd_buffer, false);

            swap_row_cmd_buffer_pending = false;
        }
    }
    else if (dma_channel_get_irq0_status(pixel_ctrl_chan))
    {
        // Clear the interrupt request for DMA channel
        dma_channel_acknowledge_irq0(pixel_ctrl_chan);

        if (swap_frame_buffer_pending)
        {
            // dma_buffer  → active front buffer (DMA streams from it)
            // frame_buffer → back buffer (refilled by read_chan_handler)
            // Swap: the new back buffer becomes the new front buffer.

            uint8_t *new_front = frame_buffer;
            frame_buffer = (new_front == frame_buffer1) ? frame_buffer2 : frame_buffer1;
            dma_buffer = new_front;
            // Reconfigure pixel_ctrl_chan with a new dma_buffer pointer
            dma_channel_set_read_addr(pixel_ctrl_chan, &dma_buffer, false);

            swap_frame_buffer_pending = false;
        }
    }
}

void setup_display_irq()
{
    dma_channel_set_irq0_enabled(row_ctrl_chan, true);
    dma_channel_set_irq0_enabled(pixel_ctrl_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, ctrl_chan_handler);
    irq_set_enabled(DMA_IRQ_0, true);
}

/**
 * @brief DMA IRQ handler for bitplane generation pipeline.
 *
 * This function implements a streaming pipeline:
 *
 * Step-by-step:
 * -------------
 * 1. Select next bitplane from BCM sequence
 * 2. Configure PIO shift amount (extract correct bit)
 * 3. Restart DMA:
 *      rgb_buffer → PIO → frame_buffer
 *
 * When all bitplanes are processed:
 * ---------------------------------
 * - Reset to first bitplane
 * - Signal buffer swap (double buffering)
 *
 * Concurrency notes:
 * ------------------
 * - Runs on DMA IRQ1
 * - Must be deterministic and low-latency
 * - Uses memory barrier (__dmb) before signaling swap
 */
void read_chan_handler()
{
    // Clear the interrupt request for DMA channel
    dma_channel_acknowledge_irq1(read_chan);

    // go through all bitplanes in BCM_SEQUENCE
    if (++bitplane < bcm_sequence_length)
    {
        // Set shift to suit next bitplane
        uint shamt = BCM_SEQUENCE[bitplane];
        hub75_bitplane_setup_set_shift(pio_config.pio_read, pio_config.sm_read, pio_config.offs_read, shamt);

        // Prepare DMA channels for building next bitplane
        uint8_t *plane_dst = frame_buffer + (bitplane * (TOTAL_PIXELS >> 1));
        dma_channel_set_write_addr(write_chan, plane_dst, false);
        dma_channel_set_read_addr(read_chan, rgb_buffer, false);
        dma_start_channel_mask((1u << read_chan) | (1u << write_chan));
    }
    else
    {
        __dmb();

        // Reset shift for bitplane 0
        bitplane = 0;
        uint shamt = BCM_SEQUENCE[bitplane];
        hub75_bitplane_setup_set_shift(pio_config.pio_read, pio_config.sm_read, pio_config.offs_read, shamt);

        // frame_buffer rebuild is complete.
        // Signal to swap frame_buffer
        // - to display new content of frame_buffer on matrix panel
        // - to make new "back-buffer" available for writing
        swap_frame_buffer_pending = true;
    }
}

static void setup_bitplane_creation()
{
    read_chan = dma_claim_unused_channel(true);
    write_chan = dma_claim_unused_channel(true);

    // --- READ CHANNEL (Memory -> PIO) ---
    dma_channel_config read_chan_config = dma_channel_get_default_config(read_chan);
    channel_config_set_transfer_data_size(&read_chan_config, DMA_SIZE_32);
    channel_config_set_read_increment(&read_chan_config, true);
    channel_config_set_write_increment(&read_chan_config, false);
    // DREQ: Wait for PIO TX FIFO space
    channel_config_set_dreq(&read_chan_config, pio_get_dreq(pio_config.pio_read, pio_config.sm_read, true));
    channel_config_set_high_priority(&read_chan_config, true);

    dma_channel_configure(
        read_chan,
        &read_chan_config,
        &pio_config.pio_read->txf[pio_config.sm_read], // Write to PIO TX FIFO
        nullptr,                                       // Read address set later
        dma_encode_transfer_count(TOTAL_PIXELS),       // Total pixel (pairs) to process
        false                                          // Don't start yet
    );

    // --- WRITE CHANNEL (PIO -> Memory) ---
    dma_channel_config write_chan_config = dma_channel_get_default_config(write_chan);
    channel_config_set_transfer_data_size(&write_chan_config, DMA_SIZE_32); // PIO pushes 4 bytes
    channel_config_set_read_increment(&write_chan_config, false);
    channel_config_set_write_increment(&write_chan_config, true);
    // DREQ: Wait for PIO RX FIFO data
    channel_config_set_dreq(&write_chan_config, pio_get_dreq(pio_config.pio_read, pio_config.sm_read, false));

    channel_config_set_high_priority(&write_chan_config, true);

    dma_channel_configure(
        write_chan,
        &write_chan_config,
        nullptr,                                             // Write address set later
        &pio_config.pio_read->rxf[pio_config.sm_read],       // Read from PIO RX FIFO
        dma_encode_transfer_count((TOTAL_PIXELS >> 1) >> 2), // Two colour informations per byte (xxr0g0b0r1b1g1) => (TOTAL_PIXELS >> 1)
                                                             // 4 bytes put in a transfered word => ((TOTAL_PIXELS >> 1) >> 2)
        false                                                // Don't start yet
    );
}

void setup_bitplane_stream_irq()
{
    dma_channel_set_irq1_enabled(read_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_1, read_chan_handler);
    irq_set_enabled(DMA_IRQ_1, true);
}

/**
 * @brief Initializes the HUB75 display by setting up DMA and PIO subsystems.
 *
 * This function configures the necessary hardware components to drive a HUB75
 * LED matrix display. It initializes DMA channels, PIO state machines, and
 * interrupt handlers.
 *
 */
void create_hub75_driver(void)
{
    dma_buffer = frame_buffer1;
    frame_buffer = frame_buffer2;

    dma_row_cmd_buffer = row_cmd_buffer1;
    row_cmd_buffer = row_cmd_buffer2;

    hub75_timing_init(&hub75_timing_config, clock_get_hz(clk_sys), SM_CLOCKDIV);

#if PANEL_TYPE == PANEL_FM6126A
    FM6126A_setup();
#elif PANEL_TYPE == PANEL_RUL6024
    RUL6024_setup();
#endif

    configure_pio(INVERTED_STB);
    setup_dma_transfers();
    setup_bitplane_creation();
    setup_display_irq();
    setup_bitplane_stream_irq();
    hub75_build_row_cmd_buffer(brightness_fp);
}

/**
 * @brief Starts the DMA transfers for the HUB75 display driver.
 *
 * This function initializes the DMA transfers by setting up the write address
 * for the Output Enable finished DMA channel and the read address for pixel data.
 * It ensures that the display begins processing frames.
 */
void start_hub75_driver(void)
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
            DATA_BASE_PIN,
            // This parameter needs to know the lowest and highest GPIO number actually used by the state machine
            // across all its pin groups: out, set, in, and side-set, so it can pick/configure a PIO instance whose window covers both ends.
            // ToDo: Put restraints on the mapping of GPIO pins so this requirement is always fullfilled!
            OEN_PIN - DATA_BASE_PIN + 1,
            true))
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
                ROWSEL_BASE_PIN,
                // This parameter needs to know the lowest and highest GPIO number actually used by the state machine
                // across all its pin groups: out, set, in, and side-set, so it can pick/configure a PIO instance whose window covers both ends.
                // ToDo: Put restraints on the mapping of GPIO pins so this requirement is always fullfilled!
                ROWSEL_N_PINS + 2,
                true))
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
                ROWSEL_BASE_PIN,
                // This parameter needs to know the lowest and highest GPIO number actually used by the state machine
                // across all its pin groups: out, set, in, and side-set, so it can pick/configure a PIO instance whose window covers both ends.
                // ToDo: Put restraints on the mapping of GPIO pins so this requirement is always fullfilled!
                (OEN_PIN - ROWSEL_BASE_PIN + 1),
                true))
        {
            panic("Failed to claim PIO SM for hub75_row_program\n");
        }
    }

    hub75_bitplane_stream_program_init(pio_config.data_pio, pio_config.sm_data, pio_config.data_prog_offs, DATA_BASE_PIN, CLK_PIN, PanelConfig::BITPLANE_STREAM_LENGTH);

    // Implementation of Pimoronis anti ghosting solution: https://github.com/pimoroni/pimoroni-pico/commit/9e7c2640d426f7b97ca2d5e9161d3f0a00f21abf
    // base_latch_wait_cycles passed as parameter to hub75_row program
    if (inverted_stb)
        hub75_row_inverted_program_init(pio_config.row_pio, pio_config.sm_row, pio_config.row_prog_offs, ROWSEL_BASE_PIN, ROWSEL_N_PINS, STROBE_PIN, hub75_timing_config.latch_cycles);
    else
        hub75_row_program_init(pio_config.row_pio, pio_config.sm_row, pio_config.row_prog_offs, ROWSEL_BASE_PIN, ROWSEL_N_PINS, STROBE_PIN, hub75_timing_config.latch_cycles);

    // State machine for "parallelized" building of the bit-plane structure
    if (!pio_claim_free_sm_and_add_program(
            &hub75_bitplane_setup_program,
            &pio_config.pio_read,
            &pio_config.sm_read,
            &pio_config.offs_read))
    {
        panic("Failed to claim PIO SM for hub75_bitplane_setup_program\n");
    }

    hub75_bitplane_setup_program_init(pio_config.pio_read, pio_config.sm_read, pio_config.offs_read);
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

    // One big transfer of the complete content of dma_row_cmd_buffer.
    // The dma_row_cmd_buffer
    //    - has (mostly) different lit cycles and dark cycles for each bitplane
    //    - has the addresses of each row in each bitplane
    dma_channel_configure(row_chan,
                          &row_chan_config,
                          &pio_config.row_pio->txf[pio_config.sm_row],
                          dma_row_cmd_buffer,
                          dma_encode_transfer_count(bcm_sequence_length * PanelConfig::SCAN_DEPTH * row_cmd_struct_members),
                          false);

    // row ctrl channel
    dma_channel_config row_ctrl_chan_config = dma_channel_get_default_config(row_ctrl_chan);

    channel_config_set_transfer_data_size(&row_ctrl_chan_config, DMA_SIZE_32);
    channel_config_set_read_increment(&row_ctrl_chan_config, false);
    channel_config_set_write_increment(&row_ctrl_chan_config, false);

    channel_config_set_dreq(&row_ctrl_chan_config, DREQ_FORCE);

    channel_config_set_high_priority(&row_ctrl_chan_config, true);

    channel_config_set_chain_to(&row_ctrl_chan_config, row_chan);

    // When row_chan has finished a complete frame (each row in each bitplane) has been emitted.
    // The row_ctrl_chan resets the start address of row_chan to dma_row_cmd_buffer.
    dma_channel_configure(row_ctrl_chan, &row_ctrl_chan_config, &dma_hw->ch[row_chan].read_addr, dma_row_cmd_buffer, dma_encode_transfer_count(1), false);

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

    // Due to DMA channel row_chan the complete pre-build bit planes can be passed to DMA channel pixel_chan.
    // The pixel_chan iterates over all bitplanes in one big swoop.
    dma_channel_configure(pixel_chan,
                          &pixel_chan_config,
                          &pio_config.data_pio->txf[pio_config.sm_data],
                          dma_buffer,
                          dma_encode_transfer_count((TOTAL_PIXELS >> 1) * bcm_sequence_length),
                          false);

    // pixel ctrl channel
    dma_channel_config pixel_ctrl_chan_config = dma_channel_get_default_config(pixel_ctrl_chan);

    channel_config_set_transfer_data_size(&pixel_ctrl_chan_config, DMA_SIZE_32);
    channel_config_set_read_increment(&pixel_ctrl_chan_config, false);
    channel_config_set_write_increment(&pixel_ctrl_chan_config, false);

    channel_config_set_dreq(&pixel_ctrl_chan_config, DREQ_FORCE);

    channel_config_set_high_priority(&pixel_ctrl_chan_config, true);

    channel_config_set_chain_to(&pixel_ctrl_chan_config, pixel_chan);

    // When pixel_chan has finished a complete frame (each row in each bitplane) has been emitted.
    // The pixel_ctrl_chan resets the start address of pixel_chan to dma_buffer.
    dma_channel_configure(pixel_ctrl_chan, &pixel_ctrl_chan_config, &dma_hw->ch[pixel_chan].read_addr, dma_buffer, dma_encode_transfer_count(1), false);

    pio_sm_set_clkdiv(pio_config.data_pio, pio_config.sm_data, SM_CLOCKDIV);
    pio_sm_set_clkdiv(pio_config.row_pio, pio_config.sm_row, SM_CLOCKDIV);
}

/**
 * @brief Map logical framebuffer into HUB75 scanline order.
 *
 * This section transforms linear image data into the interleaved format required by HUB75 panels.
 *
 * Key transformations:
 * --------------------
 * 1. Scan multiplexing:
 *    Rows are split into:
 *      SCAN_DEPTH groups
 *      ROWS_IN_PARALLEL interleaved rows
 *
 * 2. Pixel pairing:
 *    Each output word encodes:
 *      r0 g0 b0 (top row)
 *      r1 g1 b1 (bottom row)
 *
 * 3. Panel chaining:
 *    - Horizontal chaining (CHAIN_COLS)
 *    - Vertical chaining (CHAIN_ROWS)
 *    - Serpentine (U-turn) layout
 *
 * 4. Rotation handling:
 *    Odd chain rows are rotated 180° in software
 *
 * Performance considerations:
 * ---------------------------
 * - No divisions inside inner loops
 * - Memory access is mostly linear
 * - LUT + CCM applied inline
 */

// Helper: apply LUT and pack into 30-bit RGB (10 bits per channel)
static inline uint32_t pack_lut_rgb(uint32_t colour)
{
    uint32_t rv = CIE_RED[(colour >> 16u) & 0xFFu];
    uint32_t gv = CIE_GREEN[(colour >> 8u) & 0xFFu];
    uint32_t bv = CIE_BLUE[colour & 0xFFu];
    CCM_APPLY(rv, gv, bv);
    return (bv << 20u) | (gv << 10u) | rv;
}

// Helper: apply LUT and pack into 30-bit RGB (10 bits per channel)
static inline uint32_t pack_lut_rgb_(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t rv = CIE_RED[r];
    uint32_t gv = CIE_GREEN[g];
    uint32_t bv = CIE_BLUE[b];
    CCM_APPLY(rv, gv, bv);
    return (bv << 20u) | (gv << 10u) | rv;
}

/**
 * @brief Calculate offset for current row in panel with coordinatres (v, h) in positive or negative (´reverse´) direction,
 *
 * @param row current row
 * @param v vertical panel position
 * @param h horizontal panel position
 * @param reverse calculate offset for ´reverse´ direction
 */
inline int32_t map_panel_row(int row, int v, int h, bool reverse)
{
    // Reverse physical panel column order for serpentine odd chain rows
    const int32_t phys_h = reverse ? (CHAIN_COLS - 1 - h) : h;

    // Reverse over full panel height (not just SCAN_DEPTH) so that
    // combined with a negative stride_to_paired_row step in the caller,
    // both paired rows land at the correct mirrored source positions.
    const int32_t local_row = reverse ? (MATRIX_PANEL_HEIGHT - 1 - row) : row;

    // Top-left pixel of this panel in the row-major source framebuffer:
    //   v panels down  → v * MATRIX_PANEL_HEIGHT full source rows
    //   phys_h panels right → phys_h * MATRIX_PANEL_WIDTH columns
    const int32_t panel_top_left = v * (int32_t)(MATRIX_PANEL_HEIGHT * DISPLAY_WIDTH) + phys_h * (int32_t)MATRIX_PANEL_WIDTH;

    return panel_top_left + local_row * (int32_t)DISPLAY_WIDTH;
}

// Returns the flat src-buffer index for display coordinate (dx, dy)
static inline constexpr int rotated_src_index(int dx, int dy, int dw, int dh)
{
#if DISPLAY_ROTATION == 90
    // CW 90°: src(x,y) = (dy, dw - 1 - dx)
    // src_width = dh  (Quell-Höhe wird zur neuen Breite)
    return (dw - 1 - dx) * dh + dy;
#elif DISPLAY_ROTATION == 180
    // 180°: src(x,y) = (dw-1-dx, dh-1-dy)
    return (dh - 1 - dy) * dw + (dw - 1 - dx);
#elif DISPLAY_ROTATION == 270
    // CW 270° (= CCW 90°): src(x,y) = (dh-1-dy, dx)
    // src_width = dh
    return dx * dh + (dh - 1 - dy);
#else
    // 0°: identity
    return dy * dw + dx;
#endif
}

// ---------------------------------------------------------------------------
// Shared rotation-lookup helper
//
// Replaces the repeated idiom:
//     const int dy = base / W;             (hoisted once per row-group)
//     ... LUT_MAPPING(src[rotated_src_index(i, dy, W, H)]) ...
//
// IMPORTANT: a flat panel-side index `base` is only guaranteed to be a multiple of W (DISPLAY_WIDTH) when CHAIN_COLS == 1.
// For CHAIN_COLS > 1, map_panel_row() returns a row_base that already carries a horizontal panel offset (h * MATRIX_PANEL_WIDTH),
// which is NOT, in general, a multiple of DISPLAY_WIDTH. Decompose base into (dx_base, dy) with both % and / once per row-group,
// then add the column-local `i` to dx_base (not to dy) before passing to rotated_src_index(). Only the W-divide is needed
// once per (row, v, h[, p]) group; the inner i-loop stays division-free.
// ---------------------------------------------------------------------------
static inline uint32_t rot_lut(const uint32_t *src, int dx_base, int dy, int i, int W, int H)
{
    return LUT_MAPPING(src[rotated_src_index(dx_base + i, dy, W, H)]);
}

// BGR/uint8_t* byte-triple variant for update_bgr()
static inline uint32_t rot_lut_rgb(const uint8_t *src, int dx_base, int dy, int i, int W, int H)
{
    const int32_t rot = rotated_src_index(dx_base + i, dy, W, H) * 3;
    return LUT_MAPPING_RGB(src[rot + 2], src[rot + 1], src[rot]);
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

#if DISPLAY_ROTATION == 90 || DISPLAY_ROTATION == 270
    constexpr int expected_w = DISPLAY_HEIGHT;
    constexpr int expected_h = DISPLAY_WIDTH;
    const char *const error_msg = "For DISPLAY_ROTATION 90/270, width must be DISPLAY_HEIGHT and height must be DISPLAY_WIDTH!";
#else
    constexpr int expected_w = DISPLAY_WIDTH;
    constexpr int expected_h = DISPLAY_HEIGHT;
    const char *const error_msg = "For DISPLAY_ROTATION 0/180, width must be DISPLAY_WIDTH and height must be DISPLAY_HEIGHT!";
#endif

    if (graphics->bounds.w != expected_w || graphics->bounds.h != expected_h)
    {
        printf("\n[HUB75 ERROR] Dimension Mismatch!\n");
        printf("Expected: %dx%d, Got: %dx%d\n", expected_w, expected_h, graphics->bounds.w, graphics->bounds.h);

        // Hard panic halts both pico cores and prints a clean debug trace over the terminal
        panic(error_msg);
    }

    __attribute__((aligned(4))) uint32_t const *src = static_cast<uint32_t const *>(graphics->frame_buffer);

#if ROW_MAPPING == ROW_MAP_STANDARD
#if CHAIN_COLS == 1 && CHAIN_ROWS == 1
    // HUB75_MULTIPLEX_2_ROWS — single panel, with display rotation support.

    constexpr int W = DISPLAY_WIDTH;
    constexpr int H = DISPLAY_HEIGHT;

    constexpr int rows_per_bank = H / PanelConfig::ROWS_IN_PARALLEL;

    int32_t fb_index = 0;

    int dx = 0;          // column:       0 .. W-1, then wraps
    int row_in_bank = 0; // row within one bank: 0 .. rows_per_bank-1

    for (int32_t i = 0; i < PanelConfig::stride_to_paired_row; ++i)
    {
        for (int p = 0; p < PanelConfig::ROWS_IN_PARALLEL; ++p)
        {
            // dy = which display row: bank p starts at p * rows_per_bank
            const int dy = p * rows_per_bank + row_in_bank;
            rgb_buffer[fb_index++] = LUT_MAPPING(src[rotated_src_index(dx, dy, W, H)]);
        }

        // Advance column; roll over into next row-within-bank
        if (++dx == W)
        {
            dx = 0;
            ++row_in_bank; // at most H/ROWS_IN_PARALLEL increments total
        }
    }
#else
    // U-Type Serpentine Chaining
    // Example:
    //    Six matrix panels of width 32 columns and height 32 rows are chained as depicted below:
    //
    //    0 -> 1 -> 2 -> 3 -> 4 -> 5  matrix panels chained
    //
    //    This results in a long matrix panel with 192 columns and 32 rows.
    //    If you want a rectangular 64 x 96 chained matrix panel align the panels with unmodified connections as shown here:
    //
    //                       0 -> 1 U-turn to panel 2
    //                            |
    //                            v
    //    U-turn to panel 4  3 <- 2
    //                       |
    //                       v
    //                       4 -> 5
    //
    //    The connections between each of the panels remains unchanged, but now content of panels 2 and 3 will be rotated for 180°
    //    and panel 2 is positioned below panel 1 and panel 3 below panel 0. The next U-turn positions panel 4 below panel 3 and
    //    panel 5 below panel 2.
    //    We have to adapt the mapping of the src-data to compensate the physical rotation by doing a software rotation.
    //

    constexpr int W = DISPLAY_WIDTH;
    constexpr int H = DISPLAY_HEIGHT;

    // NOTE: rows_per_bank is the step between paired rows *within a single
    // panel's SCAN_DEPTH*, not DISPLAY_HEIGHT / ROWS_IN_PARALLEL. The two
    // coincide only when CHAIN_ROWS == 1. PanelConfig::SCAN_DEPTH is the
    // authoritative source (== MATRIX_PANEL_HEIGHT / ROWS_IN_PARALLEL).
    constexpr int rows_per_bank = PanelConfig::SCAN_DEPTH;

    int32_t fb_index = 0;

    for (int row = 0; row < PanelConfig::SCAN_DEPTH; row++) // row: current row
    {
        for (int v = 0; v < CHAIN_ROWS; v++) // v: panel in row (vertical chain)
        {
            const bool reverse = (CHAIN_MODE == CHAIN_MODE_SERPENTINE) ? (v & 1) : false;

            for (int h = 0; h < CHAIN_COLS; h++) // h: panel in column (horizontal chain)
            {
                // Input parameters
                // row: current row, (v, h): panel coordinates, reverse: U-turn descriptor
                // Output parameters
                // row_base: row offset
                const int32_t row_base = map_panel_row(row, v, h, reverse);

                // row_base is only guaranteed W-aligned when CHAIN_COLS == 1
                // (phys_h * MATRIX_PANEL_WIDTH is otherwise a sub-row offset).
                const int dx_base = row_base % W;
                const int dy_base = row_base / W;

                if (reverse)
                {
                    // Serpentine physical 180° correction (chain topology):
                    //   - scan row reversed  → map_panel_row
                    //   - i traversal        → reversed below
                    //   - multiplex ordering → reversed below
                    // DISPLAY_ROTATION is composited independently via rot_lut().
                    for (int i = MATRIX_PANEL_WIDTH - 1; i >= 0; --i)
                    {
                        for (int p = 0; p < PanelConfig::ROWS_IN_PARALLEL; ++p)
                        {
                            const int dy = dy_base - p * rows_per_bank;
                            rgb_buffer[fb_index++] = rot_lut(src, dx_base, dy, i, W, H);
                        }
                    }
                }
                else
                {
                    for (int i = 0; i < MATRIX_PANEL_WIDTH; ++i)
                    {
                        for (int p = 0; p < PanelConfig::ROWS_IN_PARALLEL; ++p)
                        {
                            const int dy = dy_base + p * rows_per_bank;
                            rgb_buffer[fb_index++] = rot_lut(src, dx_base, dy, i, W, H);
                        }
                    }
                }
            }
        }
    }
#endif // CHAIN_COLS
#elif ROW_MAPPING == ROW_MAP_SPLIT
    // Split-half mapping. Four rows per address. Used by many P10 outdoor panels with split upper/lower-half addressing.
#if CHAIN_COLS == 1 && CHAIN_ROWS == 1
    // Single panel, with display rotation support.
    //
    // index` and `index + HALF_PANEL_OFFSET` are flat pixel indices in [0, W*H).
    // We decompose each into (dx, dy) and redirect through rotated_src_index().

    constexpr int W = DISPLAY_WIDTH;
    constexpr int H = DISPLAY_HEIGHT;

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
        // Panel-side flat index (destination address in display space).
        // Single-panel case: this index is always within [0, W*H), so a
        // direct %/ decomposition (not the row_base-based rot_lut helper) is the natural fit here.
        const int32_t index = !(j & PAIR_HALF_BIT) ? j - (line << PAIR_HALF_SHIFT) : GROUP_ROW_OFFSET + j - ((line + 1) << PAIR_HALF_SHIFT);
        const int32_t index2 = index + HALF_PANEL_OFFSET;

        rgb_buffer[fb_index] = LUT_MAPPING(src[rotated_src_index(index % W, index / W, W, H)]);
        rgb_buffer[fb_index + 1] = LUT_MAPPING(src[rotated_src_index(index2 % W, index2 / W, W, H)]);

        if (++counter >= COLUMN_PAIRS)
        {
            counter = 0;
            ++line;
        }
    }
#else
    // P10 chained — with display rotation support.
    static constexpr uint8_t scan_map[4] = {0, 1, 2, 3};

    constexpr int W = DISPLAY_WIDTH;
    constexpr int H = DISPLAY_HEIGHT;

    size_t fb_index = 0;

    for (int row = 0; row < PanelConfig::SCAN_DEPTH; ++row)
    {
        for (int v = 0; v < CHAIN_ROWS; ++v)
        {
            const bool reverse = (CHAIN_MODE == CHAIN_MODE_SERPENTINE) && (v & 1);

            for (int h = 0; h < CHAIN_COLS; ++h)
            {
                const int32_t row_base = map_panel_row(row, v, h, reverse);

                const int32_t row_ptr[4] = {
                    row_base + scan_map[0] * PanelConfig::stride_to_paired_row,
                    row_base + scan_map[1] * PanelConfig::stride_to_paired_row,
                    row_base + scan_map[2] * PanelConfig::stride_to_paired_row,
                    row_base + scan_map[3] * PanelConfig::stride_to_paired_row,
                };

                // row_ptr[p] is only guaranteed W-aligned when CHAIN_COLS == 1.
                // Decompose fully (dx_base AND dy) per scan group — 4 of eachvper (row, v, h) triplet
                const int dx_base[4] = {row_ptr[0] % W, row_ptr[1] % W, row_ptr[2] % W, row_ptr[3] % W};
                const int dy[4] = {row_ptr[0] / W, row_ptr[1] / W, row_ptr[2] / W, row_ptr[3] / W};

                if (reverse)
                {
                    // Serpentine physical 180° correction:
                    //   - scan row reversed  → map_panel_row
                    //   - i reversed         → below
                    //   - scan group order   → p counts 3..0
                    // DISPLAY_ROTATION composited independently via rot_lut().
                    for (int i = MATRIX_PANEL_WIDTH - 1; i >= 0; --i)
                    {
                        for (int p = 3; p >= 0; --p)
                        {
                            rgb_buffer[fb_index++] = rot_lut(src, dx_base[p], dy[p], i, W, H);
                        }
                    }
                }
                else
                {
                    // Normal orientation
                    for (int i = 0; i < MATRIX_PANEL_WIDTH; ++i)
                    {
                        for (int p = 0; p < 4; ++p)
                        {
                            rgb_buffer[fb_index++] = rot_lut(src, dx_base[p], dy[p], i, W, H);
                        }
                    }
                }
            }
        }
    }
#endif
#elif ROW_MAPPING == ROW_MAP_S31
    // S31 mapping. Four-way interleaved quarter mapping. Used by panels marketed as "...S31".
#if CHAIN_COLS == 1 && CHAIN_ROWS == 1
    // Single panel, with display rotation support.
    //
    // q1..q4 are flat pixel indices advancing sequentially. We decompose each
    // into (dx, dy) and redirect through rotated_src_index().
    // Panel-side write order (dst pointer) is unchanged.
    {
        constexpr int W = DISPLAY_WIDTH;
        constexpr int H = DISPLAY_HEIGHT;

        constexpr uint total_pixels = TOTAL_PIXELS;
        constexpr uint line_offset = PanelConfig::LINE_OFFSET;

        constexpr uint quarter = total_pixels >> 2; // number of pixels in a quarter of the panel

        uint quarter1 = 0 * quarter; // rows in quarter1  0–15
        uint quarter2 = 1 * quarter; // rows in quarter2  16–31
        uint quarter3 = 2 * quarter; // rows in quarter3  32–47
        uint quarter4 = 3 * quarter; // rows in quarter4  48–63

        uint p = 0; // per line pixel counter

        uint line = 0; // Number of logical rows processed

        uint32_t *dst = rgb_buffer; // rgb_buffer write pointer

        // Each iteration processes 4 physical rows (2 scan-row pairs)
        while (line < (PanelConfig::HEIGHT >> 2))
        {
            dst[0] = LUT_MAPPING(src[rotated_src_index(quarter2 % W, quarter2 / W, W, H)]);
            ++quarter2;
            dst[1] = LUT_MAPPING(src[rotated_src_index(quarter4 % W, quarter4 / W, W, H)]);
            ++quarter4;
            dst[line_offset + 0] = LUT_MAPPING(src[rotated_src_index(quarter1 % W, quarter1 / W, W, H)]);
            ++quarter1;
            dst[line_offset + 1] = LUT_MAPPING(src[rotated_src_index(quarter3 % W, quarter3 / W, W, H)]);
            ++quarter3;

            dst += 2;

            // End of logical row
            if (++p >= PanelConfig::WIDTH)
            {
                p = 0;
                line++;
                dst += line_offset; // advance to next scan-row pair
            }
        }
    }
#else
    // P3 chained — with display rotation support.
    constexpr int W = DISPLAY_WIDTH;
    constexpr int H = DISPLAY_HEIGHT;

    size_t fb_index = 0;

    for (int row = 0; row < PanelConfig::SCAN_DEPTH; row++)
    {
        for (int v = 0; v < CHAIN_ROWS; v++)
        {
            const bool reverse = (CHAIN_MODE == CHAIN_MODE_SERPENTINE) && (v & 1);

            for (int h = 0; h < CHAIN_COLS; h++)
            {
                // Input parameters
                // row: current row, (v, h): panel coordinates, reverse: U-turn descriptor
                // Output parameters
                // row_base: row offset
                const int32_t row_base = map_panel_row(row, v, h, reverse);

                // S31 quarter-row layout
                const int32_t sign = reverse ? -1 : 1;
                const int32_t base0 = row_base + sign * 0 * PanelConfig::stride_to_paired_row;
                const int32_t base1 = row_base + sign * 1 * PanelConfig::stride_to_paired_row;
                const int32_t base2 = row_base + sign * 2 * PanelConfig::stride_to_paired_row;
                const int32_t base3 = row_base + sign * 3 * PanelConfig::stride_to_paired_row;

                const int dx_base0 = base0 % W, dy0 = base0 / W;
                const int dx_base1 = base1 % W, dy1 = base1 / W;
                const int dx_base2 = base2 % W, dy2 = base2 / W;
                const int dx_base3 = base3 % W, dy3 = base3 / W;

                if (reverse)
                {
                    // Serpentine physical 180° correction (chain topology):
                    //   - scan row reversed    → map_panel_row
                    //   - i reversed           → below
                    //   - sign on quarter rows → above
                    // DISPLAY_ROTATION composited independently via rot_lut().
                    for (int i = MATRIX_PANEL_WIDTH - 1; i >= 0; --i)
                    {
                        rgb_buffer[fb_index++] = rot_lut(src, dx_base1, dy1, i, W, H);
                        rgb_buffer[fb_index++] = rot_lut(src, dx_base3, dy3, i, W, H);
                    }
                    for (int i = MATRIX_PANEL_WIDTH - 1; i >= 0; --i)
                    {
                        rgb_buffer[fb_index++] = rot_lut(src, dx_base0, dy0, i, W, H);
                        rgb_buffer[fb_index++] = rot_lut(src, dx_base2, dy2, i, W, H);
                    }
                }
                else
                {
                    // Normal orientation
                    for (int i = 0; i < MATRIX_PANEL_WIDTH; ++i)
                    {
                        rgb_buffer[fb_index++] = rot_lut(src, dx_base1, dy1, i, W, H);
                        rgb_buffer[fb_index++] = rot_lut(src, dx_base3, dy3, i, W, H);
                    }
                    for (int i = 0; i < MATRIX_PANEL_WIDTH; ++i)
                    {
                        rgb_buffer[fb_index++] = rot_lut(src, dx_base0, dy0, i, W, H);
                        rgb_buffer[fb_index++] = rot_lut(src, dx_base2, dy2, i, W, H);
                    }
                }
            }
        }
    }
#endif
#endif
    // Kick off building bitplanes from rgb_buffer to be written to frame_buffer
    dma_channel_set_write_addr(write_chan, frame_buffer, false);
    dma_channel_set_read_addr(read_chan, rgb_buffer, false);
    dma_start_channel_mask((1u << read_chan) | (1u << write_chan));
}
#endif

/**
 * @brief Updates the frame buffer with pixel data from the source array.
 *
 * This function takes a source array of pixel data and updates the frame buffer
 * with interleaved pixel values. The pixel values are CIE-corrected to 10 bits using a lookup table.
 *
 * @param src Graphics object to be updated - RGB888 format, 24-bits in uint32_t array
 */
__attribute__((optimize("unroll-loops"))) void update_bgr(const uint8_t *src)
{
#if ROW_MAPPING == ROW_MAP_STANDARD
#if CHAIN_COLS == 1 && CHAIN_ROWS == 1
    // HUB75_MULTIPLEX_2_ROWS — single panel, with display rotation support (BGR byte layout).
    //
    // src is uint8_t* with 3 bytes per pixel: [B, G, R] at byte offset flat_idx*3.
    // Decompose the flat pixel index into (dx, dy) and use rot_lut_rgb(), which applies rotated_src_index() and multiplies by 3 internally.

    constexpr int W = DISPLAY_WIDTH;
    constexpr int H = DISPLAY_HEIGHT;

    constexpr int rows_per_bank = H / PanelConfig::ROWS_IN_PARALLEL;

    int32_t fb_index = 0;

    int dx = 0;
    int row_in_bank = 0;

    for (int32_t i = 0; i < PanelConfig::stride_to_paired_row; ++i)
    {
        for (int p = 0; p < PanelConfig::ROWS_IN_PARALLEL; ++p)
        {
            const int dy = p * rows_per_bank + row_in_bank;
            rgb_buffer[fb_index++] = rot_lut_rgb(src, dx, dy, 0, W, H);
        }

        if (++dx == W)
        {
            dx = 0;
            ++row_in_bank;
        }
    }
#else
    // U-Type Serpentine Chaining
    // Example:
    //    Six matrix panels of width 32 columns and height 32 rows are chained as depicted below:
    //
    //    0 -> 1 -> 2 -> 3 -> 4 -> 5  matrix panels chained
    //
    //    This results in a long matrix panel with 192 columns and 32 rows.
    //    If you want a rectangular 64 x 96 chained matrix panel align the panels with unmodified connections as shown here:
    //
    //                       0 -> 1 U-turn to panel 2
    //                            |
    //                            v
    //    U-turn to panel 4  3 <- 2
    //                       |
    //                       v
    //                       4 -> 5
    //
    //    The connections between each of the panels remains unchanged, but now content of panels 2 and 3 will be rotated for 180°
    //    and panel 2 is positioned below panel 1 and panel 3 below panel 0. The next U-turn positions panel 4 below panel 3 and
    //    panel 5 below panel 2.
    //    We have to adapt the mapping of the src-data to compensate the physical rotation by doing a software rotation.
    //

    constexpr int W = DISPLAY_WIDTH;
    constexpr int H = DISPLAY_HEIGHT;

    // Step between paired rows within a single panel's SCAN_DEPTH — not
    // DISPLAY_HEIGHT / ROWS_IN_PARALLEL. The two only coincide when CHAIN_ROWS == 1.
    constexpr int rows_per_bank = PanelConfig::SCAN_DEPTH;

    size_t fb_index = 0;

    for (int row = 0; row < PanelConfig::SCAN_DEPTH; row++)
    {
        for (int v = 0; v < CHAIN_ROWS; v++)
        {
            const bool reverse = (CHAIN_MODE == CHAIN_MODE_SERPENTINE) && (v & 1);

            for (int h = 0; h < CHAIN_COLS; h++)
            {
                // Input parameters
                // row: current row, (v, h): panel coordinates, reverse: U-turn descriptor
                // Output parameters
                // row_base: row offset
                //
                // map_panel_row():
                //   - selects physical panel
                //   - selects row inside panel
                const int32_t row_base = map_panel_row(row, v, h, reverse);

                // row_base (pixel-domain) is only guaranteed W-aligned when
                // CHAIN_COLS == 1; decompose fully once per (row, v, h).
                const int dx_base = row_base % W;
                const int dy_base = row_base / W;

                if (reverse)
                {
                    // True 180° rotation:
                    //
                    // reverse:
                    //   - local scan row      (done in map_panel_row)
                    //   - i traversal         (done here)
                    //   - multiplex ordering  (done here)
                    // DISPLAY_ROTATION composited independently via rot_lut_rgb().
                    for (int i = MATRIX_PANEL_WIDTH - 1; i >= 0; --i)
                    {
                        for (int p = 0; p < PanelConfig::ROWS_IN_PARALLEL; ++p)
                        {
                            const int dy = dy_base - p * rows_per_bank;
                            rgb_buffer[fb_index++] = rot_lut_rgb(src, dx_base, dy, i, W, H);
                        }
                    }
                }
                else
                {
                    for (int i = 0; i < MATRIX_PANEL_WIDTH; ++i)
                    {
                        for (int p = 0; p < PanelConfig::ROWS_IN_PARALLEL; ++p)
                        {
                            const int dy = dy_base + p * rows_per_bank;
                            rgb_buffer[fb_index++] = rot_lut_rgb(src, dx_base, dy, i, W, H);
                        }
                    }
                }
            }
        }
    }
#endif
#elif ROW_MAPPING == ROW_MAP_SPIT
    // Split-half mapping. Four rows per address. Used by many P10 outdoor panels with split upper/lower-half addressing.
#if CHAIN_COLS == 1 && CHAIN_ROWS == 1
    // Single panel, with display rotation support (BGR byte layout).
    //
    // pf / pf2 are flat pixel indices (no byte multiply). rot_lut_rgb() applies
    // rotated_src_index() and the *3 byte conversion internally.
    constexpr int W = DISPLAY_WIDTH;
    constexpr int H = DISPLAY_HEIGHT;

    int line = 0;
    int counter = 0;

    constexpr int COLUMN_PAIRS = MATRIX_PANEL_WIDTH >> 1;
    constexpr int HALF_PAIRS = COLUMN_PAIRS >> 1;

    constexpr int PAIR_HALF_BIT = HALF_PAIRS;
    constexpr int PAIR_HALF_SHIFT = __builtin_ctz(HALF_PAIRS);

    constexpr int ROW_STRIDE = MATRIX_PANEL_WIDTH;
    constexpr int ROWS_PER_GROUP = MATRIX_PANEL_HEIGHT / SCAN_GROUPS;
    constexpr int GROUP_ROW_OFFSET = ROWS_PER_GROUP * ROW_STRIDE;
    constexpr int HALF_PANEL_OFFSET_PX = (MATRIX_PANEL_HEIGHT >> 1) * ROW_STRIDE; // pixels, not bytes

    constexpr int total_pairs = (MATRIX_PANEL_WIDTH * MATRIX_PANEL_HEIGHT) >> 1;

    for (int j = 0, fb_index = 0; j < total_pairs; ++j, fb_index += 2)
    {
        const int32_t pf = !(j & PAIR_HALF_BIT) ? j - (line << PAIR_HALF_SHIFT) : GROUP_ROW_OFFSET + j - ((line + 1) << PAIR_HALF_SHIFT);
        const int32_t pf2 = pf + HALF_PANEL_OFFSET_PX;

        rgb_buffer[fb_index] = rot_lut_rgb(src, pf % W, pf / W, 0, W, H);
        rgb_buffer[fb_index + 1] = rot_lut_rgb(src, pf2 % W, pf2 / W, 0, W, H);

        if (++counter >= COLUMN_PAIRS)
        {
            counter = 0;
            ++line;
        }
    }
#else
    // P10 chained — with display rotation support (BGR byte layout).
    static constexpr uint8_t scan_map[4] = {0, 1, 2, 3};

    constexpr int W = DISPLAY_WIDTH;
    constexpr int H = DISPLAY_HEIGHT;

    size_t fb_index = 0;

    for (int row = 0; row < PanelConfig::SCAN_DEPTH; ++row)
    {
        for (int v = 0; v < CHAIN_ROWS; ++v)
        {
            const bool reverse = (CHAIN_MODE == CHAIN_MODE_SERPENTINE) && (v & 1);

            for (int h = 0; h < CHAIN_COLS; ++h)
            {
                const int32_t row_base = map_panel_row(row, v, h, reverse);

                // Pixel-domain row pointers (no byte multiply yet — rot_lut_rgb performs the *3 conversion internally after rotation).
                const int32_t row_ptr[4] = {
                    row_base + scan_map[0] * PanelConfig::stride_to_paired_row,
                    row_base + scan_map[1] * PanelConfig::stride_to_paired_row,
                    row_base + scan_map[2] * PanelConfig::stride_to_paired_row,
                    row_base + scan_map[3] * PanelConfig::stride_to_paired_row,
                };

                // row_ptr[p] is only guaranteed W-aligned when CHAIN_COLS == 1.
                // Decompose fully (dx_base AND dy) once per scan group.
                const int dx_base[4] = {row_ptr[0] % W, row_ptr[1] % W, row_ptr[2] % W, row_ptr[3] % W};
                const int dy[4] = {row_ptr[0] / W, row_ptr[1] / W, row_ptr[2] / W, row_ptr[3] / W};

                if (reverse)
                {
                    // 180° rotation
                    //
                    // reverse:
                    //   - scan row       (map_panel_row)
                    //   - traversal      (reverse i)
                    //   - scan group     (reverse p)
                    // DISPLAY_ROTATION composited independently via rot_lut_rgb().
                    for (int i = MATRIX_PANEL_WIDTH - 1; i >= 0; --i)
                    {
                        for (int p = 3; p >= 0; --p)
                        {
                            rgb_buffer[fb_index++] = rot_lut_rgb(src, dx_base[p], dy[p], i, W, H);
                        }
                    }
                }
                else
                {
                    for (int i = 0; i < MATRIX_PANEL_WIDTH; ++i)
                    {
                        for (int p = 0; p < 4; ++p)
                        {
                            rgb_buffer[fb_index++] = rot_lut_rgb(src, dx_base[p], dy[p], i, W, H);
                        }
                    }
                }
            }
        }
    }
#endif
    elif ROW_MAPPING == ROW_MAP_S31
    // S31 mapping. Four-way interleaved quarter mapping. Used by panels marketed as "...S31".
#if CHAIN_COLS == 1 && CHAIN_ROWS == 1
        // Single panel, with display rotation support (BGR byte layout).
        //
        // quarter1..quarter4 are flat pixel-index counters (no *3). rot_lut_rgb() applies
        // rotated_src_index() and the byte conversion internally.
        constexpr int W = DISPLAY_WIDTH;
    constexpr int H = DISPLAY_HEIGHT;

    constexpr uint total_pixels = MATRIX_PANEL_WIDTH * MATRIX_PANEL_HEIGHT;
    constexpr uint line_width = PanelConfig::LINE_OFFSET;

    constexpr uint quarter = (total_pixels >> 2) * 3; // number of pixels in a quarter of the panel

    uint quarter1 = 0 * quarter; // rows in quarter1  0–15
    uint quarter2 = 1 * quarter; // rows in quarter2  16–31
    uint quarter3 = 2 * quarter; // rows in quarter3  32–47
    uint quarter4 = 3 * quarter; // rows in quarter4  48–63

    uint p = 0; // per line pixel counter
    uint line = 0;
    uint32_t *dst = rgb_buffer;

    while (line < (PanelConfig::HEIGHT >> 2))
    {
        dst[0] = rot_lut_rgb(src, quarter2 % W, quarter2 / W, 0, W, H);
        ++quarter2;
        dst[1] = rot_lut_rgb(src, quarter4 % W, quarter4 / W, 0, W, H);
        ++quarter4;
        dst[line_width + 0] = rot_lut_rgb(src, quarter1 % W, quarter1 / W, 0, W, H);
        ++quarter1;
        dst[line_width + 1] = rot_lut_rgb(src, quarter3 % W, quarter3 / W, 0, W, H);
        ++quarter3;

        dst += 2;
        p++;

        // End of logical row
        if (p == PanelConfig::WIDTH)
        {
            p = 0;
            line++;
            dst += line_width; // advance to next scan-row pair
        }
    }
#else
        // P3 chained — with display rotation support (BGR byte layout).
        //
        // U-Type Serpentine Chaining
        // Example:
        //    Six matrix panels of width 32 columns and height 32 rows are chained as depicted below:
        //
        //    0 -> 1 -> 2 -> 3 -> 4 -> 5  matrix panels chained
        //
        //    This results in a long matrix panel with 192 columns and 32 rows.
        //    If you want a rectangular 64 x 96 chained matrix panel align the panels with unmodified connections as shown here:
        //
        //                       0 -> 1 U-turn to panel 2
        //                            |
        //                            v
        //    U-turn to panel 4  3 <- 2
        //                       |
        //                       v
        //                       4 -> 5
        //
        //    The connections between each of the panels remains unchanged, but now content of panels 2 and 3 will be rotated for 180°
        //    and panel 2 is positioned below panel 1 and panel 3 below panel 0. The next U-turn positions panel 4 below panel 3 and
        //    panel 5 below panel 2.
        //    We have to adapt the mapping of the src-data to compensate the physical rotation by doing a software rotation.
        //

        constexpr int W = DISPLAY_WIDTH;
    constexpr int H = DISPLAY_HEIGHT;

    size_t fb_index = 0;

    for (int row = 0; row < PanelConfig::SCAN_DEPTH; row++)
    {
        for (int v = 0; v < CHAIN_ROWS; v++)
        {
            const bool reverse = (CHAIN_MODE == CHAIN_MODE_SERPENTINE) && (v & 1);

            for (int h = 0; h < CHAIN_COLS; h++)
            {
                // Input parameters
                // row: current row, (v, h): panel coordinates, reverse: U-turn descriptor
                // Output parameters
                // row_base: row offset
                const int32_t row_base = map_panel_row(row, v, h, reverse);

                // S31 quarter-row layout (pixel domain — rot_lut_rgb converts to bytes)
                const int32_t sign = reverse ? -1 : 1;
                const int32_t base0 = row_base + sign * 0 * (int32_t)PanelConfig::stride_to_paired_row;
                const int32_t base1 = row_base + sign * 1 * (int32_t)PanelConfig::stride_to_paired_row;
                const int32_t base2 = row_base + sign * 2 * (int32_t)PanelConfig::stride_to_paired_row;
                const int32_t base3 = row_base + sign * 3 * (int32_t)PanelConfig::stride_to_paired_row;

                // baseN is only guaranteed W-aligned when CHAIN_COLS == 1.
                // Decompose fully (dx_base AND dy) once per quarter-row.
                const int dx_base0 = base0 % W, dy0 = base0 / W;
                const int dx_base1 = base1 % W, dy1 = base1 / W;
                const int dx_base2 = base2 % W, dy2 = base2 / W;
                const int dx_base3 = base3 % W, dy3 = base3 / W;

                if (reverse)
                {
                    // 180° rotation:
                    // DISPLAY_ROTATION composited independently via rot_lut_rgb().
                    for (int i = MATRIX_PANEL_WIDTH - 1; i >= 0; --i)
                    {
                        rgb_buffer[fb_index++] = rot_lut_rgb(src, dx_base1, dy1, i, W, H);
                        rgb_buffer[fb_index++] = rot_lut_rgb(src, dx_base3, dy3, i, W, H);
                    }
                    for (int i = MATRIX_PANEL_WIDTH - 1; i >= 0; --i)
                    {
                        rgb_buffer[fb_index++] = rot_lut_rgb(src, dx_base0, dy0, i, W, H);
                        rgb_buffer[fb_index++] = rot_lut_rgb(src, dx_base2, dy2, i, W, H);
                    }
                }
                else
                {
                    // Normal orientation
                    for (int i = 0; i < MATRIX_PANEL_WIDTH; ++i)
                    {
                        rgb_buffer[fb_index++] = rot_lut_rgb(src, dx_base1, dy1, i, W, H);
                        rgb_buffer[fb_index++] = rot_lut_rgb(src, dx_base3, dy3, i, W, H);
                    }

                    for (int i = 0; i < MATRIX_PANEL_WIDTH; ++i)
                    {
                        rgb_buffer[fb_index++] = rot_lut_rgb(src, dx_base0, dy0, i, W, H);
                        rgb_buffer[fb_index++] = rot_lut_rgb(src, dx_base2, dy2, i, W, H);
                    }
                }
            }
        }
    }
#endif
#endif
    // Kick off building bitplanes from rgb_buffer to be written to frame_buffer
    dma_channel_set_write_addr(write_chan, frame_buffer, false);
    dma_channel_set_read_addr(read_chan, rgb_buffer, false);
    dma_start_channel_mask((1u << read_chan) | (1u << write_chan));
}
