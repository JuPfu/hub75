// =============================================================================
// icnd2153.cpp
//
// One-time configuration sequence for a chain of ICND2153 HUB75 driver ICs.
// This runs BEFORE normal HUB75 scanning starts: it borrows the PIO/GPIO
// resources long enough to run PRE_ACT / EN_OP / VSYNC and shift the
// WR_CFG1..WR_CFG4 / WR_DBG register values into every daisy-chained chip,
// then hands the PIO block back so icnd2153_row / icnd2153_bitplane_stream
// can drive the panel normally.
//
// Directly modelled on rul6024.cpp — see icnd2153.pio's icnd2153_write_register
// program for the shared wire protocol (identical to rul6024_write_register:
// a command is decoded purely by its trailing LE-high pulse count), and
// icnd2153.h for the command-signature / register-value constants.
//
// Sequence, per icnd2153_control_command.png:
//   1. PRE_ACT   (14 LE-high pulses, no payload)
//   2. EN_OP     (12 LE-high pulses, no payload)
//   3. VSYNC     ( 3 LE-high pulses, no payload)
//   4. WR_CFG1 .. WR_CFG4, WR_DBG — each carries `display_width` bits of
//      register data (the same 16-bit value repeated once per daisy-chained
//      chip), with the register's own trailing LE-high pulse count as the
//      command signature (4, 6, 8, 10, 2 respectively).
//   5. DATA_LATCH (1 LE-high pulse, no payload) — commits the shifted values.
//
// OPEN QUESTIONS / ASSUMPTIONS carried over from the accompanying chat
// message — please confirm before relying on this in production:
//   - Exact command ORDER above (PRE_ACT -> EN_OP -> VSYNC -> registers ->
//     DATA_LATCH); the annotated scope trace supports this reading but
//     doesn't nail down whether DATA_LATCH belongs at the very end or
//     between other steps.
//   - PWCLK/OEN held HIGH (blanked) for the whole sequence, mirroring
//     RUL6024 — the scope trace's OE toggling during the long register-write
//     burst might mean this needs to keep pulsing instead.
//   - Chain length = display_width / 16 chips, same convention as RUL6024 —
//     confirm 16 is really the per-chip shift-register width here too.
//   - Which CFG2 "Setting Example" (RED/GREEN/BLUE) applies; ICND2153_CFG1_VALUE()
//     currently hardcodes RED (see icnd2153_setup() below).
// =============================================================================

#include <cstdint>
#include <cassert>
#include <algorithm>
#include <vector>

#include "pico/stdlib.h"
#include "hardware/pio.h"

#include "hub75.hpp"
#include "icnd2153.pio.h"

#include "icnd2153.h"

// Cached panel/pin configuration for the chain currently being initialized.
// NOTE: file-scope static -> not reentrant, same caveat as rul6024.cpp's cfg.
static Hub75Config cfg;

// -----------------------------------------------------------------------------
// register_dma_buffer layout — one expanded "DMA word per CLK pulse" image
// per configuration register (REG1..REG4 plus the debug register, REG5),
// each `display_width` uint32_t entries long. Same scheme as rul6024.cpp's
// register_dma_buffer, just with 5 slots instead of 2.
// -----------------------------------------------------------------------------
static constexpr uint32_t REGISTER_SLOT_CFG1 = 0;
static constexpr uint32_t REGISTER_SLOT_CFG2 = 1;
static constexpr uint32_t REGISTER_SLOT_CFG3 = 2;
static constexpr uint32_t REGISTER_SLOT_CFG4 = 3;
static constexpr uint32_t REGISTER_SLOT_DBG = 4;
static constexpr uint32_t REGISTER_SLOT_COUNT = 5;

static std::vector<uint32_t> register_dma_buffer;

// (Re)sizes register_dma_buffer for the given display_width. Must be called
// before the first register_slot() use for that display_width —
// icnd2153_setup() does this as its first step.
static void ensure_register_dma_buffer_capacity(uint32_t display_width)
{
    assert(display_width > 0);
    assert(display_width % 16 == 0); // prepare_register_dma() assumes an integral number of 16-bit chips
    register_dma_buffer.assign(static_cast<size_t>(REGISTER_SLOT_COUNT) * display_width, 0);
}

// Returns a pointer to the start of `slot`'s region within register_dma_buffer.
// Requires ensure_register_dma_buffer_capacity(display_width) to have already
// been called for this display_width.
static inline uint32_t *register_slot(uint32_t slot, uint32_t display_width)
{
    assert(slot < REGISTER_SLOT_COUNT);
    size_t offset = static_cast<size_t>(slot) * display_width;
    assert(offset + display_width <= register_dma_buffer.size());
    return &register_dma_buffer[offset];
}

// -----------------------------------------------------------------------------
// prepare_register_dma()
//
// Expands one 16-bit register value into `display_width` 6-bit-per-lane DMA
// words, MSB first, repeated once per daisy-chained chip — identical scheme
// to rul6024.cpp's prepare_register_dma(). As with RUL6024, every chip in
// the chain currently receives the identical register value; there's no
// support yet for giving different chips different WR_CFG*/WR_DBG values.
// -----------------------------------------------------------------------------
static constexpr uint8_t ICND2153_DATA_HIGH = 0x3f;
static constexpr uint8_t ICND2153_DATA_LOW = 0x00;

static void prepare_register_dma(uint16_t value, uint32_t *dst, uint32_t display_width)
{
    int repeats_per_chain = display_width / 16; // one 16-bit register per chained chip
    for (int chip = 0; chip < repeats_per_chain; ++chip)
    {
        for (int bit = 15; bit >= 0; --bit) // MSB first
        {
            *dst++ = (value & (1u << bit)) ? ICND2153_DATA_HIGH : ICND2153_DATA_LOW;
        }
    }
}

// -----------------------------------------------------------------------------
// icnd2153_setup()
//
// Runs the configuration sequence for one ICND2153 chain — see the sequence
// and open-questions notes at the top of this file.
// -----------------------------------------------------------------------------
void icnd2153_setup(PIO pio, uint sm, uint offset)
{
    uint32_t display_width = cfg.panel.matrix_panel_width * cfg.panel.chain_cols;

    // Must happen before any register_slot() call below.
    ensure_register_dma_buffer_capacity(display_width);

    uint32_t *cfg1_buf = register_slot(REGISTER_SLOT_CFG1, display_width);
    uint32_t *cfg2_buf = register_slot(REGISTER_SLOT_CFG2, display_width);
    uint32_t *cfg3_buf = register_slot(REGISTER_SLOT_CFG3, display_width);
    uint32_t *cfg4_buf = register_slot(REGISTER_SLOT_CFG4, display_width);
    uint32_t *dbg_buf = register_slot(REGISTER_SLOT_DBG, display_width);

    prepare_register_dma(ICND2153_CFG1_VALUE, cfg1_buf, display_width);
    prepare_register_dma(ICND2153_CFG2_VALUE, cfg2_buf, display_width);
    prepare_register_dma(ICND2153_CFG3_VALUE, cfg3_buf, display_width);
    prepare_register_dma(ICND2153_CFG4_VALUE, cfg4_buf, display_width);
    prepare_register_dma(ICND2153_CFG5_VALUE, dbg_buf, display_width);

    icnd2153_write_register_program_init(pio, sm, offset, cfg.pins.data_base_pin, cfg.pins.clk_pin);

    // ---- 1. Pre-activate, enable outputs, vertical sync — all no-payload ----
    // Pre-active command
    icnd2153_write_control_command(pio, sm, ICND2153_CMD_PRE_ACT);
    // Enable all output channels
    icnd2153_write_control_command(pio, sm, ICND2153_CMD_EN_OP);
    // Vertical sync. signal
    icnd2153_write_control_command(pio, sm, ICND2153_CMD_VSYNC);

    // ---- 2. Configuration registers 1..4, then the debug register (REG5) ----
    // Pre-active command
    icnd2153_write_control_command(pio, sm, ICND2153_CMD_PRE_ACT);
    // Write configuration register 1
    icnd2153_write_register(pio, sm, display_width, ICND2153_CMD_WR_CFG1, cfg1_buf);

    // Pre-active command
    icnd2153_write_control_command(pio, sm, ICND2153_CMD_PRE_ACT);
    // Write configuration register 2
    icnd2153_write_register(pio, sm, display_width, ICND2153_CMD_WR_CFG2, cfg2_buf);

    // Pre-active command
    icnd2153_write_control_command(pio, sm, ICND2153_CMD_PRE_ACT);
    // Write configuration register 3
    icnd2153_write_register(pio, sm, display_width, ICND2153_CMD_WR_CFG3, cfg3_buf);

    // Pre-active command
    icnd2153_write_control_command(pio, sm, ICND2153_CMD_PRE_ACT);
    // Write configuration register 4
    icnd2153_write_register(pio, sm, display_width, ICND2153_CMD_WR_CFG4, cfg4_buf);

        // Pre-active command
    icnd2153_write_control_command(pio, sm, ICND2153_CMD_PRE_ACT);
    // Write debug register
    icnd2153_write_register(pio, sm, display_width, ICND2153_CMD_WR_DBG, dbg_buf);

    // ---- 3. Commit the shifted register values ----
    icnd2153_write_control_command(pio, sm, ICND2153_CMD_DATA_LATCH);
    printf(">>>>>WROTE icnd2153_write_control_command\n");
}

// -----------------------------------------------------------------------------
// icnd2153_initialize()
//
// Entry point: claims a PIO state machine covering every GPIO this chain's
// control-command sequence touches (6 data lanes + CLK/LE/PWCLK), runs
// icnd2153_setup(), then releases the program and state machine so the
// normal HUB75 row/bitplane PIO programs can use that PIO block. Structured
// identically to rul6024_initialize().
// -----------------------------------------------------------------------------
void icnd2153_initialize(Hub75Config Cfg)
{
    cfg = Cfg;

    uint sm;
    PIO pio;
    uint offset;

    // Every GPIO this chain's PIO program will touch, so pio_claim_free_...
    // below can compute the minimal contiguous GPIO window to request.
    size_t gpio_pins[] = {
        cfg.pins.data_base_pin,
        cfg.pins.data_base_pin + 5, // last of the 6 RGB data lanes
        cfg.pins.clk_pin,
        cfg.pins.strobe_pin, // LE / LAT
        cfg.pins.oen_pin};   // PWCLK / OEN
    size_t n = sizeof(gpio_pins) / sizeof(gpio_pins[0]);

    size_t min_gpio = *std::min_element(gpio_pins, gpio_pins + n);
    size_t max_gpio = *std::max_element(gpio_pins, gpio_pins + n);

    // Same RP2350B / PIO2 rationale as rul6024_initialize(): force_pio2=true
    // ensures we land on a PIO block that can actually reach GPIO 30-47.
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &icnd2153_write_register_program,
            &pio,
            &sm,
            &offset,
            min_gpio,
            static_cast<uint>(max_gpio - min_gpio + 1),
            true))
    {
        panic("Failed to claim PIO SM for icnd2153_write_register_program\n");
    }

    if (sm < 0)
    {
        printf("icnd2153_initialize: No free SM on this PIO instance!\n");
        return;
    }

    // setup initialisation sequence and emit it to panel
    icnd2153_setup(pio, sm, offset);

    // disable state machine
    pio_sm_set_enabled(pio, sm, false);

    // remove icnd2153_write_register_program and unclaim state machine
    pio_remove_program(pio, &icnd2153_write_register_program, offset);
    pio_sm_unclaim(pio, sm);
}