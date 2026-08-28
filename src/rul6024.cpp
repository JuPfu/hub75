// =============================================================================
// rul6024.cpp
//
// One-time configuration ("register write") sequence for a chain of
// RUL6024 HUB75 driver ICs. This runs BEFORE normal HUB75 scanning starts:
// it borrows the PIO/GPIO resources long enough to shift WREG1 and WREG2
// into every daisy-chained chip, then hands the PIO block back so
// hub75_row / hub75_bitplane_stream can drive the panel normally.
//
// Protocol background: see rul6024.h for the LE-length command scheme and
// the WREG1/WREG2 values used here.
// =============================================================================

#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <algorithm>

#include "pico/stdlib.h"
#include "hardware/pio.h"

#include "hub75.hpp"
#include "hub75.pio.h"

#include "rul6024.h"

// Cached panel/pin configuration for the chain currently being initialized.
// NOTE: file-scope static -> not reentrant. See the rul6024_initialize()
// doc comment in rul6024.h for the implication (one chain in flight at a
// time).
static Hub75Config cfg;

// -----------------------------------------------------------------------------
// register_dma_buffer layout
//
// rul6024_write_register() (PIO helper, see hub75.pio) expects one
// already-expanded "DMA word per CLK pulse" image per register write: one
// uint32_t per output CLK cycle, each holding a 6-bit value (one bit per
// RGB sub-pixel lane) for that cycle. prepare_register_dma() produces
// exactly `display_width` such words per register (display_width = one CLK
// pulse per bit position, repeated across the whole daisy chain).
//
// register_dma_buffer holds up to REGISTER_SLOT_COUNT such images
// side-by-side, addressed by slot index rather than a hardcoded byte
// offset — so the layout stays correct for any configured display_width
// (MATRIX_PANEL_WIDTH * CHAIN_COLS), instead of only working for whichever
// width happened to be used when the offsets were last hand-picked.
// -----------------------------------------------------------------------------
static constexpr uint32_t REGISTER_SLOT_WREG1 = 0;
static constexpr uint32_t REGISTER_SLOT_WREG2 = 1;
static constexpr uint32_t REGISTER_SLOT_TEST  = 2;  // only used under RUL6024_PROBE_RESERVED
static constexpr uint32_t REGISTER_SLOT_COUNT = 3;

// Largest display_width (MATRIX_PANEL_WIDTH * CHAIN_COLS) this driver is
// built to support. Raise this — and register_dma_buffer's size follows
// automatically — if a wider chain is ever configured. register_slot()'s
// assert() below turns a mismatch into an immediate, loud failure instead
// of silently corrupting the neighbouring slot the way fixed byte offsets
// used to.
static constexpr uint32_t RUL6024_MAX_DISPLAY_WIDTH = 64;

static uint32_t register_dma_buffer[REGISTER_SLOT_COUNT * RUL6024_MAX_DISPLAY_WIDTH];

// Returns a pointer to the start of `slot`'s region within
// register_dma_buffer, sized for the given display_width. Centralizing this
// (rather than repeating `slot * display_width` at each call site) means
// there is exactly one place that can get the indexing wrong.
static inline uint32_t *register_slot(uint32_t slot, uint32_t display_width)
{
    assert(display_width <= RUL6024_MAX_DISPLAY_WIDTH);
    assert(slot < REGISTER_SLOT_COUNT);
    return &register_dma_buffer[slot * display_width];
}

// -----------------------------------------------------------------------------
// prepare_register_dma()
//
// Expands one 16-bit register value into `display_width` 6-bit-per-lane DMA
// words, MSB (bit 15) first, matching the chip's documented shift order
// ("the data transmitted to the chip first is the high bit of the
// register"). Each 16-bit value is repeated back-to-back to fill the whole
// chain: display_width / 16 gives the number of daisy-chained chips, so
// e.g. for display_width = 64 the same 16-bit value is written 4 times in
// a row, one per chip in the chain.
//
// Each output word is either:
//     RUL6024_DATA_HIGH (0x3f) -> all six data lanes driven HIGH this cycle
//     RUL6024_DATA_LOW  (0x00) -> all six data lanes driven LOW this cycle
//
// i.e. every chip in the chain receives the identical register value —
// there is currently no support for giving different chips in one chain
// different WREG1/WREG2 values.
// -----------------------------------------------------------------------------
static constexpr uint8_t RUL6024_DATA_HIGH = 0x3f;
static constexpr uint8_t RUL6024_DATA_LOW = 0x00;

static void prepare_register_dma(uint16_t value, uint32_t *dst, uint32_t display_width)
{
    int repeats_per_chain = display_width / 16;  // one 16-bit register per chained chip
    for (int chip = 0; chip < repeats_per_chain; ++chip)
    {
        for (int bit = 15; bit >= 0; --bit)  // MSB first
        {
            *dst++ = (value & (1u << bit)) ? RUL6024_DATA_HIGH : RUL6024_DATA_LOW;
        }
    }
}

// -----------------------------------------------------------------------------
// rul6024_setup()
//
// Runs the confirmed-working configuration sequence for one RUL6024 chain:
//
//   1. Build the WREG1 / WREG2 DMA images (and, optionally, a probe image
//      for the reserved 4–10 command range — see RUL6024_PROBE_RESERVED
//      below).
//   2. Initialize the rul6024_write_register PIO program on the given
//      state machine.
//   3. Write CMD_WREG1, then CMD_WREG2, in that order.
//
// Order matters: WREG2-before-WREG1 was tried previously and left the
// display showing nothing but init leftovers, so WREG1 must go first.
//
// Unlike an earlier iteration of this file, this sequence does NOT send
// CMD_DATA_LATCH or CMD_RESET_OEN afterwards, and the PIO program's OE
// commit pulse (see the comment above .wrap in hub75.pio) stays disabled.
// Testing confirms panel initialization is reliable without them for this
// single-chain configuration. If you extend this to a chained/multi-group
// setup and see initialization become flaky again, this is the first place
// to revisit — reintroduce DATA_LATCH/RESET_OEN behind a flag and compare.
// -----------------------------------------------------------------------------
void rul6024_setup(PIO pio, uint sm, uint offset)
{
    uint32_t display_width = cfg.panel.matrix_panel_width * cfg.panel.chain_cols;

    uint32_t *wreg1_buf = register_slot(REGISTER_SLOT_WREG1, display_width);
    uint32_t *wreg2_buf = register_slot(REGISTER_SLOT_WREG2, display_width);

    prepare_register_dma(RUL6024_WREG1_VALUE, wreg1_buf, display_width);
    prepare_register_dma(RUL6024_WREG2_VALUE, wreg2_buf, display_width);

    rul6024_write_register_program_init(pio, sm, offset, cfg.pins.data_base_pin, cfg.pins.clk_pin);

    // ---------------------------------------------------------------------
    // Optional: probe the undocumented "reserved" 4–10 command range.
    //
    // Off by default (register_dma_buffer's REGISTER_SLOT_TEST simply goes
    // unused when this isn't defined). Enable with
    // -DRUL6024_PROBE_RESERVED, edit test_data[] to the 16-bit value you
    // want tried for each register (index 0 = register 4, ... index 6 =
    // register 10), and observe the panel after each individual write —
    // ideally one register at a time, not all seven in one boot, so an
    // observed effect can be attributed to a specific register.
    // ---------------------------------------------------------------------
#ifdef RUL6024_PROBE_RESERVED
    uint32_t *test_buf = register_slot(REGISTER_SLOT_TEST, display_width);
    uint16_t test_data[] = {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000};  // registers 4..10

    for (uint32_t reg = 4; reg <= 10; ++reg)
    {
        prepare_register_dma(test_data[reg - 4], test_buf, display_width);
        rul6024_write_register(pio, sm, display_width, reg, test_buf);
    }
#endif

    rul6024_write_register(pio, sm, display_width, CMD_WREG1, wreg1_buf);
    rul6024_write_register(pio, sm, display_width, CMD_WREG2, wreg2_buf);
}

// -----------------------------------------------------------------------------
// rul6024_initialize()
//
// Entry point: claims a PIO state machine covering every GPIO this chain's
// register-write sequence touches (6 data lanes + CLK/LE/OEN), runs
// rul6024_setup(), then releases the program and state machine so the
// normal HUB75 row/bitplane PIO programs can use that PIO block.
// -----------------------------------------------------------------------------
void rul6024_initialize(Hub75Config Cfg)
{
    cfg = Cfg;

    uint sm;
    PIO pio;
    uint offset;

    // Every GPIO this chain's PIO program will touch, so pio_claim_free_...
    // below can compute the minimal contiguous GPIO window to request.
    // data_base_pin .. data_base_pin+5 covers the 6 RGB data lanes;
    // clk_pin, strobe_pin (LE) and oen_pin must be the remaining three
    // "set pins" used by rul6024_write_register (see hub75.pio — they are
    // assumed consecutive: clk_pin, clk_pin+1, clk_pin+2).
    size_t gpio_pins[] = {
        cfg.pins.data_base_pin,
        cfg.pins.data_base_pin + 5,  // last of the 6 RGB data lanes
        cfg.pins.clk_pin,
        cfg.pins.strobe_pin,
        cfg.pins.oen_pin};
    size_t n = sizeof(gpio_pins) / sizeof(gpio_pins[0]);

    size_t min_gpio = *std::min_element(gpio_pins, gpio_pins + n);
    size_t max_gpio = *std::max_element(gpio_pins, gpio_pins + n);

    // On RP2350B, GPIO 30-47 are only reachable via PIO2. Passing `true`
    // (force_pio2 / "require exact instance", per this project's helper)
    // ensures we land on a PIO block that can actually reach these pins,
    // rather than silently claiming an SM that can't.
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &rul6024_write_register_program,
            &pio,
            &sm,
            &offset,
            min_gpio,
            // Needs the lowest and highest GPIO actually used across all of
            // the state machine's pin groups (out, set, in, side-set) so it
            // can pick a PIO instance whose addressable window covers both
            // ends.
            static_cast<uint>(max_gpio - min_gpio + 1),
            true))
    {
        panic("Failed to claim PIO SM for rul6024_write_register_program\n");
    }

    if (sm < 0)
    {
        printf("rul6024_initialize: No free SM on this PIO instance!\n");
        return;
    }

    rul6024_setup(pio, sm, offset);
    pio_sm_set_enabled(pio, sm, false);

    pio_remove_program(pio, &rul6024_write_register_program, offset);
    pio_sm_unclaim(pio, sm);
}
