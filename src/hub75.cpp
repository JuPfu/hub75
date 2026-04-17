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

#include "cie.hpp"

// Frame buffer for the HUB75 matrix - memory area where pixel data is stored
alignas(32) uint8_t *frame_buffer; ///< Back buffer — written by bitplane builder (read_chan_handler)
uint8_t *frame_buffer1;            ///< Physical buffer A
uint8_t *frame_buffer2;            ///< Physical buffer B
uint8_t *dma_buffer;               ///< Front buffer — read by pixel_chan DMA → panel streamer

static uint32_t *rgb_buffer;

// Three-word record sent to the hub75_row PIO state machine for each row/bit-plane.
// The PIO consumes all three words in order via DMA.
//
// Memory layout (must remain packed, no padding):
//   offset 0: row_address  — 5-bit row select
//   offset 4: lit_cycles   — OEn asserted   (panel ON)  duration, BCM weighted
//   offset 8: dark_cycles  — OEn deasserted (panel OFF) duration
//
// Invariant: lit_cycles + dark_cycles = basis_factor << bit_plane (constant), which guarantees a brightness-independent frame rate.
struct row_cmd_t
{
    uint32_t row_address; ///< 5-bit row select; selects which pair of rows to drive
    uint32_t t_addr;      ///< wait cycles for row address to stabilise (bitplane dependent)
    uint32_t t_oe;        ///< wait cycles before OE enabled (bitplane dependent)
    uint32_t lit_cycles;  ///< OEn ON  duration for this bit plane, scaled by brightness
    uint32_t dark_cycles; ///< OEn OFF duration = full BCM period - lit_cycles

} __attribute__((packed));

constexpr uint32_t row_cmd_struct_members = sizeof(row_cmd_t) / sizeof(uint32_t);

row_cmd_t *row_cmd_buffer;
row_cmd_t *row_cmd_buffer1;
row_cmd_t *row_cmd_buffer2;
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

static void configure_pio(bool);
static void setup_dma_transfers();

// Width and height of the HUB75 LED matrix
static uint width;
static uint height;
typedef struct
{
    // --- Physikalische Zeiten (Konfigurationsebene) ---
    uint32_t latch_ns; // STB settle
    uint32_t addr_ns;  // Address settle
    uint32_t oe_ns;    // OE guard

    // --- Derived PIO-cycles (Runtime) ---
    uint16_t latch_cycles;
    uint16_t addr_cycles;
    uint16_t oe_cycles;

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
    cfg->oe_cycles = ns_to_pio_cycles(cfg->oe_ns, cfg->clk_sys_hz, cfg->clkdiv);
}

void hub75_set_timing_ns(hub75_timing_config_t *cfg, uint32_t latch_ns, uint32_t addr_ns, uint32_t oe_ns)
{
    cfg->latch_ns = latch_ns;
    cfg->addr_ns = addr_ns;
    cfg->oe_ns = oe_ns;

    hub75_timing_recompute(cfg);
}

void hub75_timing_init(hub75_timing_config_t *cfg, float clk_sys_hz, float clkdiv)
{
    cfg->latch_ns = BASE_LATCH_NS;
    cfg->addr_ns = BASE_ADDR_NS;
    cfg->oe_ns = BASE_OE_NS;

    cfg->clk_sys_hz = clk_sys_hz;
    cfg->clkdiv = clkdiv;

    hub75_timing_recompute(cfg);
}

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

        // Divide the time by the number of times this BP appears in the sequence
        uint32_t lit_cycles = split_factor == 1 ? total_lit : ((total_lit + split_factor / 2) / split_factor);
        uint32_t dark_cycles = split_factor == 1 ? total_dark : ((total_dark + split_factor / 2) / split_factor);

        for (uint32_t row = 0; row < PanelConfig::SCAN_DEPTH; ++row)
        {
            row_cmd_t *cmd = &row_cmd_buffer[idx++];
            cmd->row_address = encode_row_address(row);
            cmd->t_addr = hub75_timing_config.addr_cycles + (bp >> 1); // very effective
            cmd->t_oe = hub75_timing_config.oe_cycles + (bp >> 2);     // fine tuning
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
            dma_row_cmd_buffer = row_cmd_buffer;

            row_cmd_buffer = (dma_row_cmd_buffer == row_cmd_buffer1) ? row_cmd_buffer2 : row_cmd_buffer1;

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
            dma_buffer = frame_buffer;

            frame_buffer = (dma_buffer == frame_buffer1) ? frame_buffer2 : frame_buffer1;

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
        uint8_t *plane_dst = frame_buffer + (bitplane * (PIXELS >> 1));
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
        dma_encode_transfer_count(PIXELS),             // Total pixel (pairs) to process
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
        nullptr,                                       // Write address set later
        &pio_config.pio_read->rxf[pio_config.sm_read], // Read from PIO RX FIFO
        dma_encode_transfer_count((PIXELS >> 1) >> 2), // Two colour informations per byte (xxr0g0b0r1b1g1) => (PIXELS >> 1)
                                                       // 4 bytes put in a transfered word => ((PIXELS >> 1) >> 2)
        false                                          // Don't start yet
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
 * @param w Width of the HUB75 display in pixels.
 * @param h Height of the HUB75 display in pixels.
 */
void create_hub75_driver(uint w, uint h, uint panel_type = PANEL_TYPE, bool inverted_stb = INVERTED_STB)
{
    width = w;
    height = h;

    frame_buffer1 = new uint8_t[(PIXELS >> 1) * bcm_sequence_length];
    frame_buffer2 = new uint8_t[(PIXELS >> 1) * bcm_sequence_length];

    dma_buffer = frame_buffer1;
    frame_buffer = frame_buffer2;

    row_cmd_buffer1 = new row_cmd_t[PanelConfig::SCAN_DEPTH * bcm_sequence_length];
    row_cmd_buffer2 = new row_cmd_t[PanelConfig::SCAN_DEPTH * bcm_sequence_length];

    dma_row_cmd_buffer = row_cmd_buffer1;
    row_cmd_buffer = row_cmd_buffer2;

    rgb_buffer = new uint32_t[width * height]();

    hub75_timing_init(&hub75_timing_config, clock_get_hz(clk_sys), (SM_CLOCKDIV_FACTOR < 1.0f) ? 1.0f : SM_CLOCKDIV_FACTOR);

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

    hub75_bitplane_stream_program_init(pio_config.data_pio, pio_config.sm_data, pio_config.data_prog_offs, DATA_BASE_PIN, CLK_PIN, PanelConfig::SCAN_MODE_WIDTH);

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
                          dma_encode_transfer_count((PIXELS >> 1) * bcm_sequence_length),
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
    dma_channel_configure(pixel_ctrl_chan, &pixel_ctrl_chan_config, &dma_hw->ch[pixel_chan].read_addr, dma_buffer, 1, false);

    constexpr float clkdiv = (SM_CLOCKDIV_FACTOR < 1.0f) ? 1.0f : SM_CLOCKDIV_FACTOR;

    pio_sm_set_clkdiv(pio_config.data_pio, pio_config.sm_data, clkdiv);
    pio_sm_set_clkdiv(pio_config.row_pio, pio_config.sm_row, clkdiv);
}

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
    constexpr size_t offset = PIXELS >> 1;
    for (size_t fb_index = 0, j = 0; fb_index < PIXELS; fb_index += 2, ++j)
    {
        rgb_buffer[fb_index] = LUT_MAPPING(src[j]);
        rgb_buffer[fb_index + 1] = LUT_MAPPING(src[j + offset]);
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

        rgb_buffer[fb_index] = LUT_MAPPING(src[index]);
        rgb_buffer[fb_index + 1] = LUT_MAPPING(src[index + HALF_PANEL_OFFSET]);

        if (++counter >= COLUMN_PAIRS)
        {
            counter = 0;
            ++line;
        }
    }
#elif defined HUB75_P3_1415_16S_64X64_S31
    constexpr uint total_pixels = MATRIX_PANEL_WIDTH * MATRIX_PANEL_HEIGHT;
    constexpr uint line_offset = PanelConfig::SCAN_MODE_WIDTH;

    constexpr uint quarter = total_pixels >> 2; // number of pixels in a quarter of the panel

    uint quarter1 = 0 * quarter; // rows in quarter1  0–15
    uint quarter2 = 1 * quarter; // rows in quarter2  16–31
    uint quarter3 = 2 * quarter; // rows in quarter3  32–47
    uint quarter4 = 3 * quarter; // rows in quarter4  48–63

    uint p = 0; // per line pixel counter

    uint line = 0; // Number of logical rows processed

    uint32_t *dst = rgb_buffer; // rgb_buffer write pointer

    // Each iteration processes 4 physical rows (2 scan-row pairs)
    while (line < (height >> 2))
    {
        dst[0] = LUT_MAPPING(src[quarter2++]);
        dst[1] = LUT_MAPPING(src[quarter4++]);
        dst[line_offset + 0] = LUT_MAPPING(src[quarter1++]);
        dst[line_offset + 1] = LUT_MAPPING(src[quarter3++]);

        dst += 2;

        // End of logical row
        if (++p >= width)
        {
            p = 0;
            line++;
            dst += line_offset; // advance to next scan-row pair
        }
    }
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
 * with interleaved pixel values. The pixel values are gamma-corrected to 10 bits using a lookup table.
 *
 * @param src Graphics object to be updated - RGB888 format, 24-bits in uint32_t array
 */
__attribute__((optimize("unroll-loops"))) void update_bgr(const uint8_t *src)
{
#ifdef HUB75_MULTIPLEX_2_ROWS
    constexpr size_t offset = (PIXELS >> 1) * 3;
    for (size_t fb_index = 0, j = 0; fb_index < PIXELS; j += 3, fb_index += 2)
    {
        rgb_buffer[fb_index] = LUT_MAPPING_RGB(src[j + 2], src[j + 1], src[j]);
        rgb_buffer[fb_index + 1] = LUT_MAPPING_RGB(src[offset + j + 2], src[offset + j + 1], src[offset + j]);
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

        rgb_buffer[fb_index] = LUT_MAPPING_RGB(src[index + 2], src[index + 1], src[index]);
        index += HALF_PANEL_OFFSET;
        rgb_buffer[fb_index + 1] = LUT_MAPPING_RGB(src[index + 2], src[index + 1], src[index]);

        if (++counter >= COLUMN_PAIRS)
        {
            counter = 0;
            ++line;
        }
    }
#elif defined HUB75_P3_1415_16S_64X64_S31
    constexpr uint total_pixels = MATRIX_PANEL_WIDTH * MATRIX_PANEL_HEIGHT;
    constexpr uint line_width = PanelConfig::SCAN_MODE_WIDTH;

    constexpr uint quarter = (total_pixels >> 2) * 3; // number of pixels in a quarter of the panel

    uint quarter1 = 0 * quarter; // rows in quarter1  0–15
    uint quarter2 = 1 * quarter; // rows in quarter2  16–31
    uint quarter3 = 2 * quarter; // rows in quarter3  32–47
    uint quarter4 = 3 * quarter; // rows in quarter4  48–63

    uint p = 0; // per line pixel counter

    uint line = 0; // Number of logical rows processed

    uint32_t *dst = rgb_buffer; // rgb_buffer write pointer

    // Each iteration processes 4 physical rows (2 scan-row pairs)
    while (line < (height >> 2))
    {
        dst[0] = LUT_MAPPING_RGB(src[quarter2 + 2], src[quarter2 + 1], src[quarter2]);
        quarter2 += 3;
        dst[1] = LUT_MAPPING_RGB(src[quarter4 + 2], src[quarter4 + 1], src[quarter4]);
        quarter4 += 3;
        dst[line_width + 0] = LUT_MAPPING_RGB(src[quarter1 + 2], src[quarter1 + 1], src[quarter1]);
        quarter1 += 3;
        dst[line_width + 1] = LUT_MAPPING_RGB(src[quarter3 + 2], src[quarter3 + 1], src[quarter3]);
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
    // Kick off building bitplanes from rgb_buffer to be written to frame_buffer
    dma_channel_set_write_addr(write_chan, frame_buffer, false);
    dma_channel_set_read_addr(read_chan, rgb_buffer, false);
    dma_start_channel_mask((1u << read_chan) | (1u << write_chan));
}
